# EDA 优化工作台

这是一个面向教学与实验的 EDA 项目，围绕“Verilog 子集前端编译 + RTLIL 生成 + 定制 Yosys 优化 + Web 可视化演示”构建了一套完整工作流。

## 项目亮点

- `compiler/` 中实现了一个轻量级 Verilog 子集到 RTLIL 的独立编译器。
- `yosys/passes/opt/` 中集成了多类自定义逻辑优化与时序分析 pass。
- `web/` 提供了本地可运行的可视化演示界面，便于观察优化流程与结果。
- `examples/` 和 `yosys/tests/` 提供了示例输入、脚本和针对性测试用例。

## 项目定位

这个仓库更适合作为逻辑综合、优化算法和 EDA 流程实验平台，而不是直接面向生产环境的通用综合工具链。

当前主流程如下：

`Verilog 子集 -> 自研编译器 -> RTLIL -> 自定义 Yosys pass -> 优化结果 / 分析结果 / 网表可视化`

## 目录结构

```text
.
|-- build.ps1                # 独立编译器构建脚本
|-- compiler/                # Verilog 子集解析、IR、网表生成、RTLIL 输出
|-- examples/                # 示例输入、RTLIL 结果、Yosys 脚本
|-- optimizer/               # 较早期的优化代码，保留作参考
|-- web/                     # 本地 Web 演示界面与 Python 服务
`-- yosys/                   # 当前维护中的 Yosys 源码树与自定义优化 pass
```

## 核心模块

### 1. 独立 Verilog 到 RTLIL 编译器

独立编译器源码位于 `compiler/`，负责将一个受限 Verilog 子集直接转换为 RTLIL。

处理流程：

`parseModule -> generateNetlist -> writeRtlil`

当前已支持的语法与能力：

- 单模块输入
- `input`、`output`、`wire`、`assign`
- 位宽声明，例如 `[3:0]`
- 十进制、二进制、十六进制常量
- 表达式运算：`~`、`&`、`|`、`^`、`+`、`-`、`*`、`<<`、`>>`、三目运算 `?:`

当前限制：

- 暂不支持 `always`、`reg`、`if`、`case`、`generate` 等时序/过程化语法
- 当前更适合组合逻辑示例、前端实验与 RTLIL 生成验证

### 2. 自定义 Yosys 优化 Pass

定制版 Yosys 位于 `yosys/`，项目相关的 pass 主要放在 `yosys/passes/opt/`。

目前包含的主要 pass 有：

- `my_opt_expr`：常量折叠、拷贝传播、别名清理、基础代数化简
- `my_opt_strength`：强度削减
- `my_opt_cse`：公共子表达式消除
- `my_opt_dce`：死组合逻辑删除
- `my_opt_share`：资源共享
- `my_opt_reduce`：规约逻辑优化
- `my_opt_muxtree`：MUX 树化简
- `my_opt_qm`：基于 Quine-McCluskey 思路的精确两级逻辑最小化
- `my_opt_rewrite`：多级逻辑重写
- `my_opt_timing`：静态时序分析与关键路径报告

### 3. Web 演示界面

`web/` 中的 Python 服务与前端页面可以完成以下工作：

- 探测编译器、Yosys、Graphviz 是否可用
- 按预设优化配置运行流程
- 展示 RTLIL、Yosys 脚本、日志、时序分析结果和网表图
- 在浏览器中直观对比编译与优化结果

## 快速开始

### 环境要求

建议环境如下：

- Windows
- Git
- 支持 C++17 的 `g++`
- Python 3
- Graphviz（用于 DOT 图渲染）
- 如果需要重新构建定制 Yosys，可安装 Visual Studio

### 构建独立编译器

```powershell
.\build.ps1
```

生成产物：

```text
bin/verilog_rtlil_compiler.exe
```

### 运行编译器示例

```powershell
.\bin\verilog_rtlil_compiler.exe .\examples\demo.v -o .\examples\demo.il
```

### 运行 Yosys 优化示例

示例 `.ys` 脚本使用的是相对路径，因此建议先进入 `examples/` 目录再执行：

```powershell
Set-Location .\examples
..\MyVSYosys.exe -s .\run_my_opt_logic_demo.ys
```

其他可直接运行的示例脚本：

- `run_my_opt_algebra_demo.ys`
- `run_my_opt_two_level_demo.ys`
- `run_my_opt_timing_demo.ys`
- `run_my_opt_all_plugins_demo.ys`

### 启动 Web 演示

```powershell
Set-Location .\web
python .\server.py
```

然后在浏览器打开：

```text
http://127.0.0.1:8000
```

## 测试与验证

与自定义 Yosys pass 对应的测试样例位于：

- `yosys/tests/my_opt_qm/`
- `yosys/tests/my_opt_multilevel/`
- `yosys/tests/my_opt_timing/`

这次 README 整理过程中，已经在本地重新验证过以下命令：

- `.\build.ps1`
- `.\bin\verilog_rtlil_compiler.exe .\examples\demo.v -o .\examples\demo.il`
- 在 `examples/` 目录下执行 `..\MyVSYosys.exe -s .\run_my_opt_logic_demo.ys`
- `python .\web\server.py` 与 `GET /api/health`

## 说明

- 根目录下的 `yosys/` 是当前项目实际维护和继续开发的 Yosys 源码树。
- 示例 `.ys` 文件已经整理为 UTF-8 无 BOM，避免 Yosys 首行命令解析异常。
- 独立编译器已经额外处理了 Windows 下非 ASCII 路径问题，因此在中文目录中运行更稳定。

## 后续可扩展方向

- 扩展 Verilog 前端支持范围，不再局限于组合逻辑子集
- 增加自动化回归脚本，覆盖编译器和自定义 pass
- 引入 benchmark，并统计优化前后 QoR 指标
- 增强 Web 界面的逐步优化展示与结果对比能力

## 许可证

当前仓库还没有附带 `LICENSE` 文件。如果要按正式开源项目方式发布，建议补充许可证后再公开。
