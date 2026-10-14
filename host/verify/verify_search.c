/* Search check: gate H3. Runs the search in host/oracle/ida.c on every state
 * and checks that each solution is exactly as long as the state's distance,
 * not merely a solution that works. See host/verify/README.verify_search.md.
 *
 * The reference is solver.c, included whole: the exact distances come from
 * its BFS table, and each solution is replayed with its per-cubie model. The
 * search is included whole as well, so the code checked is the very code the
 * target is translated from.
 *
 * Build and run from the repository root, after generating tables.h:
 *   cc -O2 -std=c99 -Wall -Wextra -o host/verify/verify_search \
 *       host/verify/verify_search.c
 *   ./host/verify/verify_search             H3: all 3,674,160 states, minutes
 *   ./host/verify/verify_search --depth 11  only the states at one distance
 */

#define main solver_main
#include "../../solver.c"
#undef main

/* The search under test. host/test/run.sh points this at copies of ida.c
 * broken on purpose, to check that this gate catches them.
 */
#ifndef IDA_SOURCE
#define IDA_SOURCE "../oracle/ida.c"
#endif
#define main ida_main
#include IDA_SOURCE
#undef main

#include <stdarg.h>
#include <time.h>

static unsigned failed;

/* Prints one check as ok or FAIL and counts the failures. */
static int check(int ok, const char *format, ...)
{
    va_list args;
    printf("  %-4s  ", ok ? "ok" : "FAIL");
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
    failed += !ok;
    return ok;
}

/* ==== The reference: solver.c ==== */

/* Exact distance to solved of every state, indexed by rank. */
static uint8_t *exact;
static uint8_t diameter;

/* solver.c's BFS table holds, for each state, the move that takes it one
 * step closer to solved, so following those moves and counting gives the
 * distance. Each state is walked once: a chain stops at the first state whose
 * distance is known, then fills in the distances on the way back. Returns 0
 * if the table cannot be built or a chain does not reach solved within the
 * diameter. The same as in verify_tables.c.
 */
static int build_exact(void)
{
    uint32_t chain[UINT8_MAX + 1];
    uint8_t *toward_solved = build_table(&diameter);
    if (!toward_solved)
        return 0;
    exact = malloc(STATES);
    if (!exact) {
        free(toward_solved);
        return 0;
    }
    memset(exact, UINT8_MAX, STATES);
    exact[0] = 0;
    for (uint32_t rank = 1; rank < STATES; ++rank) {
        uint32_t length = 0, here = rank;
        while (exact[here] == UINT8_MAX && length <= diameter) {
            state_t state;
            chain[length++] = here;
            unrank_state(here, &state);
            state = apply_move(state, toward_solved[here]);
            here = rank_state(&state);
        }
        uint32_t d = exact[here];
        if (d == UINT8_MAX || d + length > diameter) {
            free(toward_solved);
            return 0;
        }
        while (length)
            exact[chain[--length]] = (uint8_t) ++d;
    }
    free(toward_solved);
    return 1;
}

/* The 14-digit form of a state, to report a failure in the form ida takes. */
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

/* ==== Walking the states ==== */

/* The inputs read_state must reject: the invalid states that the Makefile's
 * check target feeds solver and mini, one per rejection path.
 */
static const char *const invalid_states[] = {
    "1234567111111",  "123456711111111", "02345671111111",
    "82345671111111", "12345671111110",  "12345671111114",
    "1234567111111a", "11345671111111",  "12345671111112",
};

/* Replays the moves in stack[0..length-1] on the state with solver.c's
 * per-cubie model, apart from the tables the search used. Returns 1 if they
 * end at solved.
 */
static int replays_to_solved(uint32_t rank, unsigned length)
{
    state_t state;
    unrank_state(rank, &state);
    for (unsigned g = 0; g < length; ++g)
        state = apply_move(state,
                           (uint8_t) (stack[g].face * 3U + stack[g].turn));
    return rank_state(&state) == 0;
}

/* One counter per check, filled by walk_states. */
static struct {
    unsigned long searched, misread, unsolved, wrong, unreplayed, rejected,
        accepted;
    unsigned long count[MAX_DEPTH + 1];
    uint32_t first_bad;
    double seconds;
} tally;

/* Takes every state, or only those at distance only when only >= 0, the way
 * ida's main does: its 14 digits through read_state, then solve, then
 * check_path. The states are visited p by p and o by o, so rank is a running
 * count and (p, o) is what read_state should give.
 */
