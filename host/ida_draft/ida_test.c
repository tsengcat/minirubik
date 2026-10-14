/* IDA* test: tries the stage 2 design on the host before any of it is
 * written for the target. See host/ida_draft/README.md.
 *
 * The design is IDA* with h = max(perm_dist[p], ori_dist[o]), where
 * perm_dist and ori_dist are the distances to solved over the 5,040 corner
 * permutations and over the 729 orientations. It checks h against
 * the exact distance of every state, checks that IDA* returns a shortest
 * solution, and counts the nodes IDA* visits.
 *
 * Build and run from the repository root:
 *   cc -O2 -std=c99 -Wall -Wextra -o host/ida_draft/ida_test \
 *       host/ida_draft/ida_test.c
 *   ./host/ida_draft/ida_test  tables, H1/H2, estimate, sample state
 *   ./host/ida_draft/ida_test 21345671111111  solve one state, per-bound counts
 *   ./host/ida_draft/ida_test --depth11  IDA* over all 2,644 distance-11 states
 *   ./host/ida_draft/ida_test --all  H3: IDA* over all 3,674,160 states
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==== Cube model: per-cubie state, moves, ranking ==== */

enum {
    CUBIES = 7,
    PERMUTATIONS = 5040,
    ORIENTATIONS = 729,
    STATES = PERMUTATIONS * ORIENTATIONS,
    MOVES = 9
};

typedef struct {
    uint8_t p[CUBIES], o[CUBIES];
} state_t;

static const char *const move_names[MOVES] = {"R",  "R2", "R'", "B", "B2",
                                              "B'", "D",  "D2", "D'"};

/* Each destination takes a cubie from source[face][destination]. */
static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};
static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};

/* The three quarter-turns preserve the fixed front-upper-left corner. */
static state_t quarter_turn(state_t state, uint8_t face)
{
    state_t result;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t from = source[face][i];
        result.p[i] = state.p[from];
        result.o[i] = (uint8_t) ((state.o[from] + twist[face][i]) % 3U);
    }
    return result;
}

static state_t apply_move(state_t state, uint8_t move)
{
    uint8_t turns = (uint8_t) (move % 3U + 1U);
    for (uint8_t i = 0; i < turns; ++i)
        state = quarter_turn(state, (uint8_t) (move / 3U));
    return state;
}

static uint32_t rank_state(const state_t *state)
{
    uint32_t p = 0, o = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t smaller = 0;
        for (uint8_t j = (uint8_t) (i + 1U); j < CUBIES; ++j)
            if (state->p[j] < state->p[i])
                ++smaller;
        p = p * (CUBIES - i) + smaller;
    }
    for (uint8_t i = 0; i < 6; ++i)
        o = o * 3U + state->o[i];
    return p * ORIENTATIONS + o;
}

static void unrank_state(uint32_t rank, state_t *state)
{
    uint8_t available[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    uint32_t p = rank / ORIENTATIONS, o = rank % ORIENTATIONS, f = 720;
    uint8_t sum = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t q = (uint8_t) (p / f);
        p %= f;
        state->p[i] = available[q];
        for (uint8_t j = q; j + 1U < CUBIES - i; ++j)
            available[j] = available[j + 1U];
        if (i < 5)
            f /= 6U - i;
    }
    for (uint8_t i = 6; i-- > 0;) {
        state->o[i] = (uint8_t) (o % 3U);
        sum = (uint8_t) (sum + state->o[i]);
        o /= 3U;
    }
    state->o[6] = (uint8_t) ((3U - sum % 3U) % 3U);
}

static int valid(const state_t *state)
{
    uint8_t sum = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        if (state->p[i] >= CUBIES || state->o[i] >= 3)
            return 0;
        for (uint8_t j = 0; j < i; ++j)
            if (state->p[j] == state->p[i])
                return 0;
        sum = (uint8_t) (sum + state->o[i]);
    }
    return sum % 3U == 0;
}

static int parse_state(const char *input, state_t *state)
{
    for (int i = 0; i < 14; ++i) {
        int limit = i < 7 ? 7 : 3;
        if (input[i] < '1' || input[i] > '0' + limit)
            return 0;
        (i < 7 ? state->p : state->o)[i % 7] = (uint8_t) (input[i] - '1');
    }
    return input[14] == '\0' && valid(state);
}

/* ==== The four search tables ==== */

/* Quarter-turn transitions, one table per projection. */
static uint16_t perm_turn[3][PERMUTATIONS], ori_turn[3][ORIENTATIONS];
/* Distance to solved in each projection: the two heuristic tables. */
static uint8_t perm_dist[PERMUTATIONS], ori_dist[ORIENTATIONS];

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

/* Fills all four tables. The distance tables are built from the turn
 * tables, so the order matters.
 */
static void build_tables(void)
{
    build_turn_tables();
    bfs_projection(&perm_turn[0][0], PERMUTATIONS, perm_dist);
    bfs_projection(&ori_turn[0][0], ORIENTATIONS, ori_dist);
}

/* ==== Search and checks ==== */

enum { MAX_DEPTH = 20, NO_FACE = 3 };

/* Exact distance of every state. Host-only oracle, never on the target. */
static uint8_t *exact;
static uint8_t diameter;

static uint8_t path[MAX_DEPTH];
static unsigned long long generated, expanded;

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

/* Replays path with the per-cubie model, independent of the factored tables
 * the search used: it must reach solved in exactly exact[rank] moves.
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
    build_tables();
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
