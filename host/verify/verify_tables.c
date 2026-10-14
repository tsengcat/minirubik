/* Table check: gates H2, H4 and H1 on the tables a solver links in, read
 * back from the generated assembly file. See
 * host/verify/README.verify_tables.md.
 *
 * The reference is solver.c, included whole: its cube model, and its BFS
 * table, from which the exact distance of every state is derived. Nothing
 * here comes from host/oracle/gen_tables.c, the program under test; only its
 * output is read.
 *
 * Build and run from the repository root, after generating the tables:
 *   ./host/oracle/gen_tables > host/oracle/tables.h
 *   ./host/oracle/gen_tables --asm > host/oracle/tables.s
 *   cc -O2 -std=c99 -Wall -Wextra -o host/verify/verify_tables \
 *       host/verify/verify_tables.c
 *   ./host/verify/verify_tables host/oracle/tables.s
 */

#define main solver_main
#include "../../solver.c"
#undef main

#include <stdarg.h>

#include "../oracle/tables.h"

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

/* ==== Reading the assembly file ==== */

enum { MAX_TABLES = 8, MAX_ENTRIES = 3 * PERMUTATIONS, MAX_LINE = 1024 };

/* One labelled run of .half or .byte data, as the assembler lays it out. */
typedef struct {
    char name[32];
    unsigned width;  /* 2 for .half, 1 for .byte, 0 before any data */
    uint32_t offset; /* bytes from the start of the file's data */
    uint32_t count;
    uint16_t value[MAX_ENTRIES];
} table_t;

static table_t tables[MAX_TABLES];
static unsigned table_count;
static uint32_t data_bytes;

static int blank(char c)
{
    return c == ' ' || c == '\t';
}

static int parse_error(const char *path, unsigned line, const char *message)
{
    fprintf(stderr, "%s:%u: %s\n", path, line, message);
    return 0;
}

/* Reads the subset of assembler syntax that gen_tables --asm writes: '#'
 * comments, a .data directive, labels, and .half or .byte lines of decimal
 * values. Anything else is rejected, so no data the assembler would place
 * goes unread. Returns 1 on success.
 */
static int read_asm(const char *path)
{
    FILE *file = fopen(path, "r");
    char text[MAX_LINE];
    unsigned line = 0;
    int in_data = 0, ok = 1;
    if (!file) {
        fprintf(stderr, "%s: cannot open\n", path);
        return 0;
    }
    while (ok && fgets(text, sizeof text, file)) {
        ++line;
        if (!strchr(text, '\n') && !feof(file)) {
            ok = parse_error(path, line, "line too long");
            break;
        }
        char *hash = strchr(text, '#');
        if (hash)
            *hash = '\0';
        char *s = text;
        while (blank(*s))
            ++s;
        size_t length = strlen(s);
        while (length && (blank(s[length - 1]) || s[length - 1] == '\n'))
            s[--length] = '\0';
        if (!length)
            continue;
        if (!strcmp(s, ".data")) {
            in_data = 1;
            continue;
        }
        if (!in_data) {
            ok = parse_error(path, line, "data before .data");
            break;
        }
        if (s[length - 1] == ':') {
            if (table_count == MAX_TABLES || length > sizeof tables[0].name) {
                ok = parse_error(path, line, "too many or too long labels");
                break;
            }
            s[length - 1] = '\0';
            for (unsigned i = 0; i < table_count; ++i)
                if (!strcmp(tables[i].name, s))
                    ok = parse_error(path, line, "label defined twice");
            table_t *table = &tables[table_count++];
            strcpy(table->name, s);
            table->offset = data_bytes;
            continue;
        }
        unsigned width;
        if (!strncmp(s, ".half", 5) && blank(s[5]))
            width = 2;
        else if (!strncmp(s, ".byte", 5) && blank(s[5]))
            width = 1;
        else {
            ok = parse_error(path, line, "not a label, .data, .half or .byte");
            break;
        }
        if (!table_count) {
            ok = parse_error(path, line, "data before the first label");
            break;
        }
        table_t *table = &tables[table_count - 1];
        if (table->width && table->width != width) {
            ok = parse_error(path, line, "a table mixes .half and .byte");
            break;
        }
        table->width = width;
        for (char *p = s + 5;;) {
            while (blank(*p))
                ++p;
            if (*p < '0' || *p > '9') {
                ok = parse_error(path, line, "expected a decimal value");
                break;
            }
            char *end;
            unsigned long value = strtoul(p, &end, 10);
            if (value > (unsigned long) (width == 2 ? UINT16_MAX : UINT8_MAX) ||
                table->count == MAX_ENTRIES) {
                ok = parse_error(path, line, "value or table too large");
                break;
            }
            table->value[table->count++] = (uint16_t) value;
            data_bytes += width;
            for (p = end; blank(*p);)
                ++p;
            if (*p == '\0')
                break;
            if (*p++ != ',') {
                ok = parse_error(path, line, "expected ',' between values");
                break;
            }
        }
    }
    if (ferror(file))
        ok = parse_error(path, line, "read error");
    fclose(file);
    return ok;
}

