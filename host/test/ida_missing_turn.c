/* BROKEN ON PURPOSE, for host/test/run.sh: a copy of host/oracle/ida.c in
 * which next_child never tries the third quarter turn (R', B', D').
 * verify_search must reject it. See host/test/README.md.
 */

/* IDA* query: solves one cube state using only the four search tables. It is
 * the C model of the target's assembly, which is translated from it by hand,
 * so every operation here has a direct RV32I counterpart. See
 * host/oracle/README.ida.md.
 *
 * Rules: it reads nothing but tables.h; it has no multiply, divide or modulo,
 * no recursion, no heap and no floating point; and a turn table row is
 * reached through a row pointer, never by multiplying by the row length.
 * Locals are unsigned, the width of a register; only data kept in memory is
 * uint16_t or uint8_t.
 *
 * Build and run from the repository root, after generating tables.h:
 *   cc -O2 -std=c99 -Wall -Wextra -o host/oracle/ida host/oracle/ida.c
 *   ./host/oracle/ida 21345671111111
 */

#include <stdint.h>
#include <stdio.h>

#include "../oracle/tables.h"

enum { MAX_DEPTH = 11, NO_FACE = 3 };

/* Row f of each turn table: one quarter turn of face f, 0 R, 1 B, 2 D. */
static const uint16_t *const perm_row[3] = {perm_turn[0], perm_turn[1],
                                            perm_turn[2]};
static const uint16_t *const ori_row[3] = {ori_turn[0], ori_turn[1],
                                           ori_turn[2]};

/* The search stack, one frame per depth. Frame g holds the node at depth g
 * and the move being tried from it; the child that move leads to is the
 * node of frame g + 1. After a search, frames 0..length-1 hold the moves.
 */
typedef struct {
    uint16_t p, o;      /* the node */
    uint8_t face, turn; /* the move: face 0-2, turn 0-2 for 1-3 quarter turns */
    uint8_t skip;       /* the face of the move that led here */
} frame_t;

static frame_t stack[MAX_DEPTH + 1];

/* Reads the 14-digit state, cubie digits 1-7 then orientation digits 1-3,
 * into the permutation index p and the orientation index o: the values
 * solver.c's rank_state gives as rank / 729 and rank % 729. Returns 0 if the
 * text is not a valid state.
 */
static int read_state(const char *s, unsigned *p_out, unsigned *o_out)
{
    unsigned seen = 0, sum = 0, c[6], p, o = 0;
    for (unsigned i = 0; i < 7; ++i) {
        unsigned d = s[i] - '1'; /* a '\0' wraps to a large value */
        if (d > 6 || ((seen >> d) & 1))
            return 0;
        seen |= 1u << d;
    }
    for (unsigned i = 7; i < 14; ++i) {
        unsigned d = s[i] - '1';
        if (d > 2)
            return 0;
        sum += d;
        if (sum >= 3)
            sum -= 3;
    }
    if (s[14] != '\0' || sum != 0)
        return 0;

    /* c[i]: how many cubies after position i have a smaller digit. */
    for (unsigned i = 0; i < 6; ++i) {
        c[i] = 0;
        for (unsigned j = i + 1; j < 7; ++j)
            if (s[j] < s[i])
                ++c[i];
    }
    /* p = ((((c0 * 6 + c1) * 5 + c2) * 4 + c3) * 3 + c4) * 2 + c5 */
    p = c[0];
    p = (p << 2) + (p << 1) + c[1]; /* p * 6 + c1 */
    p = (p << 2) + p + c[2];        /* p * 5 + c2 */
    p = (p << 2) + c[3];            /* p * 4 + c3 */
    p = (p << 1) + p + c[4];        /* p * 3 + c4 */
    p = (p << 1) + c[5];            /* p * 2 + c5 */

    /* o: the first six orientations in base 3; the seventh follows. */
    for (unsigned i = 7; i < 13; ++i)
        o = (o << 1) + o + (s[i] - '1'); /* o * 3 + digit */

    *p_out = p;
    *o_out = o;
    return 1;
}

/* Sets frame g to try one quarter turn of face, and works out its child. */
static void start_face(unsigned g, unsigned face)
{
    stack[g].face = face;
    stack[g].turn = 0;
    stack[g + 1].p = perm_row[face][stack[g].p];
    stack[g + 1].o = ori_row[face][stack[g].o];
}

