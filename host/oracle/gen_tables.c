/* Builds the four search tables from the per-cubie cube model and writes
 * them out, as a C header for host programs or as assembler data for the
 * target, so a solver can link them in as read-only data instead of building
 * them at run time. See host/oracle/README.gen_tables.md.
 *
 * It depends on nothing else in the repository. The cube model and the table
 * code are taken from host/ida_draft/ida_test.c, which stays as it is. It
 * does not check the tables: host/verify/verify_tables.c checks the files
 * written here.
 *
 * Build and run from the repository root:
 *   cc -O2 -std=c99 -Wall -Wextra -o host/oracle/gen_tables \
 *       host/oracle/gen_tables.c
 *   ./host/oracle/gen_tables > host/oracle/tables.h
 *   ./host/oracle/gen_tables --asm > host/oracle/tables.s
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==== Cube model: per-cubie state, quarter turns, ranking ==== */

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

/* ==== Output ==== */

/* Prints a [rows][cols] table, or a [cols] one when rows is 1, as a C array
 * definition. wide selects uint16_t data over uint8_t.
 */
static void emit(const char *name, const void *data, int wide, unsigned rows,
                 unsigned cols)
{
    unsigned per_line = wide ? 12 : 24;
    const char *indent = rows > 1 ? "        " : "    ";
    printf("static const %s %s", wide ? "uint16_t" : "uint8_t", name);
    if (rows > 1)
        printf("[%u]", rows);
    printf("[%u] = {\n", cols);
    for (unsigned r = 0; r < rows; ++r) {
        if (rows > 1)
            puts("    {");
        for (unsigned c = 0; c < cols; ++c) {
            unsigned i = r * cols + c;
            unsigned value = wide ? ((const uint16_t *) data)[i]
                                  : ((const uint8_t *) data)[i];
            if (c % per_line == 0)
                fputs(indent, stdout);
            printf("%u,", value);
            putchar(c % per_line == per_line - 1 || c + 1 == cols ? '\n'
                                                                  : ' ');
        }
        if (rows > 1)
            puts("    },");
    }
    puts("};\n");
}

/* Prints a [rows][cols] table as assembler data: a label, then .half lines
 * (wide) or .byte lines in memory order, with a comment before each row when
 * there is more than one.
 */
static void emit_asm(const char *name, const void *data, int wide,
                     unsigned rows, unsigned cols)
{
    unsigned per_line = wide ? 12 : 24;
    printf("%s:\n", name);
    for (unsigned r = 0; r < rows; ++r) {
        if (rows > 1)
            printf("    # face %u (%c)\n", r, "RBD"[r]);
        for (unsigned c = 0; c < cols; ++c) {
            unsigned i = r * cols + c;
            unsigned value = wide ? ((const uint16_t *) data)[i]
                                  : ((const uint8_t *) data)[i];
            if (c % per_line == 0)
                printf("    %s ", wide ? ".half" : ".byte");
            printf("%u", value);
            fputs(c % per_line == per_line - 1 || c + 1 == cols ? "\n" : ", ",
                  stdout);
        }
    }
    putchar('\n');
}

static void write_header(void)
{
    puts("/* Generated by host/oracle/gen_tables.c. Do not edit.\n"
         " *\n"
         " * perm_turn[f][p], ori_turn[f][o]: one quarter turn of face f, "
         "0 R, 1 B, 2 D.\n"
         " * perm_dist[p], ori_dist[o]: moves to solved in each "
         "projection.");
    printf(" * Static data: %zu + %zu + %zu + %zu = %zu bytes.\n */\n\n",
           sizeof perm_turn, sizeof ori_turn, sizeof perm_dist,
           sizeof ori_dist,
           sizeof perm_turn + sizeof ori_turn + sizeof perm_dist +
               sizeof ori_dist);
    puts("#ifndef TABLES_H\n#define TABLES_H\n\n#include <stdint.h>\n");
    emit("perm_turn", perm_turn, 1, 3, PERMUTATIONS);
    emit("ori_turn", ori_turn, 1, 3, ORIENTATIONS);
    emit("perm_dist", perm_dist, 0, 1, PERMUTATIONS);
    emit("ori_dist", ori_dist, 0, 1, ORIENTATIONS);
    puts("#endif");
}

/* The .half tables come first, so they stay 2-byte aligned as long as the
 * file starts on an even address.
 */
static void write_asm(void)
{
    puts("# Generated by host/oracle/gen_tables.c --asm. Do not edit.\n"
         "#\n"
         "# perm_turn[f][p], ori_turn[f][o]: one quarter turn of face f, "
         "0 R, 1 B, 2 D,\n"
         "# stored face by face. perm_dist[p], ori_dist[o]: moves to solved "
         "in each\n"
         "# projection. The .half tables come first: start this file on an "
         "even address.");
    printf("# Static data: %zu + %zu + %zu + %zu = %zu bytes.\n\n",
           sizeof perm_turn, sizeof ori_turn, sizeof perm_dist,
           sizeof ori_dist,
           sizeof perm_turn + sizeof ori_turn + sizeof perm_dist +
               sizeof ori_dist);
    puts("    .data\n");
    emit_asm("perm_turn", perm_turn, 1, 3, PERMUTATIONS);
    emit_asm("ori_turn", ori_turn, 1, 3, ORIENTATIONS);
    emit_asm("perm_dist", perm_dist, 0, 1, PERMUTATIONS);
    emit_asm("ori_dist", ori_dist, 0, 1, ORIENTATIONS);
}

int main(int argc, char **argv)
{
    int as_asm = argc == 2 && !strcmp(argv[1], "--asm");
    if (argc > 2 || (argc == 2 && !as_asm)) {
        fprintf(stderr, "usage: %s [--asm]\n", argv[0]);
        return 2;
    }
    build_tables();
    if (as_asm)
        write_asm();
    else
        write_header();
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fputs("could not write the tables\n", stderr);
        return 1;
    }
    return 0;
}
