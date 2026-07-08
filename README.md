# EDA Optimization Workbench

An educational EDA project that connects a lightweight Verilog-to-RTLIL compiler, a customized Yosys optimization pipeline, and a small web playground for visualization and experimentation.

## Highlights

- Lightweight compiler in `compiler/` that translates a small Verilog subset into RTLIL.
- Customized Yosys passes in `yosys/passes/opt/` for algebraic, two-level, multi-level, and timing-oriented optimization.
- Browser-based demo in `web/` for running the flow and visualizing results.
- Example Verilog/RTLIL/Yosys scripts in `examples/` and focused regression cases in `yosys/tests/`.

## Project Scope

This repository is best understood as a teaching and experimentation workspace for logic optimization rather than a drop-in production synthesis toolchain.

The current flow is:

`Verilog subset -> custom compiler -> RTLIL -> custom Yosys passes -> analysis / optimized RTLIL / netlist graph`

## Repository Layout

```text
.
|-- build.ps1                # Build script for the standalone compiler
|-- compiler/                # Verilog-subset parser, IR, netlist builder, RTLIL writer
|-- examples/                # Demo inputs, RTLIL snapshots, and Yosys scripts
|-- optimizer/               # Older optimization code kept as reference material
|-- web/                     # Local web UI + Python server for visualization
`-- yosys/                   # Active Yosys source tree with custom optimization passes
```

## Main Components

### 1. Standalone Verilog-to-RTLIL Compiler

The standalone compiler is built from the sources in `compiler/` and emits RTLIL directly.

Pipeline:

`parseModule -> generateNetlist -> writeRtlil`

Supported constructs in the current implementation:

- Single module input
- `input`, `output`, `wire`, `assign`
- Width declarations such as `[3:0]`
- Constants such as decimal, binary, and hex literals
- Expressions using `~`, `&`, `|`, `^`, `+`, `-`, `*`, `<<`, `>>`, and ternary `?:`

Current limitations:

- No `always`, `reg`, `if`, `case`, `generate`, or sequential logic lowering
- Intended mainly for combinational examples and compiler/frontend experiments

### 2. Custom Yosys Optimization Passes

The customized Yosys tree lives in `yosys/`, with project-specific passes under `yosys/passes/opt/`.

Implemented passes include:

- `my_opt_expr`: constant folding, copy propagation, alias cleanup, and simple algebraic simplification
- `my_opt_strength`: strength reduction
- `my_opt_cse`: common subexpression elimination
- `my_opt_dce`: dead combinational logic elimination
- `my_opt_share`: resource sharing
- `my_opt_reduce`: logic reduction
- `my_opt_muxtree`: MUX tree simplification
- `my_opt_qm`: exact two-level minimization with Quine-McCluskey style cover solving
- `my_opt_rewrite`: multi-level logic rewriting
- `my_opt_timing`: static timing analysis and critical-path reporting

### 3. Web Playground

The `web/` folder contains a lightweight Python server and frontend that can:

- Probe compiler, Yosys, and Graphviz availability
- Run predefined optimization profiles
- Show generated RTLIL, logs, scripts, timing output, and netlist graphs
- Visualize compiler/Yosys flow results in the browser

## Quick Start

### Requirements

Recommended environment:

- Windows
- Git
- `g++` with C++17 support
- Python 3
- Graphviz for DOT rendering
- Visual Studio if you want to rebuild the customized Yosys executable

### Build the Standalone Compiler

```powershell
.\build.ps1
```

This generates:

```text
bin/verilog_rtlil_compiler.exe
```

### Run the Compiler on a Demo

```powershell
.\bin\verilog_rtlil_compiler.exe .\examples\demo.v -o .\examples\demo.il
```

### Run a Yosys Optimization Demo

Run the example scripts from inside `examples/`, because the `.ys` files use relative paths:

```powershell
Set-Location .\examples
..\MyVSYosys.exe -s .\run_my_opt_logic_demo.ys
```

Other demo scripts:

- `run_my_opt_algebra_demo.ys`
- `run_my_opt_two_level_demo.ys`
- `run_my_opt_timing_demo.ys`
- `run_my_opt_all_plugins_demo.ys`

### Launch the Web Playground

```powershell
Set-Location .\web
python .\server.py
```

Then open:

```text
http://127.0.0.1:8000
```

## Tests and Validation

Focused Yosys-side regression cases are provided in:

- `yosys/tests/my_opt_qm/`
- `yosys/tests/my_opt_multilevel/`
- `yosys/tests/my_opt_timing/`

During this README refresh, the following were re-validated locally:

- `.\build.ps1`
- `.\bin\verilog_rtlil_compiler.exe .\examples\demo.v -o .\examples\demo.il`
- `..\MyVSYosys.exe -s .\run_my_opt_logic_demo.ys` from `examples/`
- `python .\web\server.py` with `GET /api/health`

## Notes

- The root `yosys/` tree is the active maintained source tree for project-specific pass development.
- Example `.ys` scripts are stored as UTF-8 without BOM so Yosys can execute them directly.
- The standalone compiler was updated to handle Windows Unicode paths more reliably when the workspace contains non-ASCII characters.

## Roadmap Ideas

- Broaden Verilog frontend coverage beyond combinational subsets
- Add automated regression scripts for compiler and custom Yosys passes
- Add benchmark cases and before/after QoR summaries
- Improve the web UI with richer diff views and pass-by-pass visualization

## License

No license file is included yet. Add one before treating this repository as an open-source release.
