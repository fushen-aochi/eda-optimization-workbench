# EDA_opt logic synthesis passes

This directory contains project-owned Yosys optimization passes. The integration
model is Yosys-first: optimization algorithms live as Yosys passes under
`passes/opt`, and the web UI may invoke Yosys scripts that run these passes and
display the results.

## Two-level Logic Optimization

`my_opt_qm.cc` registers:

- `my_opt_qm`: exact Quine-McCluskey two-level minimization for supported
  combinational module output cones. It enumerates truth tables, computes prime
  implicants, simplifies the prime implicant chart with essential-prime and
  dominance reductions, solves the remaining cover by branch-and-bound, and
  rebuilds SOP logic only when cost improves.
- `my_opt_cover`: heuristic two-level cover minimization for one-bit
  `$and/$or/$not` SOP networks. It builds a cube matrix, computes the exact
  original ON-set within `-max_vars`, and iterates
  `EXPAND -> IRREDUNDANT -> REDUCE` with exact coverage checks.

## Multi-level Logic Optimization

`my_opt_rewrite.cc` registers:

- `my_opt_kernel`: enumerates cube-free kernels and co-kernels within
  `-max_kernel_literals`, then applies the best positive-gain factorization.
- `my_opt_algdiv`: performs exact algebraic division by generated
  kernel/co-kernel divisors and rebuilds `quotient * divisor + remainder`.
- `my_opt_rewrite`: iterates the same kernel and algebraic-division engine as a
  heuristic multi-level rewriting pass.

This follows the PPT multi-level section: algebraic division, kernel/co-kernel
generation, matrix-style cube support enumeration, and positive-gain
multi-level rewriting.

## Timing Analysis

`my_opt_timing.cc` registers:

- `my_opt_timing`: builds a combinational timing graph, computes arrival time,
  required time, slack, low-slack endpoints, critical nodes under a configured
  slack threshold, and one reconstructed critical path. Sequential cells are
  treated as timing boundaries.

The delay model follows the PPT estimation idea: standard cells can be given by
explicit delay overrides, and otherwise delays are estimated by built-in
operation complexity, width, and fanout. Use `-clock` or `-period` for endpoint
required time, `-default_delay` for fallback delay, `-delay <celltype> <N>` for
cell-specific overrides, `-topn` for report length, `-critical_slack` for the
critical-node threshold, and `-show_all` for a full cell table.

## Verification

Two-level tests live in `tests/my_opt_qm`.

Multi-level tests live in `tests/my_opt_multilevel`.

Timing-analysis tests live in `tests/my_opt_timing`.

The rewriting tests keep a `gold` copy, optimize a `gate` copy, and run:

```yosys
equiv_make gold gate equiv
prep -top equiv
equiv_simple
equiv_status -assert
```

Build a Yosys binary containing `my_opt_qm.cc`, `my_opt_rewrite.cc`, and
`my_opt_timing.cc`, then run the scripts from their test directories.

The timing scripts are analysis-only: they run `my_opt_timing` and verify the
reported critical path, arrival time, required time, and slack.
