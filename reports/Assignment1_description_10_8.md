---
tags: computer-arch
---

# Assignment 1: Optimizations and RISC-V Assembly
> Phase 1 due: ==Oct 8, 2026, 23:59 (GMT+8)==
> Phase 2 due: ==Oct 18, 2026, 23:59 (GMT+8)==

## Scope

This assignment has two phases. Phase 1 turns a working C program into hand written RV32I assembly with a visual demonstration on Ripes. Phase 2 is a short interview about that code, conducted under [Section 5 of the AI Guidelines for Computer Architecture (Fall 2026)](https://hackmd.io/@sysprog/arch2026-ai-guidelines).

The Phase 1 deadline marks what you submit, not what you are stuck with. Tag the commit you are submitting and record the tag and the HackMD revision URL on the form: that snapshot is what is graded and what the interview is built from. Both the C program and the RV32I assembly stay open for improvement afterwards, in later commits, and arriving at the interview with a better version is to your credit, provided you can account for what changed and why.

## Preparation

1. Follow [Lab1: RV32I Simulator](https://hackmd.io/@sysprog/H1TpVYMdB) to get acquainted with RISC-V assembly ([RV32I](https://en.wikipedia.org/wiki/RISC-V) ISA), including console output through environment calls.
2. Install a Ripes build that provides the `RV32_ISS` processor model. It is not in the v2.2.6 release; use a `continuous` prerelease or a build from `master`. Record the version you used, since the figures below and correctness gate T7 depend on it.
3. Fork [sysprog21/minirubik](https://github.com/sysprog21/minirubik), a solver for the 2x2x2 Rubik's Cube. Record the commit you forked from in your write-up, so your measurements stay interpretable if upstream moves. All Phase 1 work lives in your fork, pushed as commits with meaningful messages (see [How to Write a Git Commit Message](https://cbea.ms/git-commit/)). :warning: Your work MUST be committed to the main branch of your GitHub repository forked from [sysprog21/minirubik](https://github.com/sysprog21/minirubik).

Throughout, move counts are in the half-turn metric (HTM), where each of `R R2 R' B B2 B' D D2 D'` costs one move. In the quarter-turn metric a half turn costs two and the diameter is 14, not 11.

## Phase 1: minirubik on RV32I
> Due Oct 8, 2026, 23:59 (GMT+8)

### The starting point

`solver.c` builds a complete breadth-first table over all 3,674,160 states of the 2x2x2 cube, then answers any query in at most 11 table lookups. [`report.md`](https://github.com/sysprog21/minirubik/blob/main/report.md) in the repository describes how it works. The sum of its three dominant allocations is exact:

| Allocation | Storage | Bytes |
| :--- | :--- | ---: |
| One move toward solved per state | heap | 3,674,160 |
| BFS queue of 32-bit ranks | heap | 14,696,640 |
| Factored quarter-turn transitions | automatic | 34,614 |
| Peak | | 18,405,414 (17.553 MiB) |

Building that table expands 3,674,160 states across 33,067,440 edges, and each edge advances two factored tables, the permutation and the orientation, so the build performs 66,134,880 transition updates. At roughly 15 instructions per update, a direct translation retires on the order of $10^9$ instructions. That figure is an estimate, not a measurement: the premise of this assignment is that you cannot run the baseline on the target to check it.

Read `report.md` section 7 before you start, and read it critically. It recommends keeping the full table, on the grounds that the table is the verification artifact. That argument is sound on a hosted machine and does not survive the move to Ripes. Explaining why is part of stage 1.

### Why the target changes the answer

Neither number is a defect in the C program. Both become fatal only because of where the program now has to run, and working out why is the first thing this assignment asks of you.

Guest memory is sparse host memory. The VSRTL model behind Ripes stores written guest bytes in an `unordered_map<address, byte>` rather than a contiguous array, so one hash entry exists per guest byte and a single 32-bit store creates four. Measure that ratio yourself with a tight loop over a large region against a small control, then project what the 18,405,414-byte peak would cost your machine. The slope is an empirical property of one build, not an API guarantee, which is exactly why you measure it instead of quoting it.

Simulation rate is the harder limit. A simple memory loop measures roughly:

| Ripes processor | Retired instructions/second | Idealized time for $10^9$ |
| :--- | ---: | ---: |
| `RV32_ISS` | 1.09 million | 15 minutes |
| `RV32_SS` | 86 thousand | 3.2 hours |
| `RV32_5S` | 20.5 thousand | 13.5 hours |
| `RV32_6S_DUAL` | 6.2 thousand | 45 hours |

The visual pipeline models are worth using to step through a single query. They are not worth using to build a table.

RV32I imposes a third constraint that RV32IM would not. The base integer ISA has no multiply and no divide at all, so `mul`, `mulh`, `div`, and `rem` are equally unavailable. A high-half product can be synthesized from 16-bit pieces, so reciprocal-multiply division by a constant is not strictly impossible, but it is expensive enough that restructuring the program until the division disappears beats implementing it.

### Stages

Work through the stages in order. Each appears in your HackMD note as its own section, carrying the reasoning that leads to the next one.

1. Characterize the baseline. Explain what the program computes, how it represents a cube, which invariants it relies on, and where its cost lies. Reproduce two measurements on your own installation and report what you get: the host-bytes-per-guest-byte ratio, and the retired-instructions-per-second rate for at least `RV32_ISS` and one pipelined model.
2. Choose a representation and an algorithm that fit the target. Optimality is not negotiable: whatever you build still returns a shortest solution, and you can show that it does. Justify every choice against the numbers from stage 1, and against the budget below.
3. Improve efficiency in C first. Remove branches, memory traffic, and arithmetic the target cannot execute cheaply, and argue the effect from operation counts.
4. Translate the result into RV32I assembly, together with its test data. From here on, measure with Ripes' `--iret` rather than asserting that something got faster.

What counts as done, for grading:

* Total static data, `.data` plus `.bss` plus `.rodata`, at or under 128 KiB.
* No distance-11 state exceeding $5 \times 10^7$ retired instructions on `RV32_ISS`, with the renderer compiled out, on the Ripes build you pinned. This is the pass condition, and it is a worst case: search cost varies by more than 3x across the 2,644 distance-11 states, so one sampled state is not evidence about the rest.
* Report separately the count for the vector `21345671111111`, so the figures are comparable between submissions. That number is reported, not graded against a threshold.

Precomputation rule: transition tables and any heuristic tables may be generated on the host in C and linked in as read-only data. The search itself must execute on the target. A complete precomputed distance table over all 3,674,160 states is not accepted, whatever its packing, because it moves the work off the simulated processor; the 128 KiB budget is set to rule it out.

A direction rather than a recipe: admissible heuristics, pattern databases built over abstractions of the state space, and iterative-deepening search are the established tools for this shape of problem, and the references below cover all three. Section 4 of `report.md` is the place to start, since it establishes that the permutation and the orientation evolve independently of one another. Which abstractions to build, how to pack them, and where to put the memory-against-time line is yours to decide and yours to defend with measurement. Ideas that sound promising and turn out not to pay are worth reporting too; a negative result you measured is worth more than a positive one you assumed.

### Where the gains are

Three independent directions, each with what it is actually worth here. None of them is the design; how they combine is stage 2.

Memory. The baseline's 18,405,414 bytes are not evenly spread. The BFS queue alone is 14,696,640 bytes, 79.8% of the peak, and it exists only because the search enumerates the whole space breadth-first. Give up full enumeration and it disappears outright, along with the 3,674,160-byte move-per-state table that is the other 20.0%. What remains, the 34,614 bytes of factored transition tables (automatic storage in the C baseline, `.rodata` once linked on the target), fits the 128 KiB budget nearly four times over. That is the point rather than the relief: the other 94 KiB is what you have to spend on heuristics, and spending it well is the design problem. Past that the reductions get smaller and more deliberate: no distance on this cube exceeds 11, so a distance fits in a nibble and any byte-per-entry table halves; and a table built over an abstraction of the state space is smaller than the state space by a factor equal to the size of the fibers the abstraction collapses, exactly so when those fibers are equal-sized, which they are for the projections this cube offers. For scale, the complete distance table packed at four bits per state is still 1,794 KiB, fourteen times the 128 KiB budget. Packing alone does not rescue enumeration.

Graph structure. The Cayley graph has 3,674,160 vertices and branching factor 9, and one observation cuts the search before any heuristic does. A move of face $f$ followed by another move of face $f$ either cancels or composes to a single move, so after the first move only 6 of the 9 generators are worth trying. It is free, and it matters more than it looks: unrolling the graph into a search tree duplicates each state once per path that reaches it, so even with that pruning applied a depth-11 tree holds 653,034,700 nodes against 3,674,160 distinct states in the graph. Do not read the state count as the target. At a few hundred instructions per expanded node, visiting all 3,674,160 states would cost around $7 \times 10^8$ retired instructions: 14 times the instruction budget above, and only a factor of 1.4 below the baseline this whole exercise exists to escape. Work the ceiling backwards from the budget instead. Two hundred instructions per node against $5 \times 10^7$ leaves you room for roughly $2.5 \times 10^5$ expansions, out of those 653,034,700 tree nodes. That is the reduction an admissible heuristic has to deliver. Other prunings suggest themselves; measure what each is worth before keeping it, since a check that looks free per child may not be.

Iterative deepening restarts at each bound, beginning at $h(\text{root})$ rather than 0, and with branching factor 6 the final bound costs more than everything before it combined. That is why the depth distribution in `report.md` matters: 51.4% of states sit at distance 9 and 92.0% between 8 and 10, so a typical query runs out to bound 9 or 10, and tuning against one easy state tells you nothing.

RV32I. With no multiply in the base ISA, every index computation is built from shifts and adds, and the cost is small and knowable: $\times 2$ and $\times 4$ are a single shift, while $\times 3$, $\times 5$, $\times 7$, and $\times 9$ each cost a shift plus one add or subtract. Larger constants decompose the same way, and a constant built from few power-of-two terms costs less than its binary expansion suggests; work out the sequence for whatever strides your layout actually needs. Strides that are powers of two cost nothing extra, and strides chosen carelessly reintroduce a multiply through the back door.

Modulo 3 is the other recurring operation, and it never needs a divide. Orientation arithmetic adds two values each at most 2, so the sum is at most 4 and a single conditional subtract is exact over all nine operand pairs. Branchless it costs four instructions after the add: an arithmetic shift turns the sign of $\text{sum} - 3$ into an all-ones mask exactly when no reduction was needed, and that mask adds the modulus back. RV32I compares register against register with no branch-immediate form, but `x0` is free, so the branching version tests the sign of the same $\text{sum} - 3$ and needs no constant register: two instructions when the branch is taken, three otherwise. It wins on retired instructions and loses on a pipelined model that has to flush. Measure both with `--iret` instead of guessing.

Packing has a price as well as a benefit. Reading a nibble out of a packed table runs about seven instructions including the address arithmetic and the load, against one or two for an unpacked byte table, so halving a table costs roughly five instructions on every lookup. Whether that trade pays depends on how often you look up, which is a measurement rather than a preference.

### Constraints

* Use only RV32I instructions. No extensions are permitted, M included, and you may not call compiler-generated routines such as `__mulsi3` or `__divsi3`. Where a multiply or a modulo survives, write it yourself; better, restructure so that it does not survive.
* No heap, no recursion, no floating point. There is no allocator in hand-written assembly, and Ripes' `brk` fails as soon as the break reaches the stack pointer, so size everything at assembly time.
* The RV32I program accepts an arbitrary cube state, supplied as a 14-character string inlined at assembly time. It will be exercised on states you have not seen.
* Include at least three test cases of your own: a solved cube, a short scramble, and a distance-11 state. Only 2,644 of 3,674,160 states are at distance 11, about 0.072%, so uniform sampling needs roughly 1,390 draws per hit; `tests/solutions.txt` already contains the vector `21345671111111`, and you may use it. Validate results inside the program rather than inspecting output by hand.
* Avoid a mechanical, line by line translation of the C source. Your assembly is expected to beat `riscv64-unknown-elf-gcc -O2 -march=rv32i -mabi=ilp32` on your own final C algorithm, measured in retired instructions and code size. Report that reference build alongside your assembly, and explain any case where your assembly does not win.
* Show the iterative refinement of your assembly, with explicit measurements at each step. Code size means bytes of linked `.text` with the renderer compiled out; retired instruction count means `--iret` on the pinned Ripes build with the same input. State both conventions in your note so the numbers are comparable across steps.
* Everything must run correctly on the [Ripes](https://github.com/mortbopet/Ripes) simulator.

### Correctness gates

The native C oracle enumerates the whole domain in well under a second, so gates H1 to H4 are cheap. Gate H3 is the exception: it runs your search over the entire domain and will take minutes rather than seconds. Budget for it and report its wall-clock time.

On the host, against the exact BFS table:

* H1. Any heuristic your search uses is admissible: $h(s) \le d(s)$ over all 3,674,160 states.
* H2. Every table the program depends on is fully populated, with its maximum value and its solved entry verified.
* H3. Your search returns a solution whose length equals the exact distance, for every state, not merely a solution that works.
* H4. Any packed accessor agrees with an unpacked reference at both even and odd indices.

On the target, in Ripes:

* T5. Applying every returned path reaches the solved state.
* T6. The vector `21345671111111` returns an optimal 11-move solution. The diameter itself is a host result from H3; running a handful of states on the target cannot establish that none is deeper.
* T7. Your three test cases plus any state the grader supplies reproduce in `RV32_ISS` and in at least one visual pipeline model.

H1 is the direct admissibility test. H3 is an independent end-to-end check: an inadmissible heuristic usually shows up there as a solver that is fast and quietly suboptimal, though in principle one could be inadmissible and still return optimal solutions throughout, which is why H1 is not optional.

### Visualization on the LED Matrix
![image](https://hackmd.io/_uploads/Bki3mBJqfg.png)
> The diagram above is for illustrative purposes only. The actual design depends on the number and arrangement of LEDs in the matrix.

Instantiate an LED Matrix peripheral in the I/O tab and set Width to 35 and Height to 25. Ripes lists Height above Width in that panel, so check which box you are typing into; 35 wide by 25 tall is the peripheral's own default.

* Drive it from your assembly. The peripheral is memory mapped, one 32-bit word per LED holding 24-bit RGB, indexed row-major as `y * WIDTH + x`. Address it through the assembler symbols `LED_MATRIX_0_BASE`, `LED_MATRIX_0_WIDTH`, and `LED_MATRIX_0_HEIGHT` rather than a literal address. The peripheral's own in-GUI description gives a column-major formula; it is wrong, and `examples/C/leds.c` shows the row-major layout the implementation actually uses.
* Render the cube as an unfolded net. The six faces sit in a 4 by 3 bounding box of face slots, six of the twelve occupied (one face above, four across, one below), giving an 8 by 6 bounding grid of facelets. At 4 pixels wide and 3 pixels tall per facelet this is 32 + 3 separator columns = 35 across, and 18 + 2 separator rows = 20 down, which leaves 5 rows spare inside the 25.
* Redraw after every move the solver emits, so the corner cubies are seen traveling to their home positions rather than only the final answer appearing. The six face colors must remain distinguishable throughout.
* The display must be driven by the solver's actual output. A pre-recorded animation does not satisfy this requirement.

Ripes' CLI instantiates no I/O peripherals, so a program referencing `LED_MATRIX_0_BASE` will not assemble under `--mode cli`, and the LED-driving build therefore cannot be measured with `--iret`. Guard the renderer behind an assemble-time switch, for example `.equ RENDER, 0` with `.if RENDER`, so that one source tree produces both a GUI build that animates and a CLI build that measures. State in your write-up that the two builds differ only in the renderer.

### Instruction-level walkthrough

Explain both program behavior and instruction-level operation using Ripes.
* Visualize signals such as register write enable and multiplexer selection.
* Walk through each pipeline stage: IF, ID, EX, MEM, WB.
* Describe how memory is updated and why the result is correct.

## Phase 2: Interview
> Due Oct 18, 2026, 23:59 (GMT+8)

Every student is interviewed about their Phase 1 submission. This runs under [Section 5 of the AI Guidelines](https://hackmd.io/@sysprog/arch2026-ai-guidelines), which governs it in full; what follows summarizes how it applies here and does not override it.

Format. Questions are generated from your own submitted work by LiveTrial, a variant of [CodeTrial](https://github.com/sysprog21/codetrial), and the conversation is conducted live. Only your own submitted coursework is sent to the tool.

What the interview covers:
* You walk through your own code and explain how it works: the cube encoding you chose, why the search terminates, what each optimization bought and at what cost, and how the LED matrix mapping is derived.
* Parts of your code are rewritten, removed, or replaced with blanks, and you are asked to complete them. Restoring correct behavior is only half of it; you also state why your completion is correct.
* Improvements are proposed and you are guided through applying them. Defending a choice with evidence counts, and so does recognizing when the critique is right.

Come prepared to account for your own commits, including AI-assisted parts, and to say why a given AI suggestion was accepted, rejected, or modified. Re-read your HackMD note beforehand, since the questions follow the stages you documented there.

Your rights in this interview, from the guidelines:
* You may decline AI-generated questions and be interviewed from questions written by the instructor instead, with no effect on your grade or your scheduling.
* Audio or video recording happens only with your consent. Declining does not affect your grade; the assessor's written record is kept either way, is shown to you on request, and is the evidence available in an appeal.
* Consent to use your code or recording in class is collected separately, after grades are final, and declining carries no consequence.
* You choose the language, from those you and the assessor share, and may switch mid-interview. You may answer at the whiteboard or in writing. Oral fluency is not a learning objective of this course and is not assessed.
* Documented accommodations are arranged in advance through the usual institutional channel.
* A missed interview for a documented reason is rescheduled, and is never scored as a failed one.
* A weak interview is a trigger for review under Section 6.1, not an automatic finding and never an automatic integrity violation.

Duration, question count, the scoring rubric, and the interview's weight in the final grade are published before the first interview and apply uniformly.

## Documentation

The HackMD note is the main written deliverable. Take [`report.md`](https://github.com/sysprog21/minirubik/blob/main/report.md) as the model for register and structure, then go past it in scope. It should carry:

* A mathematical treatment of the state space: the group $\langle R, B, D \rangle$ and its order, its Cayley graph and generators, how exhaustive BFS establishes that the HTM diameter is 11 (both that a state at depth 11 exists and that none is deeper), and what the orientation-sum constraint $\sum o_i \equiv 0 \pmod 3$ rules out. It is a modulo-3 invariant, not a parity.
* The optimization argument across the four stages above, with the reasoning that carries each stage into the next rather than a list of things you tried.
* Memory quantified rather than described: bytes per table, peak working set, and how each figure was obtained.
* The RV32I-specific work, naming the instruction sequences that matter and why the base ISA forces them.
* Analysis of your code, and of the LED matrix mapping and the pipeline walkthrough required in Phase 1.

Do not paste complete program listings into the note. The code belongs in your GitHub fork, and the note links to it. Quote only the fragments the argument turns on, a few lines at a time, and say what each one does and why it is written that way. A note that reproduces the whole program has substituted transcription for analysis.

Build the note incrementally, updating it section by section or topic by topic as the work proceeds. This is a major assignment under Section 4.2 of the AI Guidelines, so it carries evidence of incremental development: retain at least three substantive revisions showing the work developing. A single bulk paste does not erase HackMD's history, but it collapses that evidence into one coarse revision. Section 4.2 also says process evidence is never conclusive on its own and that working offline or in a burst can be legitimate, so if your history is thin for a good reason, say so and offer equivalent evidence.

Document your progress in [HackMD notes](https://hackmd.io/s/features).
* See this [example page](https://hackmd.io/@sysprog/SkkbXLJRR) for HackMD formatting only. Its content predates these rules and breaks two of them: it pastes complete listings, and its assembly uses `mul`, an M-extension instruction this assignment forbids. Copy its presentation, not its practice.
* The page must be [published](https://hackmd.io/s/how-to-publish-note) so that anyone can read it, with write permission set to Signed-in users: any signed-in HackMD user must be able to edit it, so that reviewers can annotate it directly.
* Submit the HackMD note and your fork through the submission form named under Submission; both are the assignment record.
* All writing must be in English.

AI regime: this assignment is AI-assisted under Section 3 of the [AI Guidelines for Computer Architecture (Fall 2026)](https://hackmd.io/@sysprog/arch2026-ai-guidelines), and disclosure is required under Section 4.1. These parts must be your own and not generated: the choice of state representation, the search design and its admissibility argument, every measurement you report, the optimization reasoning, the RV32I assembly, and the analysis in your note. AI may be used elsewhere, including refining your English with ChatGPT, [QuillBot](https://quillbot.com/), or similar. Working without AI is equally acceptable and carries no penalty; declare "No AI tools were used in this assignment" and Section 4.1 imposes nothing further.

## Submission

The **[submission form](https://forms.gle/2ZupDEdJyJkHM8Y6A)** accepts submissions until Oct 8, 2026, 23:59 (GMT+8). After you submit the form, an automatic email from `103b0020@gs.ncku.edu.tw` reports the result of the checks. ==Do not reply to it==. Your submission is complete only when you receive the email with the subject ending in "accepted". If the subject ends in "action required" instead, fix the issues it lists and submit the form again.

Phase 2 is scheduled after Phase 1 is submitted. Grade weights for the two phases, the bonus cap, and the late policy are fixed in the syllabus before the add/drop deadline.

BONUS:
* Provide a solver for 3×3×3 (or more complex) configurations while ensuring that its memory footprint remains within the aforementioned Ripes constraints.
* Active participation in class discussions during code or quiz reviews.
* A 3D renderer for the solver on the LED Matrix, replacing the unfolded net with a projected view of the cube that turns as the solution is applied. Fixed-point arithmetic only: RV32I has no floating point and you may not link soft-float routines. Since there is no multiply either, expect shift-and-add products, and consider a precomputed table of the 24 cube orientations over live trigonometry and a perspective divide.

## References

Each entry says what it is for and when you need it. Stage numbers refer to the Stages section above.

RISC-V and the target
* Andrew Waterman and Krste Asanović, eds., [The RISC-V Instruction Set Manual, Volume I: Unprivileged Architecture](https://github.com/riscv/riscv-isa-manual/releases/latest). Chapter 2 is "Base Instruction Sets" and covers RV64I alongside RV32I, so read the RV32I section only and treat it as the exhaustive list of opcodes you may write. Anything you find in the RV64I section is out of bounds. Use it in stage 4 to check every instruction you emit.
* RISC-V International, [RISC-V Assembly Programmer's Manual](https://github.com/riscv-non-isa/riscv-asm-manual). The ISA manual defines machine instructions, not the things you actually type. Use this in stage 4 for the register ABI names (`a0`-`a7`, `t0`-`t6`, `s0`-`s11`), the pseudo-instructions (`li`, `la`, `mv`, `j`, `ret`, `bltz`), and the assembler directives you need for `.rodata` tables and the `.if` renderer switch.
* Morten Borup Petersen, [Ripes](https://github.com/mortbopet/Ripes), and its [in-tree documentation](https://github.com/mortbopet/Ripes/tree/master/docs). The GitHub wiki is a stub that redirects here. Read `cli.md` in stage 1 for the CLI you will measure with, noting that it lags master and omits `--exectime` and the cache options; read `mmio.md` before the LED matrix work.
* [Ripes environment calls](https://github.com/mortbopet/Ripes/blob/master/docs/ecalls.md). Ripes uses MARS and SPIM style ecalls, not Linux syscall numbers: `a7=1` prints an integer, `a7=4` a string, `a7=10` or `a7=93` halts. A Linux RISC-V syscall table will not work here. Use it in stage 4, as soon as you need console output, and for the exit status your test cases report through.
* [VSRTL sparse address space](https://github.com/mortbopet/VSRTL/blob/master/include/VSRTL/core/vsrtl_addressspace.h). Read the container declaration in stage 1, before you measure: seeing that guest memory is a hash map keyed per byte is what makes the measurement you are asked to take predictable rather than surprising.
* [Example RISC-V Assembly Programs](https://marz.utk.edu/my-courses/cosc230/book/example-risc-v-assembly-programs/) and [arch-riscv-progs](https://github.com/sysprog21/arch-riscv-progs). Working assembly to read in stage 4 when you need the shape of a loop, a table lookup, or a function call, rather than the semantics of one instruction.

The state space as a graph
* Eric W. Weisstein, [Cayley Graph](https://mathworld.wolfram.com/CayleyGraph.html), MathWorld. Take the definition from here, then apply it yourself in stage 1: the object `solver.c` enumerates is the Cayley graph of the fixed-corner stabilizer $\langle R, B, D \rangle$, of order $7! \cdot 3^6 = 3{,}674{,}160$, with the nine HTM moves as generators. Getting the distinction right matters, because taking arbitrary face moves modulo the 24 whole-cube rotations gives a Schreier coset graph instead.
* Jaap Scherphuis, [Pocket Cube](https://www.jaapsch.net/puzzles/cube2.htm). The 2x2x2 distance distribution in both metrics, independent of `report.md`. Use it in stage 1 to corroborate that the diameter is 11 in the half-turn metric and 14 in the quarter-turn metric, rather than asserting it from one source.
* Antti Valmari, [What the small Rubik's cube taught me about data structures, information theory, and randomisation](https://doi.org/10.1007/s10009-005-0191-z), *International Journal on Software Tools for Technology Transfer* 8(3), 2006, 180-194. The same puzzle treated as an encoding problem. Read it in stage 2 if you need to understand the factoradic ranking `solver.c` uses, or to argue about how small a dense encoding of this state space can get.
* Riccardo Donati, [Solving a Rubik's Cube Using Graph Theory](https://medium.com/@ricdonati/solving-a-rubiks-cube-using-graph-theory-6724e9ba68ce). An informal introduction, useful only if the framing above is unfamiliar and you want it in plainer language first. Medium meters access and the group theory is loose, so do not cite it for anything you assert.

Search and heuristics
* Stuart Russell and Peter Norvig, *Artificial Intelligence: A Modern Approach*, 4th edition, Pearson, 2020. Chapter 3 gives iterative deepening, A\*, admissibility, and pattern databases as textbook material with pseudocode. Start here in stage 2; the papers below are the primary sources behind it and are harder going.
* Richard E. Korf, [Depth-First Iterative-Deepening: An Optimal Admissible Tree Search](https://doi.org/10.1016/0004-3702%2885%2990084-0), *Artificial Intelligence* 27(1), 1985, 97-109. The original IDA\*, and the argument for why bounded depth-first iteration finds an optimal solution with no queue. Paywalled off campus. Read it in stage 2 when you need the optimality argument itself rather than the algorithm's shape.
* Joseph C. Culberson and Jonathan Schaeffer, [Pattern Databases](https://doi.org/10.1111/0824-7935.00065), *Computational Intelligence* 14(3), 1998, 318-334. Why a heuristic precomputed over an abstraction of the state space is admissible. Paywalled off campus. This is the argument you adapt to discharge gate H1, so read it in stage 2, before you build a table rather than after.
* Richard E. Korf, [Finding Optimal Solutions to Rubik's Cube Using Pattern Databases](https://cdn.aaai.org/AAAI/1997/AAAI97-109.pdf), *Proceedings of the Fourteenth National Conference on Artificial Intelligence (AAAI-97)*, 700-705. Open access, and the closest published analogue to what you are being asked to do, though on the 3x3x3: the techniques transfer, the numbers do not. Read it in stage 2 for how the abstractions are chosen and combined.
* Shunyu Yao and Mitchy Lee, [Solving a Rubik's Cube Using its Local Graph Structure](https://arxiv.org/abs/2408.07945), arXiv:2408.07945, 2024. A graph convolutional network heuristic for the 3x3x3. The neural machinery is out of reach here, since it needs floating point, multiplication, and far more than 128 KiB. Read it only for the underlying idea of deriving a heuristic from a state's neighbours, and if you translate that idea, measure it rather than assuming it helps.

Arithmetic without the M extension
* Sean Eron Anderson, [Bit Twiddling Hacks](https://graphics.stanford.edu/~seander/bithacks.html). Use the shift, mask, and sign-extension idioms in stage 3. Skip the entries built on integer multiplication, 64-bit arithmetic, or floating-point punning: a large fraction of the page assumes a multiplier that RV32I does not have.
* Henry S. Warren, Jr., *Hacker's Delight*, 2nd edition, Addison-Wesley, 2012. Its chapter on multiplication by constants is the one you want in stage 3, for minimal shift-and-add sequences. Its chapter on division by constants is worth knowing about and not worth applying: that technique needs a high-half multiply, which is exactly what the base ISA denies you.
* Randal E. Bryant and David R. O'Hallaron, *Computer Systems: A Programmer's Perspective*, 3rd edition, Pearson, 2016. Section 2.3 in stage 1 and 3, for two's complement arithmetic and where it overflows. Section 3.6 for the branch-against-branchless trade-off in principle, but translate rather than copy: it is written around x86 condition codes and `cmov`, and RV32I has neither, so branchless selection here is built from `slt` and arithmetic shifts.