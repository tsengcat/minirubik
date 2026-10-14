# Assignment 1, Stage 2: Choosing a Representation and an Algorithm

## 1. Why the baseline has to be replaced

The baseline builds its table once and then answers every query from it. Both the peak memory and the peak computation occur while that table is being built. The target imposes two limits:

| Limit | Value |
| :--- | :--- |
| Static data | 128 KiB |
| Retired instructions | $5 \times 10^7$ on `RV32_ISS`, with no `mul` or `div` |

The baseline meets neither (Stage 1, sections 7 and 8). The task in this stage is therefore to choose a method that is faster than building the BFS tree.

## 2. Choosing the search algorithm

There are three candidates: BFS, A\*, and IDA\*.

**BFS.** In the worst case BFS is the same as the baseline. If the distance is the maximum, 11, the graph has to be expanded all the way to level 11, and memory explodes. See the BFS queue in the assignment's peak-memory table, which is the largest allocation.

**A\*.** A\* is an extension of BFS, so it runs into the same memory bottleneck. It has to keep every generated node in an open list (a priority queue) and a closed list. That memory grows exponentially with the depth, and in the worst case it approaches the whole state space of 3,674,160 states.

**IDA\*.** IDA\* only has to maintain one stack, so its memory stays linear in the depth of the search.

This design therefore uses IDA\*.

## 3. State representation

The baseline combines $p$ and $o$ into a single rank, because it has to traverse and record every state in order to build the complete tree. Here the goal is different: find an optimal solution for the current state as quickly as possible, without traversing all states.

$p$ and $o$ are a good basis for IDA\*, because a solved cube must have both $p$ and $o$ solved. This gives a way to tell whether the current path is redundant. For example, if the search expects to finish within three moves, but the current state needs at least four moves to solve either its $p$ or its $o$, the path must be wrong.

So for $p$ and $o$ the program only needs to record, for each value, the minimum number of moves needed to solve that part alone. That is a cheap reference: the two tables take only `uint8` × (5,040 + 729) = 5,769 bytes. Because $p$ and $o$ are kept apart, the search never has to compute `rank / 729` or `rank % 729`.

## 4. The heuristic: two projection tables

### 4.1 Scope

This problem starts from the solved cube and applies turns to it, so everything discussed here is limited to the states that turns can reach. States that cannot be reached that way, such as those whose orientation sum is not 0 mod 3, are left out from the start. The end point, the solved state, is the one where $o = p = 0$.

### 4.2 Building the tables

The two tables are built the same way the baseline builds its tree, with a BFS. Each projection has fewer than 10,000 entries (5,040 and 729), so neither time nor space is a concern.

### 4.3 Why the projection distance never exceeds the real distance

Solving the cube can be viewed as solving $o$ and solving $p$. Solving one of them alone, however, can scramble the other. In the hardest cases, such as two corners swapped in place, $o$ may already be solved while $p$ is still far from solved, so the solving process has to scramble $o$ first and restore it afterwards. In addition, a physical turn sometimes changes $o$ and sometimes does not, D being an example. Either way, the change is always within one move.

Every real move turns $o$ and $p$ at most once each, because a move only shifts what sits in each position, so the order and the orientation are updated on their own (Stage 1, section 4). Every optimal solution can therefore be seen as a path in the $o$ space from the current state to 0, and also as the process of solving $p$ in the $p$ space.

This path, however, is the shortest when both parts are considered together, not necessarily the shortest within either space alone. It is similar to the triangle inequality, where the sum of two sides is greater than the third. If the real optimal path also happens to be the optimal path for $p$ or for $o$ alone, then it is that space's own optimum, and the bound still holds.

So to solve the $o$ or the $p$ projection, the number of real moves can never be smaller than the minimum number of moves that projection needs. The projection distance is therefore at most the real distance.

### 4.4 Why max, not sum

Two arguments show why the max is used and not the sum.

**A counterexample.** Take a single turn R from the solved cube. Both $p$ and $o$ are then at distance 1 in their own projections, so the sum is 2, while the real distance is 1. The sum overestimates.

**What the inequalities guarantee.** Let $a$ be the real distance, and write $p$ and $o$ here for the two projection distances. Section 4.3 gives $a \ge p$ and $a \ge o$. These only establish that $a$ is at least the larger of the two, $a \ge p \ge o$ or $a \ge o \ge p$, that is, $a \ge \max(p, o)$. They do not lead to $a \ge p + o$; adding them gives at most $2a \ge p + o$.

## 5. Pruning

Pruning comes from two sources.

1. **The minimum moves of $p$ and $o$**, as described in section 3.
2. **Consecutive turns of the same face.** If R, R2, and R' appear next to each other, the combined effect can be reached within a single move, so there is no need to turn the same face twice in a row. After the first move, each expansion therefore tries 2 faces × 3 turns instead of 3 faces × 3 turns, since the face that was just turned is not chosen again.

## 6. Why IDA\* finds a shortest solution, and why it stops

IDA\* assumes the cube can be solved within $N$ moves, with $N = h(\text{root}), h(\text{root}) + 1, \ldots$, up to 11. It starts at $h(\text{root})$, not at 1, and it raises $N$ by one at a time without skipping. Let $d$ be the length of an optimal solution.

