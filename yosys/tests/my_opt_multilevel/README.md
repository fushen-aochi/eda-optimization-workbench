# EDA_opt multi-level logic tests

These tests verify the multi-level optimization passes implemented in
`passes/opt/my_opt_rewrite.cc`:

- `my_opt_kernel`: recursive-style exact kernel/co-kernel enumeration within the
  configured support bound.
- `my_opt_algdiv`: algebraic division by generated divisors and reconstruction
  as quotient * divisor + remainder.
- `my_opt_rewrite`: iterative heuristic multi-level rewriting using the same
  kernel and algebraic division engine.

Run from `EDA/yosys/tests/my_opt_multilevel` after building the Yosys binary:

```powershell
..\..\MyVSYosys\x64\Debug\MyVSYosys.exe -s run_kernel_example.ys
..\..\MyVSYosys\x64\Debug\MyVSYosys.exe -s run_algdiv_example.ys
..\..\MyVSYosys\x64\Debug\MyVSYosys.exe -s run_rewrite_pos_example.ys
```

Each script keeps a `gold` copy, optimizes a `gate` copy, and checks equivalence
with `equiv_status -assert`.
