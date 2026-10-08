# Assignment 1, Stage 1: Characterizing the Baseline

## Test environment

All measurements in this note were taken on the single machine described below. Simulation speed depends on the host, so the figures in this note describe this setup and should not be compared directly with figures from other machines.

| Item | Value |
| :--- | :--- |
| Machine | Apple `Mac17,3` |
| Processor | Apple M5, 10 CPU cores (4 performance + 6 efficiency) |
| Memory | 24 GB unified memory, 16 KiB page size |
| Operating system | macOS 27.0.1 (build 26A434), Darwin 27.0.0, arm64 |
| Power | On battery, Low Power Mode on |
| Ripes | `v2.2.6-106-g5b8a616` (commit `5b8a616` on `master`, 106 commits after v2.2.6), macOS universal2 build running natively on arm64 |
| Ripes ISA setting | RV32I only (the CLI default, with no extensions enabled) |
| minirubik fork | `tsengcat/minirubik`, forked from `sysprog21/minirubik` at commit `231796c` |

**Power state.** Every test in this note, including the memory measurement and the instruction-rate measurement, was run in this same environment: on battery power with macOS Low Power Mode turned on, and not connected to a charger. The aim was a stable environment that stays the same across all runs, not the best performance the machine can reach.

## 1. What the program computes

`solver.c` solves the 2×2×2 Rubik's Cube optimally. It first builds a table over every state of the cube, then answers a query by looking up that table one move at a time until the cube is solved.

## 2. State representation

The whole cube is encoded in a very compact form, because the puzzle only has 3,674,160 states. One corner is fixed, so only the other 7 corners need to be described. Each corner differs only in two things: its order (which position it sits in) and its orientation.

- The order is handled with a factorial number system. The 7 digits describing which corner is where are turned into a single number $p$ from 0 to 5039, that is, below $7!$.
- The orientation is a base-3 number $o$ from 0 to 728 ($3^6$ values) over 6 corners. Every turn changes the orientations by a total that is 0 mod 3, so the orientation sum of all 7 corners always stays 0 mod 3 (section 3). Because of that, the last corner's orientation is fixed by the other six, and 6 are enough. It cannot be reduced to 5: with two unknown corners, they could compensate each other in more than one way.
- The final encoding is $\text{rank} = p \times 729 + o$, which gives $7! \times 3^6 = 3{,}674{,}160$ values.

## 3. Invariants

### Orientation sum is 0 mod 3

This constraint is about cubes that can actually be solved. Every legal state of the cube is reached from the solved cube by a sequence of face turns, and in no other way. The solved cube has an orientation sum of 0, and each turn changes the sum by a multiple of 3, so after any number of turns the sum is still 0 mod 3. Every reachable state therefore lies in the set where

$$
\sum_{i=1}^{7} o_i \equiv 0 \pmod 3 .
$$

This rules out, for example, a cube with exactly one corner twisted in place, since its sum would be 1 or 2 mod 3. Of the $3^7$ ways to assign orientations to seven corners, only one third satisfy the constraint, which is $3^6 = 729$. It is a modulo-3 invariant, not a parity.

### All 7! corner arrangements are reachable

Position 0 holds the anchor corner. It is fixed to remove whole-cube rotations and is never moved by `R`, `B`, or `D`, so it is left out here. The question is how the other seven corners can be arranged, and the answer is that all $7! = 5{,}040$ arrangements occur, with no parity restriction.

A quarter turn moves four corners in a 4-cycle, which is an odd permutation (it is three transpositions). On the 3×3×3 the same quarter turn also cycles four edges, so the corner and edge permutations always have the same parity and only half of the combined arrangements are reachable. The 2×2×2 has no edges, so nothing ties the corner parity to anything else, and both even and odd arrangements of the corners occur. The odd generator only shows that the group is not restricted to even permutations; that every one of the $7!$ arrangements is actually reached is confirmed by the BFS, which visits $7! \times 3^6$ states.

## 4. Applying a move without decoding

With such a compact encoding, applying a move should not require three steps (decode, rotate, re-encode) every time. That work is done only once. The move is turned into two lookup tables: one maps what happens to the order under each move, the other what happens to the orientation. Applying a move then only means splitting the number into P and O, looking up P' and O', and combining them again.

This split works because a move is a change of positions: it only shifts what sits in each position, so the order and the orientation can each be updated on their own.

The two tables are also much more convenient than one. A single table would have to cover all 3,674,160 states and would be extremely large, especially on Ripes.

## 5. BFS and the solution table

With the rotation cost solved, building the table becomes a breadth-first search. The BFS still needs a queue to hold its contents, so memory for all 3,674,160 entries has to be reserved for it. Each entry stores a rank, and the number of possible ranks does not fit in 2 bytes. 3 bytes would be enough, but that is awkward to implement, so each entry takes 4 bytes. This is the first limit.

The other part is the solution tree: for each state, how to reach the root (the solved state) as fast as possible. The tree is at most 11 levels deep. It also covers all 3,674,160 states, but each entry only needs about 1 byte.

