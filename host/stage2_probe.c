/* Host-side probe for stage 2.
 *
 * It builds two small distance tables, one over the 5,040 corner
 * permutations and one over the 729 orientations, and uses
 * h = max(perm_dist[p], ori_dist[o]) as the IDA* heuristic. It then checks
 * that heuristic against the exact BFS distances, estimates the IDA* cost,
 * and counts the nodes IDA* actually visits.
 *
 * Build and run from the repository root:
 *   cc -O2 -std=c99 -Wall -Wextra -o host/stage2_probe host/stage2_probe.c
 *   ./host/stage2_probe                  tables, H1/H2, estimate, sample state
 *   ./host/stage2_probe 21345671111111   solve one state, per-bound counts
 *   ./host/stage2_probe --depth11        IDA* over all 2,644 distance-11 states
 *   ./host/stage2_probe --all            H3: IDA* over all 3,674,160 states
 */

/* Reuse the cube model of solver.c unchanged, so this probe and the solver
 * cannot disagree about what a move does. Its main is renamed so this file
 * can define its own.
 */
#define main solver_main
#include "../solver.c"
#undef main

enum { MAX_DEPTH = 20, NO_FACE = 3 };

/* Quarter-turn transitions, the same factored tables build_table uses. */
static uint16_t perm_turn[3][PERMUTATIONS], ori_turn[3][ORIENTATIONS];
/* Distance to solved in each projection: the two heuristic tables. */
static uint8_t perm_dist[PERMUTATIONS], ori_dist[ORIENTATIONS];
/* Exact distance of every state. Host-only oracle, never on the target. */
static uint8_t *exact;
static uint8_t diameter;

static uint8_t path[MAX_DEPTH];
static unsigned long long generated, expanded;

static void build_turn_tables(void)
{
    state_t state;
    for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
        unrank_state((uint32_t) p * ORIENTATIONS, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            perm_turn[face][p] = (uint16_t) (rank_state(&next) / ORIENTATIONS);
        }
    }
    for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
        unrank_state(o, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            ori_turn[face][o] = (uint16_t) (rank_state(&next) % ORIENTATIONS);
        }
    }
}

/* BFS inside one projection. turn points at a [3][size] table, so entry
 * (face, x) sits at turn[face * size + x]. Index 0 is solved in both
 * projections.
 */
static void bfs_projection(const uint16_t *turn, uint16_t size, uint8_t *dist)
{
    uint16_t queue[PERMUTATIONS];
    uint16_t head = 0, tail = 1;
    memset(dist, UINT8_MAX, size);
    dist[0] = 0;
    queue[0] = 0;
    while (head < tail) {
        uint16_t here = queue[head++];
        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t there = here;
            for (uint8_t t = 0; t < 3; ++t) {
                there = turn[face * size + there];
                if (dist[there] == UINT8_MAX) {
                    dist[there] = (uint8_t) (dist[here] + 1U);
                    queue[tail++] = there;
                }
            }
        }
    }
}

/* Exact BFS over the full space, level by level: scan for the states at the
 * current level and label their unseen neighbours. No queue is needed.
 * Returns 0 if some state was never reached.
 */
static int build_exact(void)
{
    uint32_t seen = 1;
    exact = malloc(STATES);
    if (!exact)
        return 0;
    memset(exact, UINT8_MAX, STATES);
    exact[0] = 0;
    for (uint8_t level = 0;; ++level) {
        uint32_t found = 0;
        for (uint32_t rank = 0; rank < STATES; ++rank) {
            if (exact[rank] != level)
                continue;
            uint16_t p = (uint16_t) (rank / ORIENTATIONS);
            uint16_t o = (uint16_t) (rank % ORIENTATIONS);
            for (uint8_t face = 0; face < 3; ++face) {
                uint16_t next_p = p, next_o = o;
                for (uint8_t t = 0; t < 3; ++t) {
                    next_p = perm_turn[face][next_p];
                    next_o = ori_turn[face][next_o];
                    uint32_t there = (uint32_t) next_p * ORIENTATIONS + next_o;
                    if (exact[there] == UINT8_MAX) {
                        exact[there] = (uint8_t) (level + 1U);
                        ++found;
                    }
                }
            }
        }
        if (!found) {
            diameter = level;
            return seen == STATES;
        }
        seen += found;
    }
}

/* The heuristic under test. Swap this function to try a different one. */
static uint8_t heuristic(uint16_t p, uint16_t o)
{
    uint8_t a = perm_dist[p], b = ori_dist[o];
    return a > b ? a : b;
}

/* Depth-first search with a fixed bound. g moves have been made so far and
 * last_face is the face of the latest one. Returns 1 when it reaches solved,
 * with the moves in path[0..g-1].
 */