static const table_t *find_table(const char *name)
{
    for (unsigned i = 0; i < table_count; ++i)
        if (!strcmp(tables[i].name, name))
            return &tables[i];
    return NULL;
}

/* ==== Layout: what a solver finds in the file ==== */

/* The four tables as read from the assembly file, the turn tables face by
 * face: entry (f, x) of a [3][size] table sits at [f * size + x]. Every
 * check after the layout runs on these.
 */
static const uint16_t *file_perm_turn, *file_ori_turn, *file_perm_dist,
    *file_ori_dist;

/* Each expected table must be present, with the right directive and entry
 * count, and match tables.h entry for entry, so host programs built on the
 * header see the same data as the target. No other table may appear: the
 * checks below would not cover it. Returns 1 if all four tables were found
 * with the right shape, so the gates can read them.
 */
static int check_layout(void)
{
    static const struct {
        const char *name;
        unsigned width;
        uint32_t count;
        const void *header;
        const uint16_t **file;
    } expected[] = {
        {"perm_turn", 2, 3 * PERMUTATIONS, perm_turn, &file_perm_turn},
        {"ori_turn", 2, 3 * ORIENTATIONS, ori_turn, &file_ori_turn},
        {"perm_dist", 1, PERMUTATIONS, perm_dist, &file_perm_dist},
        {"ori_dist", 1, ORIENTATIONS, ori_dist, &file_ori_dist},
    };
    unsigned found = 0;
    for (unsigned k = 0; k < sizeof expected / sizeof expected[0]; ++k) {
        const table_t *table = find_table(expected[k].name);
        if (!check(table != NULL, "%s: present", expected[k].name))
            continue;
        uint32_t differ = 0;
        if (!check(table->width == expected[k].width &&
                       table->count == expected[k].count,
                   "%s: %u entries of .%s", table->name, table->count,
                   table->width == 2 ? "half" : "byte"))
            continue;
        check(table->offset % table->width == 0, "%s: at byte offset %u",
              table->name, table->offset);
        for (uint32_t i = 0; i < table->count; ++i) {
            unsigned in_header =
                table->width == 2 ? ((const uint16_t *) expected[k].header)[i]
                                  : ((const uint8_t *) expected[k].header)[i];
            differ += table->value[i] != in_header;
        }
        check(!differ, "%s: same as tables.h (%u entries differ)",
              table->name, differ);
        *expected[k].file = table->value;
        ++found;
    }
    for (unsigned i = 0; i < table_count; ++i) {
        int known = 0;
        for (unsigned k = 0; k < sizeof expected / sizeof expected[0]; ++k)
            known |= !strcmp(tables[i].name, expected[k].name);
        if (!known)
            check(0, "%s: a table these checks do not cover", tables[i].name);
    }
    check(data_bytes <= 131072, "static data in the file: %u of 131072 bytes",
          data_bytes);
    return found == sizeof expected / sizeof expected[0];
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
 * diameter.
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

/* ==== H2: every table populated, its maximum and solved entry verified ==== */

/* Each face of a [3][size] turn table must be a permutation of 0..size-1:
 * no entry left unset or out of range, and so a maximum of size - 1.
 */
static int is_permutation(const uint16_t *turn, uint16_t size)
{
    static uint8_t seen[PERMUTATIONS];
    for (uint8_t face = 0; face < 3; ++face) {
        memset(seen, 0, size);
        for (uint16_t x = 0; x < size; ++x) {
            uint16_t next = turn[face * size + x];
            if (next >= size || seen[next])
                return 0;
            seen[next] = 1;
        }
    }
    return 1;
}

/* On every state and face, the two factored lookups must give the rank that
 * solver.c's model gives. This also pins down the solved entries turn[f][0].
 */
static uint32_t turn_mismatches(void)
{
    uint32_t bad = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint16_t p = (uint16_t) (rank / ORIENTATIONS);
        uint16_t o = (uint16_t) (rank % ORIENTATIONS);
        state_t state;
        unrank_state(rank, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            uint32_t got =
                (uint32_t) file_perm_turn[face * PERMUTATIONS + p] *
                    ORIENTATIONS +
                file_ori_turn[face * ORIENTATIONS + o];
            bad += got != rank_state(&next);
        }
    }
    return bad;
}