What each entry stores: when BFS applies move A to a state and reaches a new state', it records A' (the inverse of A) at state'. Applying A' to state' leads back to state, one step closer to solved.

BFS guarantees the shortest path because of how the algorithm works: it finishes one level before going to the next, so each recorded move leads to a shorter distance.

To solve a state, the program looks up which move to make at each step. That move comes from the BFS result, so it always goes in the optimal direction.

### Why the diameter is 11

For the general $n \times n \times n$ cube, finding an optimal solution is NP-complete (Demaine, Eisenstat, and Rudoy, 2018), and there is no formula that gives the diameter directly. The 2×2×2 is small enough that the answer comes from exhaustive search instead: the BFS runs level by level until every state has been visited, and the diameter is read off from where it stops. That establishes two separate facts:

1. A state at distance 11 exists. Level 11 is not empty: it holds 2,644 states, among them `21345671111111`.
2. No state is deeper than 11. By the end of level 11 the BFS has visited all 3,674,160 states, and expanding the 2,644 states at level 11 discovers nothing new, so there is no level 12.

Both facts need the complete search. Finding one hard state proves only the first; the second holds only because every state has been counted.

## 6. Orientation convention

Orientation is defined by the up/down sticker of each corner. A D turn rotates around the up/down axis, so the orientation does not change.

## 7. Where the cost lies

Peak memory is reached when the solution tree and the queue exist at the same time. The figures below are computed in `report.md` section 4 and restated in the assignment:

| Allocation | Storage | Bytes |
| :--- | :--- | ---: |
| Move toward solved, 1 byte per state | heap | 3,674,160 |
| BFS queue, 4 bytes per state | heap | 14,696,640 |
| Factored transition tables, $3 \times (5{,}040 + 729)$ `uint16_t` | automatic | 34,614 |
| Peak | | 18,405,414 (17.553 MiB) |

The queue is 79.8% of the peak. After BFS finishes, the queue is freed and only the 3,674,160-byte move table is kept for solving.

## 8. Why the baseline does not fit Ripes

`report.md` section 7 recommends keeping the full table, because building it exhaustively is what verifies the cube model. On a hosted machine that costs 0.065 s and is a reasonable trade. On Ripes the same choice grows by orders of magnitude.

The point of this section is not that the baseline can never be made to run. On my machine it actually could. By the measurements in section 9, the 18.4 MB peak would take about 1 GB of host memory, which fits in 24 GB, and the roughly $10^9$ instructions of the build would take about 55 seconds on `RV32_ISS`. The point is how the cost scales once the program moves onto the simulator:

| Cost | Native | On Ripes, this machine | On Ripes, assignment's reference rates |
| :--- | :--- | :--- | :--- |
| Memory for the 18,405,414-byte peak | 18.4 MB | about 1 GB (×53 to 58) | — |
| Building the table, about $10^9$ instructions | 0.065 s | 55 s on `RV32_ISS`, 36.5 min on `RV32_5S` | 15 min on `RV32_ISS`, 13.5 h on `RV32_5S` |

That is an explosive trend, and how badly it hurts depends on the host. My machine happens to be fast enough to absorb it, even on battery in Low Power Mode. Most machines would not: the assignment's own reference rates are 16 to 22 times slower than what I measured.

The assignment's other rules point the same way. The pass condition is $5 \times 10^7$ retired instructions on `RV32_ISS`, and the BFS alone is about 20 times that. Static data is capped at 128 KiB, while the baseline needs 18,405,414 bytes, about 140 times as much, and hand-written assembly has no heap to put it in. A complete precomputed distance table is not accepted either, so the table cannot be built on the host and linked in. These limits make it clear that the task is not just to get the baseline running, but to run the search on the target within a small, fixed budget, by compressing the memory and cutting the instruction count.

Finally, if the target were a real small RV32I device rather than a simulator, the hash-map overhead would disappear, but the 18.4 MB working set would remain, and that is already far beyond the memory such devices typically have.

## 9. Measurements

### 9.1 Host bytes per guest byte

**Method.** Ripes stores guest memory on the host in an `unordered_map` keyed by address, one entry per guest byte, so every byte the program writes costs much more than one byte on the host. To measure how much, I ran a control program that only exits (`li a7, 10` and `ecall`) and programs that write N = 512,000 and N = 1,024,000 bytes with consecutive `sw` instructions starting at `0x10000000`. Each run was wrapped in `/usr/bin/time -l`, which reports the maximum resident set size (RSS) and the peak memory footprint (FP). The programs are in [`asm/test_mem/`](../asm/test_mem/).

```sh
/usr/bin/time -l ./Ripes-v2.2.6-106-g5b8a616-mac-universal2.app/Contents/MacOS/Ripes \
  --mode cli --src asm/test_mem/<file>.s -t asm --proc RV32_ISS --iret
```

To confirm that each program wrote exactly N bytes, I also checked `--iret`: the expected count is $3 \times (N/4) + 8$, and every run matched it.

**Data.** Averages per program from the main round of runs:

