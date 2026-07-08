# EDA_opt timing analysis tests

These scripts exercise `my_opt_timing`, the project timing-analysis pass.

The pass is analysis-only: it does not rewrite the design. It reports:

- critical path delay
- one reconstructed critical path
- per-cell delay, arrival time, required time, and slack
- critical cells under the configured slack threshold
- endpoint ranking and low-slack cell ranking

Run from `EDA/yosys/tests/my_opt_timing` after building the Yosys binary:

```powershell
..\..\MyVSYosys\x64\Debug\MyVSYosys.exe -s run_timing_path.ys
..\..\MyVSYosys\x64\Debug\MyVSYosys.exe -s run_timing_override.ys
..\..\MyVSYosys\x64\Debug\MyVSYosys.exe -s run_timing_reg_boundary.ys
```

Useful options:

- `-clock <N>` or `-period <N>` sets endpoint required time.
- `-delay <celltype> <N>` overrides a specific RTLIL cell delay.
- `-topn <N>` controls low-slack report length.
- `-critical_slack <N>` expands the critical-node threshold.
- `-show_all` prints every timed cell.
