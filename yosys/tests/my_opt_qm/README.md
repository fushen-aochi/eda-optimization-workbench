# EDA_opt two-level logic tests

These tests verify the project two-level logic passes:

- `my_opt_qm`: exact Quine-McCluskey minimization with branch-and-bound cover
  selection.
- `my_opt_cover`: cube-matrix heuristic minimization with expand,
  irredundant, and reduce operations.

Run from `EDA/yosys/tests/my_opt_qm` after building the Yosys binary that
includes `passes/opt/my_opt_qm.cc`:

```powershell
..\..\MyVSYosys\x64\Debug\MyVSYosys.exe -s run_qm_majority.ys
..\..\MyVSYosys\x64\Debug\MyVSYosys.exe -s run_qm_xor_cover.ys
..\..\MyVSYosys\x64\Debug\MyVSYosys.exe -s run_qm_mux_arith.ys
..\..\MyVSYosys\x64\Debug\MyVSYosys.exe -s run_cover_expand_irredundant.ys
..\..\MyVSYosys\x64\Debug\MyVSYosys.exe -s run_two_level_full.ys
```

Each script keeps a `gold` copy, optimizes a `gate` copy, and checks equivalence
with `equiv_status -assert`.