| N (bytes) | Runs | RSS (bytes) | FP (bytes) |
| ---: | ---: | ---: | ---: |
| 0 (control) | 3 | 73,028,949 | 13,091,736 |
| 512,000 | 2 | 100,122,624 | 42,804,160 |
| 1,024,000 | 2 | 129,753,088 | 72,426,444 |

The three control runs differ by at most 98,304 bytes of RSS (0.13%). Two earlier rounds at N = 512,000 (nine more runs) gave RSS between 100,024,320 and 100,302,848 bytes, consistent with this round.

**Calculation.** Subtracting the control and dividing by N gives the host cost of each guest byte:

| N (bytes) | ΔRSS | ΔRSS / N | ΔFP | ΔFP / N |
| ---: | ---: | ---: | ---: | ---: |
| 512,000 | 27,093,675 | 52.92 | 29,712,424 | 58.03 |
| 1,024,000 | 56,724,139 | 55.39 | 59,334,708 | 57.94 |

Both sizes give between **53 and 58** host bytes per guest byte. FP is the more consistent of the two measures, at 58.03 and 57.94, and the increase from 512,000 to 1,024,000 bytes works out to 57.9 host bytes per guest byte for both RSS and FP. As a cross-check, the increase in page reclaims times the 16 KiB page size agrees with ΔFP to within 0.7% at both sizes.

**Projection.** At 53 to 58 host bytes per guest byte, the baseline's 18,405,414-byte peak would cost 0.97 to 1.07 GB of host memory, about **1 GB**.

### 9.2 Retired instructions per second

**Method.** I used two loops, both in [`asm/test_rate/`](../asm/test_rate/):

- `mem_loop`: `sw t1, 0(t0)`, `addi t1, t1, -1`, `bne t1, x0, loop`. Three instructions per iteration, storing to the same address every time.
- `reg_loop`: `addi t0, t0, -1`, `bne t0, x0, loop`. Two instructions per iteration, with no memory access.

Time comes from Ripes' `--exectime`, in milliseconds. Every run carries a fixed overhead of about 38 ms on `RV32_ISS`, visible as almost the same time for COUNT 1 to 10,000. To cancel it, the rate is computed from the difference between a large and a small COUNT:

$$
\text{rate} = \frac{\text{iret}_{\text{large}} - \text{iret}_{\text{small}}}{t_{\text{large}} - t_{\text{small}}}
$$

Each time below is the median of its runs. Every `--iret` matched the expected count ($3 \times \text{COUNT} + 5$ or $6$ for `mem_loop`, $2 \times \text{COUNT} + 3$ or $4$ for `reg_loop`; the extra instruction appears when `li` needs a `lui` for a large immediate).

```sh
./Ripes-v2.2.6-106-g5b8a616-mac-universal2.app/Contents/MacOS/Ripes \
  --mode cli --src asm/test_rate/<file>.s -t asm --proc <MODEL> --iret --exectime
```

**Data and results.**

| Model and loop | COUNT | iret | Median time (ms) | Rate |
| :--- | :--- | :--- | :--- | ---: |
| `RV32_ISS`, `mem_loop` | 1 → $10^8$ | 8 → 300,000,006 | 37.5 → 16,667 | **18.04 M/s** |
| `RV32_ISS`, `reg_loop` | 1 → $10^8$ | 5 → 200,000,004 | 40 → 8,507 | 23.62 M/s |
| `RV32_5S`, `mem_loop` | $10^4$ → $10^6$ | 30,006 → 3,000,006 | 67 → 6,577.5 | **456.2 K/s** |

What these rates mean in time:

| Model | $10^9$ instructions (baseline build) | $5 \times 10^7$ instructions (pass condition) |
| :--- | ---: | ---: |
| `RV32_ISS` | 55.4 s | 2.8 s |
| `RV32_5S` | 36.5 min | 1.8 min |

Compared with the assignment's table, which also uses a memory loop, `RV32_ISS` is 16.6 times faster here (18.04 M/s against 1.09 M/s) and `RV32_5S` is 22.3 times faster (456.2 K/s against 20.5 K/s). On this machine `RV32_ISS` is 39.5 times faster than `RV32_5S`.

**Notes.**

- `reg_loop` at COUNT $10^8$ varied between 7,343 and 9,108 ms over five runs, a 24% spread. `mem_loop` varied by 0.2% and `RV32_5S` by 1.3%. I use `mem_loop` as the main figure, which is also what the assignment's table uses.
- Shorter runs give higher rates. On `RV32_ISS`, `mem_loop` gives 22.7 M/s between COUNT 1 and $10^6$, but 18.0 M/s between $10^6$ and $10^8$. All rates above use the largest COUNT available.

## AI usage disclosure

I used Claude (Anthropic) to help me read `report.md` and `solver.c`, to quiz me on the baseline, and to explain some group-theory background used in section 3. The measurements were run by me; Claude helped organize the measurement data and carry out the arithmetic for the ratios and rates. The answers and arguments in this note are mine: I wrote them in Chinese, and Claude translated them into English and arranged them into these sections. The memory figures in section 7 and the instruction estimate in section 8 are quoted from `report.md` and the assignment, not measured by me.
