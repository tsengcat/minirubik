# Assignment 1, Stage 1: Characterizing the Baseline

> Revision 1. This revision covers Stage 1 only. The two Ripes measurements are not done yet (see section 8).

## 1. What the program computes

`solver.c` solves the 2×2×2 Rubik's Cube optimally. It first builds a table over every state of the cube, then answers a query by looking up that table one move at a time until the cube is solved.

## 2. State representation

The whole cube is encoded in a very compact form, because the puzzle only has 3,674,160 states. One corner is fixed, so only the other 7 corners need to be described. Each corner differs only in two things: its order (which position it sits in) and its orientation.

- The order is handled with a factorial number system. The 7 digits describing which corner is where are turned into a single number $p$ from 0 to 5039, that is, below $7!$.
- The orientation is a base-3 number $o$ from 0 to 728 ($3^6$ values) over 6 corners. Every turn changes the orientations by a total that is 0 mod 3, so the orientation sum of all 7 corners always stays 0 mod 3. Because of that, the last corner's orientation is fixed by the other six, and 6 are enough. It cannot be reduced to 5: with two unknown corners, they could compensate each other in more than one way.
- The final encoding is $\text{rank} = p \times 729 + o$, which gives $7! \times 3^6 = 3{,}674{,}160$ values.

## 3. Applying a move without decoding

With such a compact encoding, applying a move should not require three steps (decode, rotate, re-encode) every time. That work is done only once. The move is turned into two lookup tables: one maps what happens to the order under each move, the other what happens to the orientation. Applying a move then only means splitting the number into P and O, looking up P' and O', and combining them again.

This split works because a move is a change of positions: it only shifts what sits in each position, so the order and the orientation can each be updated on their own.

The two tables are also much more convenient than one. A single table would have to cover all 3,674,160 states and would be extremely large, especially on Ripes.

## 4. BFS and the solution table

With the rotation cost solved, building the table becomes a breadth-first search. The BFS still needs a queue to hold its contents, so memory for all 3,674,160 entries has to be reserved for it. Each entry stores a rank, and the number of possible ranks does not fit in 2 bytes. 3 bytes would be enough, but that is awkward to implement, so each entry takes 4 bytes. This is the first limit.

The other part is the solution tree: for each state, how to reach the root (the solved state) as fast as possible. The tree is at most 11 levels deep. It also covers all 3,674,160 states, but each entry only needs about 1 byte.

What each entry stores: when BFS applies move A to a state and reaches a new state', it records A' (the inverse of A) at state'. Applying A' to state' leads back to state, one step closer to solved.

BFS guarantees the shortest path because of how the algorithm works: it finishes one level before going to the next, so each recorded move leads to a shorter distance.

To solve a state, the program looks up which move to make at each step. That move comes from the BFS result, so it always goes in the optimal direction. The maximum of 11 moves comes from building the table, since every state has been computed.

## 5. Orientation convention

Orientation is defined by the up/down sticker of each corner. A D turn rotates around the up/down axis, so the orientation does not change.

## 6. Where the cost lies

Peak memory is reached when the solution tree and the queue exist at the same time. The figures below are computed in `report.md` section 4 and restated in the assignment:

| Allocation | Storage | Bytes |
| :--- | :--- | ---: |
| Move toward solved, 1 byte per state | heap | 3,674,160 |
| BFS queue, 4 bytes per state | heap | 14,696,640 |
| Factored transition tables, $3 \times (5{,}040 + 729)$ `uint16_t` | automatic | 34,614 |
| Peak | | 18,405,414 (17.553 MiB) |

The queue is 79.8% of the peak. After BFS finishes, the queue is freed and only the 3,674,160-byte move table is kept for solving.

## 7. Why the baseline does not fit Ripes

`report.md` section 7 argues for keeping the full table. That does not work on Ripes:

- **Memory.** When the program runs in Ripes, its memory is stored on the host in an `unordered_map`, so every byte also costs the map's address, hash, key, value, and next pointer. The 18 MB peak would blow up on the host. How large the blow-up is depends on the Ripes build, which is why it has to be measured (section 8).
- **Computation.** The BFS itself is also far too expensive to run in simulation. The assignment estimates on the order of $10^9$ retired instructions for the build, which is about 15 minutes on `RV32_ISS` and hours on the pipelined models.

## 8. Measurements

Not measured yet at the time of this revision: the host-bytes-per-guest-byte ratio, and retired instructions per second on `RV32_ISS` and one pipelined model. These, together with the Ripes version and the forked commit, will be added in the next revision.

## AI usage disclosure

I used Claude (Anthropic) to help me read `report.md` and `solver.c`, and to quiz me on the baseline. The answers in this note are mine: I wrote them in Chinese, and Claude translated them into English and arranged them into these sections. The memory figures in section 6 and the instruction estimate in section 7 are quoted from `report.md` and the assignment, not measured by me.