- **$N < d$.** No solution can be found. Otherwise that solution would be shorter than the optimal one.
- **$N = d$.** The condition stays satisfied along an optimal path: $h$ never overestimates (section 4), so no state on that path is pruned by the bound. Consecutive turns of the same face never appear in an optimal path (section 5), so that pruning does not remove it either. The optimal path is therefore inside the searched set, and it will be found.
- **$N > d$.** This never happens. The search starts at $h(\text{root})$, which is at most $d$ because $h$ never overestimates, and $N$ grows one step at a time. It must equal $d$ before it can exceed it, so the solution is found first.

A solution is always found because, from the BFS, every legal state can be transformed into the solved state within 11 moves. So the solved state is always reachable within 11 moves from the current state. Here a legal state means a state within the scope of section 4.1, that is, one that turns can reach from the solved cube.

Each round of the search also ends on its own. Along any path, $g$ grows by one with every move and $h$ is never negative, so even ignoring how $h$ changes, $g + h$ must eventually exceed the bound $N$ of that round, and the path is cut there. No path therefore goes deeper than $N$ moves. The number of nodes grows by at most a factor of 6 per level, with at most $9 \times 6^{k-1}$ nodes on level $k$, so every level holds a finite number of nodes, never an infinite one. Each round therefore visits finitely many nodes and finishes, and the next round can start.

The lower bound also keeps wrong paths from being expanded too far.

## 7. Memory budget

| Table | Element type | Elements | Bytes |
| :--- | :--- | ---: | ---: |
| `perm_turn` | `uint16_t` | 3 × 5,040 | 30,240 |
| `ori_turn` | `uint16_t` | 3 × 729 | 4,374 |
| `perm_dist` | `uint8_t` | 5,040 | 5,040 |
| `ori_dist` | `uint8_t` | 729 | 729 |
| Total | | | 40,383 |
| Remaining budget | | | 131,072 − 40,383 = 90,689 (about 88.6 KiB) |

The total is the `static data` line printed by `./host/test/ida_test`.

About 88 KiB remain, which is more than enough, so no further compression is needed. Compressing further could even cost time: reading one nibble takes about five more instructions than reading a byte.

The remaining space could instead be spent on reducing computation time. The current numbering schemes, the factorial-based index up to 5,040 and the base-3 index, are not convenient to compute, and changing them is worth considering.

These are the sizes of the C arrays. The actual size of `.data` on Ripes, and any static data besides these tables, will be checked in Stage 4.

## 8. Search effort against the instruction budget

The assignment works backwards from the budget: at about 200 instructions per expanded node, $5 \times 10^7$ instructions leave room for about $2.5 \times 10^5$ expansions. With only the same-face pruning, a depth-11 search tree holds 653,034,700 nodes, so the heuristic has to cut the search by a factor of at least 2,600.

| Step | Value | Source |
| :--- | ---: | :--- |
| Expansion ceiling, $5 \times 10^7 / 200$ | 250,000 | assignment |
| Depth-11 tree with same-face pruning | 653,034,700 | assignment |
| Measured worst case over all 3,674,160 states | 106,636 expanded nodes, for `54721631111111` | H3 run, section 9 |
| Measured mean over all states | 2,698.3 expanded nodes | H3 run, section 9 |
| Share of the ceiling used by the worst case | 42.7% (a margin of about 2.3×) | 106,636 / 250,000 |
| Reduction achieved by the heuristic | about 6,100× | 653,034,700 / 106,636 |

The worst state is a distance-11 state, so it falls within the pass condition.

The figure of 200 instructions per node is the assignment's assumption, not a measurement. The actual cost per node is argued from operation counts in Stage 3 and measured with `--iret` in Stage 4.

## 9. Evidence on the host

All checks use `host/test/ida_test`, the host prototype of this design. The exact distances come from its own `build_exact()`, a level-by-level BFS over all 3,674,160 states, not from `solver.c`'s `build_table()`.

| Check | What is checked | Result | Command |
| :--- | :--- | :--- | :--- |
| H1 | $h(s) \le d(s)$ for all 3,674,160 states | 0 states with $h > d$ | `./host/test/ida_test` |
| H2 | Both distance tables are fully populated, with their maximum value and solved entry checked | `perm_dist`: 5,040 entries, all populated, max 7, solved entry 0. `ori_dist`: 729 entries, all populated, max 6, solved entry 0 | `./host/test/ida_test` |
| H3 | For every state, the returned solution length equals the exact distance | 3,674,160 states, 0 not optimal | `caffeinate -i time ./host/test/ida_test --all` |

The exact BFS reaches all 3,674,160 states and finds a diameter of 11.

The H3 run took 256.82 s of wall-clock time (256.17 s user, 0.52 s system). It was run on the machine described in Stage 1, in the same power state: on battery, with Low Power Mode on. `caffeinate -i` kept the machine from sleeping during the run.

Besides checking the length, `ida_test` also replays every solution on the per-cubie model and checks that it reaches the solved state. That replay is a separate check from H3.

## AI usage disclosure


Claude (Anthropic) wrote `host/test/ida_test.c`, originally named `stage2_probe.c`. That includes the IDA\* search, the heuristic tables, the H1 to H3 checks, and the cost estimate. Claude later refactored it so it no longer includes `solver.c`, renamed it, and split the table output into `host/test/emit_tables.c`. Claude also wrote temporary programs to check the emitted tables against `solver.c` and to confirm that Ripes reads them correctly.

Claude explained the IDA\* optimality argument and the projection argument to me, and reviewed my drafts of both, pointing out gaps. The measurements were run by me; Claude did the arithmetic for the ratios in section 8. The arguments in this note are mine: I wrote them in Chinese, and Claude translated them into English and arranged them into these sections.