static void walk_states(int only)
{
    uint32_t rank = 0;
    time_t start = time(NULL);
    tally.first_bad = UINT32_MAX;
    for (unsigned p = 0; p < PERMUTATIONS; ++p)
        for (unsigned o = 0; o < ORIENTATIONS; ++o, ++rank) {
            if (only >= 0 && exact[rank] != only)
                continue;
            if (only < 0 && rank % 200000 == 0)
                fprintf(stderr, "  %u / %u states\n", rank, (unsigned) STATES);
            char text[15];
            unsigned read_p = p, read_o = o;
            int bad = 0;
            ++tally.searched;
            format_state(rank, text);
            if (!read_state(text, &read_p, &read_o) || read_p != p ||
                read_o != o) {
                ++tally.misread;
                bad = 1;
            }
            int length = solve(read_p, read_o);
            if (length < 0) {
                ++tally.unsolved;
                bad = 1;
            } else {
                if ((unsigned) length != exact[rank]) {
                    ++tally.wrong;
                    bad = 1;
                }
                if (!replays_to_solved(rank, length)) {
                    ++tally.unreplayed;
                    bad = 1;
                }
                if (!check_path(read_p, read_o, length)) {
                    ++tally.rejected;
                    bad = 1;
                }
                /* The next turn of the same face acts differently on every
                 * state, so this path cannot end at solved.
                 */
                if (length > 0) {
                    unsigned turn = stack[0].turn;
                    stack[0].turn = turn == 2 ? 0 : turn + 1;
                    if (check_path(read_p, read_o, length)) {
                        ++tally.accepted;
                        bad = 1;
                    }
                    stack[0].turn = turn;
                }
                ++tally.count[length];
            }
            if (bad && tally.first_bad == UINT32_MAX)
                tally.first_bad = rank;
        }
    tally.seconds = difftime(time(NULL), start);
}

/* ==== read_state: the 14 digits to p and o ==== */

static int check_read_state(void)
{
    unsigned before = failed, accepted = 0;
    for (unsigned i = 0; i < sizeof invalid_states / sizeof *invalid_states;
         ++i) {
        unsigned p, o;
        accepted += read_state(invalid_states[i], &p, &o);
    }
    check(!tally.misread, "every state's digits give its p and o (%lu differ)",
          tally.misread);
    check(!accepted, "the %u invalid inputs from the Makefile are rejected "
          "(%u accepted)",
          (unsigned) (sizeof invalid_states / sizeof *invalid_states),
          accepted);
    return failed == before;
}

/* ==== H3: every solution as long as the exact distance ==== */

static int check_h3(void)
{
    unsigned before = failed;
    check(!tally.unsolved, "%lu states searched, %lu with no solution",
          tally.searched, tally.unsolved);
    check(!tally.wrong, "length equals the exact distance (%lu differ)",
          tally.wrong);
    check(!tally.unreplayed,
          "replayed with solver.c's model, reaches solved (%lu do not)",
          tally.unreplayed);
    printf("        length:count");
    for (unsigned d = 0; d <= MAX_DEPTH; ++d)
        if (tally.count[d])
            printf("  %u:%lu", d, tally.count[d]);
    printf("\n        wall-clock: %.0f s for the walk over the states\n",
           tally.seconds);
    return failed == before;
}

/* ==== T5 check: ida's check_path ==== */

static int check_t5(void)
{
    unsigned before = failed;
    check(!tally.rejected, "accepts every solution (%lu rejected)",
          tally.rejected);
    check(!tally.accepted,
          "rejects every solution with its first move changed (%lu accepted)",
          tally.accepted);
    return failed == before;
}

int main(int argc, char **argv)
{
    int only = -1;
    if (argc == 3 && !strcmp(argv[1], "--depth")) {
        char *end;
        long d = strtol(argv[2], &end, 10);
        if (argv[2][0] < '0' || argv[2][0] > '9' || *end != '\0' ||
            d > MAX_DEPTH) {
            fprintf(stderr, "%s: --depth takes 0 to %d\n", argv[0],
                    MAX_DEPTH);
            return 2;
        }
        only = (int) d;
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [--depth N]\n", argv[0]);
        return 2;
    }

    puts("== Reference: solver.c ==");
    if (!check(self_test(), "self-test")) {
        puts("\nreference FAILED, the gates were not run");
        return 1;
    }
    int built = build_exact();
    if (!check(built, "BFS table reaches all %u states, diameter %u",
               (unsigned) STATES, diameter)) {
        puts("\nreference FAILED, the gates were not run");
        return 1;
    }

    walk_states(only);
    if (only >= 0)
        printf("\nOnly the %lu states at distance %d were walked.\n",
               tally.searched, only);
    puts("\n== read_state: the 14 digits to p and o ==");
    int read_ok = check_read_state();
    puts("\n== H3: every solution as long as the exact distance ==");
    int h3_ok = check_h3();
    puts("\n== T5 check: ida's check_path ==");
    int t5_ok = check_t5();
    if (tally.first_bad != UINT32_MAX) {
        char text[15];
        format_state(tally.first_bad, text);
        printf("\nfirst failure: %s, exact distance %u\n", text,
               exact[tally.first_bad]);
    }

    if (only >= 0)
        printf("\ndistance %d only: ", only);
    else
        putchar('\n');
    printf("H3 %s, read_state %s, T5 check %s\n", h3_ok ? "PASS" : "FAIL",
           read_ok ? "PASS" : "FAIL", t5_ok ? "PASS" : "FAIL");
    free(exact);
    return failed ? 1 : 0;
}