/* Moves frame g to its next child: one more quarter turn of the same face,
 * so R, R2 and R' are one, two and three lookups in the same row; after
 * three, one quarter turn of the next face other than skip. Returns 0 when
 * the frame has no child left.
 */
static int next_child(unsigned g)
{
    frame_t *f = &stack[g], *child = &stack[g + 1];
    unsigned face;
    if (f->turn < 1) { /* BROKEN ON PURPOSE: was turn < 2 */
        ++f->turn;
        child->p = perm_row[f->face][child->p];
        child->o = ori_row[f->face][child->o];
        return 1;
    }
    face = f->face + 1;
    if (face == f->skip)
        ++face;
    if (face > 2)
        return 0;
    start_face(g, face);
    return 1;
}

/* One depth-first pass over paths of at most bound moves, from the root in
 * frame 0. Returns the solution length, or 0 if none fits.
 *
 * A child at depth g + 1 may use at most limit = bound - g - 1 more moves,
 * and is pruned when either distance table says it needs more. Only the
 * solved entry is 0 in both tables, so a child that survives with limit 0 is
 * solved: there is no separate test for it.
 */
static unsigned search(unsigned bound)
{
    unsigned g = 0;
    start_face(0, 0); /* the root has no face to skip */
    for (;;) {
        unsigned limit = bound - g - 1;
        frame_t *child = &stack[g + 1];
        if (perm_dist[child->p] <= limit && ori_dist[child->o] <= limit) {
            if (limit == 0)
                return g + 1;
            child->skip = stack[g].face;
            ++g;
            start_face(g, stack[g].skip == 0 ? 1 : 0);
            continue;
        }
        while (!next_child(g)) {
            if (g == 0)
                return 0;
            --g;
        }
    }
}

/* IDA*: one pass per bound, from h(root) up. The first pass that finds a
 * solution finds a shortest one, because h never overestimates (gate H1) and
 * every smaller bound has already failed. No state is more than 11 moves
 * from solved (gate H3), so the bound stops there. Returns the length, with
 * the moves in stack[0..length-1], or -1 if nothing was found.
 */
static int solve(unsigned p, unsigned o)
{
    unsigned bound = perm_dist[p] > ori_dist[o] ? perm_dist[p] : ori_dist[o];
    if (bound == 0)
        return 0;
    stack[0].p = p;
    stack[0].o = o;
    stack[0].skip = NO_FACE;
    for (; bound <= MAX_DEPTH; ++bound) {
        unsigned length = search(bound);
        if (length)
            return length;
    }
    return -1;
}

/* T5: applies the moves in stack[0..length-1] to (p, o) one quarter turn at
 * a time, through the same row tables, and checks that they end at solved.
 * The tables equal the per-cubie model on every state (gate H2), so this
 * applies the solution to the input cube. Returns 1 if it ends solved.
 */
static int check_path(unsigned p, unsigned o, unsigned length)
{
    for (unsigned g = 0; g < length; ++g) {
        const uint16_t *perm = perm_row[stack[g].face];
        const uint16_t *ori = ori_row[stack[g].face];
        for (unsigned t = 0; t <= stack[g].turn; ++t) {
            p = perm[p];
            o = ori[o];
        }
    }
    return p == 0 && o == 0;
}

/* Prints the moves in solver.c's format: R, R2 or R', separated by spaces. */
static void print_path(unsigned length)
{
    for (unsigned g = 0; g < length; ++g) {
        if (g)
            putchar(' ');
        putchar("RBD"[stack[g].face]);
        if (stack[g].turn == 1)
            putchar('2');
        else if (stack[g].turn == 2)
            putchar('\'');
    }
    putchar('\n');
}

int main(int argc, char **argv)
{
    unsigned p, o;
    if (argc != 2 || !read_state(argv[1], &p, &o)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "ida");
        return 2;
    }
    int length = solve(p, o);
    if (length < 0) {
        fputs("no solution within 11 moves\n", stderr);
        return 1;
    }
    print_path(length);
    if (!check_path(p, o, length)) {
        fputs("solution does not reach solved\n", stderr);
        return 1;
    }
    return fflush(stdout) != 0 || ferror(stdout);
}