/* A move changes p and o independently, so the distance in a projection is
 * the least exact distance over the states that share that p (or that o):
 * a shortest path in the projection lifts to a path that ends at solved for
 * one of them, and no path is shorter than its projection. Checking every
 * entry against this also checks the maximum and the solved entry.
 */
static void check_dist(const char *name, const uint16_t *dist,
                       const uint8_t *reference, uint16_t size)
{
    uint32_t unset = 0, differ = 0;
    unsigned max = 0, reference_max = 0;
    for (uint16_t i = 0; i < size; ++i) {
        unset += dist[i] == UINT8_MAX;
        differ += dist[i] != reference[i];
        if (dist[i] != UINT8_MAX && dist[i] > max)
            max = dist[i];
        if (reference[i] > reference_max)
            reference_max = reference[i];
    }
    check(!unset, "%s: fully populated (%u entries unset)", name, unset);
    check(dist[0] == 0, "%s: solved entry %u", name, dist[0]);
    check(max == reference_max, "%s: max %u, reference max %u", name, max,
          reference_max);
    check(!differ, "%s: every entry equals the reference (%u differ)", name,
          differ);
}

static int check_h2(void)
{
    static uint8_t perm_min[PERMUTATIONS], ori_min[ORIENTATIONS];
    unsigned before = failed;
    check(is_permutation(file_perm_turn, PERMUTATIONS),
          "perm_turn: each face a permutation of 0..%u, so max %u",
          PERMUTATIONS - 1, PERMUTATIONS - 1);
    check(is_permutation(file_ori_turn, ORIENTATIONS),
          "ori_turn: each face a permutation of 0..%u, so max %u",
          ORIENTATIONS - 1, ORIENTATIONS - 1);
    uint32_t bad = turn_mismatches();
    check(!bad,
          "perm_turn, ori_turn: agree with solver.c on %u states x 3 faces "
          "(%u differ)",
          (unsigned) STATES, bad);
    printf("        solved entries perm_turn[f][0] %u %u %u, "
           "ori_turn[f][0] %u %u %u\n",
           file_perm_turn[0], file_perm_turn[PERMUTATIONS],
           file_perm_turn[2 * PERMUTATIONS], file_ori_turn[0],
           file_ori_turn[ORIENTATIONS], file_ori_turn[2 * ORIENTATIONS]);

    memset(perm_min, UINT8_MAX, sizeof perm_min);
    memset(ori_min, UINT8_MAX, sizeof ori_min);
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint16_t p = (uint16_t) (rank / ORIENTATIONS);
        uint16_t o = (uint16_t) (rank % ORIENTATIONS);
        if (exact[rank] < perm_min[p])
            perm_min[p] = exact[rank];
        if (exact[rank] < ori_min[o])
            ori_min[o] = exact[rank];
    }
    check_dist("perm_dist", file_perm_dist, perm_min, PERMUTATIONS);
    check_dist("ori_dist", file_ori_dist, ori_min, ORIENTATIONS);
    return failed == before;
}

/* ==== H1: the heuristic never overestimates ==== */

/* A stand-in for the search's heuristic until the search exists. It reads
 * the tables from the file the way the search will; replace it with the
 * search's own function then.
 */
static uint8_t heuristic(uint16_t p, uint16_t o)
{
    uint8_t a = (uint8_t) file_perm_dist[p], b = (uint8_t) file_ori_dist[o];
    return a > b ? a : b;
}

static int check_h1(void)
{
    uint32_t over = 0;
    uint8_t max = 0;
    unsigned before = failed;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint8_t h = heuristic((uint16_t) (rank / ORIENTATIONS),
                              (uint16_t) (rank % ORIENTATIONS));
        over += h > exact[rank];
        if (h > max)
            max = h;
    }
    check(!over, "h <= d on all %u states (%u with h > d), max h %u",
          (unsigned) STATES, over, max);
    return failed == before;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s TABLES.s\n", argv[0]);
        return 2;
    }
    if (!read_asm(argv[1]))
        return 1;

    printf("== Layout of %s ==\n", argv[1]);
    if (!check_layout()) {
        puts("\na table is missing or misshapen, the gates were not run");
        return 1;
    }
    int layout = !failed;

    puts("\n== Reference: solver.c ==");
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

    puts("\n== H2: tables populated, maximum and solved entry ==");
    int h2 = check_h2();
    puts("\n== H4: packed accessors ==");
    puts("  n/a   every table is one entry per .half or .byte; none is "
         "packed");
    puts("\n== H1: heuristic admissible ==");
    int h1 = check_h1();

    printf("\nlayout %s, H2 %s, H4 n/a, H1 %s\n", layout ? "PASS" : "FAIL",
           h2 ? "PASS" : "FAIL", h1 ? "PASS" : "FAIL");
    free(exact);
    return failed ? 1 : 0;
}