static int search(uint16_t p, uint16_t o, uint8_t g, uint8_t bound,
                  uint8_t last_face)
{
    ++generated;
    if (g + heuristic(p, o) > bound)
        return 0;
    ++expanded;
    if (p == 0 && o == 0)
        return 1;
    for (uint8_t face = 0; face < 3; ++face) {
        /* Turning the same face twice in a row is never shortest. */
        if (face == last_face)
            continue;
        uint16_t next_p = p, next_o = o;
        for (uint8_t t = 0; t < 3; ++t) {
            next_p = perm_turn[face][next_p];
            next_o = ori_turn[face][next_o];
            path[g] = (uint8_t) (face * 3U + t);
            if (search(next_p, next_o, (uint8_t) (g + 1U), bound, face))
                return 1;
        }
    }
    return 0;
}

typedef struct {
    uint8_t length;
    unsigned long long generated, expanded;
} result_t;

/* IDA*: raise the bound one move at a time, starting from h(root). */
static result_t solve(uint32_t rank, int verbose)
{
    uint16_t p = (uint16_t) (rank / ORIENTATIONS);
    uint16_t o = (uint16_t) (rank % ORIENTATIONS);
    result_t result = {0, 0, 0};
    for (uint8_t bound = heuristic(p, o);; ++bound) {
        if (bound > diameter) {
            fputs("search passed the diameter\n", stderr);
            exit(1);
        }
        generated = expanded = 0;
        int found = search(p, o, 0, bound, NO_FACE);
        if (verbose)
            printf("  bound %2u: generated %12llu, expanded %12llu\n", bound,
                   generated, expanded);
        result.generated += generated;
        result.expanded += expanded;
        if (found) {
            result.length = bound;
            return result;
        }
    }
}

/* Replays path with solver.c's per-cubie model, independent of the factored
 * tables the search used: it must reach solved in exactly exact[rank] moves.
 */
static int verify(uint32_t rank, uint8_t length)
{
    state_t state;
    unrank_state(rank, &state);
    for (uint8_t i = 0; i < length; ++i)
        state = apply_move(state, path[i]);
    return rank_state(&state) == 0 && length == exact[rank];
}

/* The 14-digit input format, the inverse of parse_state. */
static void format_state(uint32_t rank, char out[15])
{
    state_t state;
    unrank_state(rank, &state);
    for (int i = 0; i < CUBIES; ++i) {
        out[i] = (char) ('1' + state.p[i]);
        out[i + CUBIES] = (char) ('1' + state.o[i]);
    }
    out[14] = '\0';
}

static int report_tables(void)
{
    const char *names[2] = {"permutation", "orientation"};
    const uint8_t *tables[2] = {perm_dist, ori_dist};
    const uint16_t sizes[2] = {PERMUTATIONS, ORIENTATIONS};
    int ok = 1;
    puts("== Projection tables (H2) ==");
    for (int k = 0; k < 2; ++k) {
        unsigned count[MAX_DEPTH] = {0};
        uint8_t max = 0;
        int full = 1;
        for (uint16_t i = 0; i < sizes[k]; ++i) {
            uint8_t d = tables[k][i];
            if (d == UINT8_MAX) {
                full = 0;
                continue;
            }
            ++count[d];
            if (d > max)
                max = d;
        }
        printf("%s: %u entries, max %u, solved entry %u, %s\n", names[k],
               sizes[k], max, tables[k][0],
               full ? "fully populated" : "NOT fully populated");
        printf("  distance:count");
        for (uint8_t d = 0; d <= max; ++d)
            printf("  %u:%u", d, count[d]);
        putchar('\n');
        ok = ok && full && tables[k][0] == 0;
    }
    printf("static data: turn tables %zu + %zu bytes, distance tables "
           "%zu + %zu bytes, total %zu of 131072\n\n",
           sizeof perm_turn, sizeof ori_turn, sizeof perm_dist,
           sizeof ori_dist,
           sizeof perm_turn + sizeof ori_turn + sizeof perm_dist +
               sizeof ori_dist);
    return ok;
}

static int report_admissibility(void)
{
    unsigned long long count[MAX_DEPTH] = {0}, h_sum[MAX_DEPTH] = {0};
    uint8_t h_min[MAX_DEPTH], h_max[MAX_DEPTH] = {0};
    unsigned long long violations = 0;
    memset(h_min, UINT8_MAX, sizeof h_min);
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint8_t d = exact[rank];
        uint8_t h = heuristic((uint16_t) (rank / ORIENTATIONS),
                              (uint16_t) (rank % ORIENTATIONS));
        if (h > d)
            ++violations;
        ++count[d];
        h_sum[d] += h;
        if (h < h_min[d])
            h_min[d] = h;
        if (h > h_max[d])
            h_max[d] = h;
    }
    puts("== Heuristic against exact distance (H1) ==");
    printf("exact BFS: %u states, diameter %u\n", (unsigned) STATES, diameter);
    puts("   d     states   min h   mean h   max h");
    for (uint8_t d = 0; d <= diameter; ++d)
        printf("  %2u  %9llu   %5u   %6.3f   %5u\n", d, count[d], h_min[d],
               (double) h_sum[d] / (double) count[d], h_max[d]);
    printf("states with h > d: %llu\n\n", violations);
    return violations == 0;
}

/* Korf, Reid and Edelkamp (2001): the iteration with bound B expands about
 * sum over i of N_i * P(B - i), where N_i counts depth-i nodes of the
 * unpruned tree and P(x) is the fraction of all states with h <= x.
 */
static void report_estimate(void)
{
    unsigned long long h_count[MAX_DEPTH] = {0};
    double at_most[MAX_DEPTH], tree[MAX_DEPTH], total = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank)
        ++h_count[heuristic((uint16_t) (rank / ORIENTATIONS),
                            (uint16_t) (rank % ORIENTATIONS))];
    puts("== Estimated IDA* cost (Korf, Reid, Edelkamp 2001) ==");
    puts("   x   P(h <= x)    N_x (unpruned tree, 6 faces after the first)");
    for (uint8_t x = 0; x <= diameter; ++x) {
        unsigned long long below = 0;
        for (uint8_t h = 0; h <= x; ++h)
            below += h_count[h];
        at_most[x] = (double) below / STATES;
        tree[x] = x == 0 ? 1 : x == 1 ? 9 : tree[x - 1] * 6;
        printf("  %2u   %9.7f   %13.0f\n", x, at_most[x], tree[x]);
    }
    puts("   B   expanded at bound B   cumulative up to B");
    for (uint8_t bound = 0; bound <= diameter; ++bound) {
        double estimate = 0;
        for (uint8_t i = 0; i <= bound; ++i)
            estimate += tree[i] * at_most[bound - i];
        total += estimate;
        printf("  %2u   %19.0f   %18.0f\n", bound, estimate, total);
    }
    puts("budget: 5e7 instructions / ~200 per node = ~250000 expansions\n");
}

static int report_one(uint32_t rank)
{
    char text[15];
    format_state(rank, text);
    printf("== IDA* on %s (exact distance %u) ==\n", text, exact[rank]);
    result_t result = solve(rank, 1);
    printf("  total:    generated %12llu, expanded %12llu\n",
           result.generated, result.expanded);
    printf("  solution (%u moves):", result.length);
    for (uint8_t i = 0; i < result.length; ++i)
        printf(" %s", move_names[path[i]]);
    int ok = verify(rank, result.length);
    printf("\n  replayed with apply_move: %s\n",
           ok ? "solved, optimal" : "FAILED");
    return ok;
}

/* Runs IDA* on every state at distance only (or on all states when only is
 * UINT8_MAX) and checks each solution against the exact distance.
 */
static int report_many(uint8_t only)
{
    unsigned long long states = 0, failures = 0, sum = 0;
    unsigned long long min = ~0ULL, max = 0, max_generated = 0;
    uint32_t min_rank = 0, max_rank = 0;
    char text[15];
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        if (only != UINT8_MAX && exact[rank] != only)
            continue;
        result_t result = solve(rank, 0);
        if (!verify(rank, result.length))
            ++failures;
        ++states;
        sum += result.expanded;
        if (result.expanded < min) {
            min = result.expanded;
            min_rank = rank;
        }
        if (result.expanded > max) {
            max = result.expanded;
            max_rank = rank;
        }
        if (result.generated > max_generated)
            max_generated = result.generated;
        if (only == UINT8_MAX && rank % 200000 == 0)
            fprintf(stderr, "  %u / %u states\n", rank, (unsigned) STATES);
    }
    if (only == UINT8_MAX)
        puts("== IDA* over all states (H3) ==");
    else
        printf("== IDA* over all distance-%u states ==\n", only);
    printf("states %llu, not optimal or not solved %llu\n", states, failures);
    format_state(min_rank, text);
    printf("expanded per state: min %llu (%s)\n", min, text);
    printf("                    mean %.1f\n", (double) sum / (double) states);
    format_state(max_rank, text);
    printf("                    max %llu (%s)\n", max, text);
    printf("generated per state: max %llu\n", max_generated);
    return failures == 0;
}

int main(int argc, char **argv)
{
    state_t state;
    build_turn_tables();
    bfs_projection(&perm_turn[0][0], PERMUTATIONS, perm_dist);
    bfs_projection(&ori_turn[0][0], ORIENTATIONS, ori_dist);
    if (!build_exact()) {
        fputs("exact BFS failed\n", stderr);
        return 1;
    }
    int ok;
    if (argc == 1) {
        ok = report_tables();
        ok = report_admissibility() && ok;
        report_estimate();
        parse_state("21345671111111", &state);
        ok = report_one(rank_state(&state)) && ok;
    } else if (argc == 2 && !strcmp(argv[1], "--depth11")) {
        ok = report_many(11);
    } else if (argc == 2 && !strcmp(argv[1], "--all")) {
        ok = report_many(UINT8_MAX);
    } else if (argc == 2 && parse_state(argv[1], &state)) {
        ok = report_one(rank_state(&state));
    } else {
        fprintf(stderr, "usage: %s [PPPPPPPOOOOOOO | --depth11 | --all]\n",
                argv[0]);
        return 2;
    }
    free(exact);
    return ok ? 0 : 1;
}
