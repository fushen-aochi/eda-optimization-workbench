from __future__ import annotations

import base64
import ctypes
import json
import mimetypes
import os
import re
import shutil
import subprocess
import tempfile
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse


WEB_ROOT = Path(__file__).resolve().parent
EDA_ROOT = WEB_ROOT.parent
COMPILER = EDA_ROOT / "bin" / "verilog_rtlil_compiler.exe"

YOSYS_CANDIDATES = [
    EDA_ROOT / "MyVSYosys.exe",
    EDA_ROOT / "yosys" / "yosys.exe",
    EDA_ROOT / "yosys" / "x64" / "Debug" / "MyVSYosys.exe",
    EDA_ROOT / "yosys" / "x64" / "Release" / "MyVSYosys.exe",
    EDA_ROOT / "yosys" / "MyVSYosys" / "x64" / "Debug" / "MyVSYosys.exe",
    EDA_ROOT / "yosys" / "MyVSYosys" / "x64" / "Release" / "MyVSYosys.exe",
    EDA_ROOT / "yosys" / "MyVSYosys" / "Debug" / "MyVSYosys.exe",
    EDA_ROOT / "yosys" / "MyVSYosys" / "Release" / "MyVSYosys.exe",
]

GRAPHVIZ_CANDIDATES = [
    Path(r"C:\Program Files\Graphviz\bin\dot.exe"),
    Path(r"C:\Program Files (x86)\Graphviz\bin\dot.exe"),
]

if os.name == "nt":
    SEM_FAILCRITICALERRORS = 0x0001
    SEM_NOGPFAULTERRORBOX = 0x0002
    CREATE_NO_WINDOW = 0x08000000
    ctypes.windll.kernel32.SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)
else:
    CREATE_NO_WINDOW = 0

ALGORITHMS: dict[str, dict[str, str]] = {
    "expr": {"label": "常量折叠 / 拷贝传播与别名传播 / 代数化简与窥孔优化", "group": "基础优化", "command": "my_opt_expr"},
    "strength": {"label": "强度削减", "group": "基础优化", "command": "my_opt_strength"},
    "cse": {"label": "公共子表达式消除 CSE / 信号去重合并", "group": "基础优化", "command": "my_opt_cse"},
    "dce": {"label": "死代码消除 DCE / 冗余逻辑删除", "group": "基础优化", "command": "my_opt_dce"},
    "cleanup": {"label": "悬空线网清理", "group": "基础优化", "command": "opt_clean"},
    "share": {"label": "资源共享", "group": "基础优化", "command": "my_opt_share"},
    "reduce": {"label": "规约逻辑优化", "group": "两级逻辑优化", "command": "my_opt_reduce"},
    "muxtree": {"label": "MUX 树化简", "group": "两级逻辑优化", "command": "my_opt_muxtree"},
    "cover": {"label": "启发式覆盖最小化", "group": "两级逻辑优化", "command": "my_opt_cover -max_vars 12 -max_iter 16"},
    "qm": {"label": "两级逻辑最小化", "group": "两级逻辑优化", "command": "my_opt_qm -max_vars 8"},
    "rewrite": {"label": "逻辑重写 / 多级逻辑优化", "group": "多级逻辑优化", "command": "my_opt_rewrite"},
    "timing": {"label": "静态时序分析", "group": "时序分析", "command": "my_opt_timing -clock 8 -topn 8 -critical_slack 1"},
}

BASIC_ALGORITHMS = ["expr", "strength", "cse", "dce", "cleanup", "share"]
TWO_LEVEL_ALGORITHMS = BASIC_ALGORITHMS + ["reduce", "muxtree", "cover", "qm"]
MULTI_LEVEL_ALGORITHMS = TWO_LEVEL_ALGORITHMS + ["rewrite"]
FULL_ALGORITHMS = MULTI_LEVEL_ALGORITHMS + ["timing"]

PROFILES: dict[str, dict[str, Any]] = {
    "compiler": {
        "label": "自研编译流程",
        "description": "",
        "algorithmIds": [],
    },
    "basic": {
        "label": "基础优化",
        "description": "",
        "algorithmIds": BASIC_ALGORITHMS,
    },
    "two_level": {
        "label": "两级逻辑优化",
        "description": "",
        "algorithmIds": TWO_LEVEL_ALGORITHMS,
    },
    "multi_level": {
        "label": "多级逻辑优化",
        "description": "",
        "algorithmIds": MULTI_LEVEL_ALGORITHMS,
    },
    "timing": {
        "label": "时序分析",
        "description": "",
        "algorithmIds": ["timing"],
    },
    "full": {
        "label": "完整演示流程",
        "description": "",
        "algorithmIds": FULL_ALGORITHMS,
    },
}

for profile in PROFILES.values():
    profile["passes"] = [ALGORITHMS[item]["command"] for item in profile["algorithmIds"]]

EXAMPLES: dict[str, str] = {
    "logic": """module logic_demo(a,b,c,d,y);
input a,b,c,d;
output y;
wire n1,n2,n3;
assign n1 = a & b;
assign n2 = a & c;
assign n3 = n1 | n2;
assign y = n3 | (d & 0);
endmodule
""",
    "arith": """module arith_demo(a,b,y);
input [3:0] a,b;
output [7:0] y;
wire [7:0] p;
assign p = a * 5;
assign y = (p + 0) ^ 0;
endmodule
""",
    "two_level": """module two_level_demo(a,b,c,d,y);
input a,b,c,d;
output y;
wire p1,p2,p3,p4;
assign p1 = a & b;
assign p2 = a & c;
assign p3 = a & d;
assign p4 = b & c;
assign y = p1 | p2 | p3 | p4;
endmodule
""",
    "timing": """module timing_demo(a,b,c,d,y);
input [3:0] a,b;
input c,d;
output [3:0] y;
wire [3:0] s,t,u;
assign s = a + b;
assign t = s ^ {4{c}};
assign u = t + {3'b000,d};
assign y = u & 4'b1111;
endmodule
""",
}


def find_first(paths: list[Path]) -> Path | None:
    for path in paths:
        if path.exists():
            return path
    return None


def find_executable(name: str) -> Path | None:
    found = shutil.which(name)
    return Path(found) if found else None


def yosys_path() -> Path | None:
    return find_first(YOSYS_CANDIDATES) or find_executable("yosys")


def graphviz_path() -> Path | None:
    return find_first(GRAPHVIZ_CANDIDATES) or find_executable("dot")


def run_command(command: list[str], cwd: Path, timeout: int = 60) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        creationflags=CREATE_NO_WINDOW,
    )


def probe_yosys(exe: Path | None) -> dict[str, Any]:
    if exe is None:
        return {"found": False, "runnable": False, "message": "未找到 Yosys 可执行文件。"}
    if os.environ.get("EDA_WEB_PROBE_YOSYS", "0") != "1":
        return {"found": True, "runnable": True, "message": "已找到 Yosys，运行流程时再执行探测。"}
    try:
        result = run_command([str(exe), "-V"], EDA_ROOT, timeout=10)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"found": True, "runnable": False, "message": f"Yosys 启动异常: {exc}"}
    if result.returncode == 0:
        version = (result.stdout or result.stderr).strip().splitlines()
        return {"found": True, "runnable": True, "message": version[0] if version else "Yosys 可运行。"}
    if result.returncode in (3221225781, -1073741515):
        return {"found": True, "runnable": False, "message": "Yosys 缺少 Visual C++ 运行库或依赖 DLL。"}
    return {"found": True, "runnable": False, "message": f"Yosys 启动失败，退出码 {result.returncode}。"}


def probe_custom_passes(exe: Path | None) -> dict[str, Any]:
    if exe is None:
        return {"available": False, "message": "未找到 Yosys。"}
    try:
        result = run_command([str(exe), "-Q", "-p", "help my_opt_expr"], EDA_ROOT, timeout=10)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"available": False, "message": f"my_opt_expr 探测失败: {exc}", "diagnostics": [str(exc)]}
    text = (result.stdout or "") + "\n" + (result.stderr or "")
    if result.returncode == 0 and "my_opt_expr" in text and "No such command" not in text:
        return {"available": True, "prefix": "my_opt", "message": "已集成自定义优化算法。"}
    return {
        "available": False,
        "prefix": "",
        "message": "当前 Yosys 未集成自定义优化算法；禁止使用原生 Yosys 兜底。",
        "diagnostics": [text.strip() or "my_opt_expr: No such command"],
    }


def write_text(path: Path, text: str) -> None:
    path.write_text(text.replace("\r\n", "\n").replace("\r", "\n"), encoding="utf-8")


def render_dot_svg(dot_text: str) -> str:
    dot = graphviz_path()
    if dot is None or not dot_text.strip():
        return ""
    try:
        result = subprocess.run(
            [str(dot), "-Tsvg"],
            input=dot_text,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=20,
            creationflags=CREATE_NO_WINDOW,
        )
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return result.stdout if result.returncode == 0 else ""


def render_dot_png_data_uri(dot_text: str) -> str:
    dot = graphviz_path()
    if dot is None or not dot_text.strip():
        return ""
    try:
        result = subprocess.run(
            [str(dot), "-Tpng"],
            input=dot_text.encode("utf-8"),
            capture_output=True,
            timeout=20,
            creationflags=CREATE_NO_WINDOW,
        )
    except (OSError, subprocess.TimeoutExpired):
        return ""
    if result.returncode != 0 or not result.stdout:
        return ""
    return "data:image/png;base64," + base64.b64encode(result.stdout).decode("ascii")


def extract_node_delays(source: str) -> dict[str, int]:
    delays: dict[str, int] = {}
    pattern = re.compile(r"(?:/\*|//)\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*delay\s*=\s*(\d+)", re.IGNORECASE)
    for match in pattern.finditer(source):
        delays[match.group(1)] = int(match.group(2))
    return delays


def apply_timing_node_delays(passes: list[str], node_delays: dict[str, int]) -> list[str]:
    if not node_delays:
        return passes
    args = "".join(f" -node_delay {name} {delay}" for name, delay in sorted(node_delays.items()))
    return [step + args if step.startswith("my_opt_timing") else step for step in passes]


def allowed_algorithm_ids(profile: str) -> list[str]:
    selected = PROFILES.get(profile, PROFILES["full"])
    return list(selected.get("algorithmIds", []))


def normalize_algorithm_ids(profile: str, requested: Any) -> list[str]:
    allowed = allowed_algorithm_ids(profile)
    if not allowed:
        return []
    if not isinstance(requested, list):
        return allowed
    requested_ids = [item for item in requested if isinstance(item, str)]
    return [item for item in allowed if item in requested_ids]


def algorithm_commands(profile: str, requested: Any = None) -> list[str]:
    ids = normalize_algorithm_ids(profile, requested)
    commands = [ALGORITHMS[item]["command"] for item in ids]
    if any(item in ids for item in ("cover", "qm", "rewrite")):
        if "dce" in ids:
            commands.append(ALGORITHMS["dce"]["command"])
        if "cleanup" in ids:
            commands.append(ALGORITHMS["cleanup"]["command"])
    return commands


def build_yosys_script(input_path: Path, output_prefix: Path, profile: str, source: str = "", algorithms: Any = None) -> str:
    selected = PROFILES.get(profile, PROFILES["full"])
    selected_passes = apply_timing_node_delays(algorithm_commands(profile, algorithms), extract_node_delays(source))
    before = output_prefix.with_name("before.il")
    after = output_prefix.with_name("after.il")
    dot_prefix = output_prefix.with_name("netlist")
    lines = [
        f"read_verilog {input_path.as_posix()}",
        "hierarchy -check -auto-top",
        "proc",
        "stat",
        f"write_rtlil {before.as_posix()}",
    ]
    lines.extend(selected_passes)
    lines.extend(
        [
            "stat",
            f"write_rtlil {after.as_posix()}",
            "select *",
            f"show -format dot -prefix {dot_prefix.as_posix()}",
        ]
    )
    return "\n".join(lines) + "\n"


def build_compiler_graph_script(rtlil_path: Path, output_prefix: Path) -> str:
    dot_prefix = output_prefix.with_name("compiler_netlist")
    return "\n".join(
        [
            f"read_rtlil {rtlil_path.as_posix()}",
            "hierarchy -check -auto-top",
            "stat",
            "select *",
            f"show -format dot -prefix {dot_prefix.as_posix()}",
        ]
    ) + "\n"


def extract_stat_blocks(log_text: str) -> list[dict[str, Any]]:
    blocks: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    cell_line = re.compile(r"^\s*(\$_?[A-Za-z0-9_.$]+)\s+(\d+)\s*$")
    for line in log_text.splitlines():
        stripped = line.strip()
        if stripped.startswith("===") and stripped.endswith("==="):
            if current:
                blocks.append(current)
            current = {"module": stripped.strip("= ").strip(), "cells": {}, "wires": None, "bits": None}
            continue
        if current is None:
            continue
        if stripped.startswith("Number of wires:"):
            current["wires"] = int(stripped.rsplit(" ", 1)[-1])
        elif stripped.startswith("Number of wire bits:"):
            current["bits"] = int(stripped.rsplit(" ", 1)[-1])
        else:
            match = cell_line.match(line)
            if match:
                current["cells"][match.group(1)] = int(match.group(2))
    if current:
        blocks.append(current)
    return blocks


def summarize_stats(blocks: list[dict[str, Any]]) -> dict[str, Any]:
    if not blocks:
        return {"before": {}, "after": {}, "deltaCells": 0, "deltaPercent": 0}
    before = blocks[0]
    after = blocks[-1]
    before_cells = sum(before.get("cells", {}).values())
    after_cells = sum(after.get("cells", {}).values())
    delta = before_cells - after_cells
    return {
        "before": {**before, "cellTotal": before_cells},
        "after": {**after, "cellTotal": after_cells},
        "deltaCells": delta,
        "deltaPercent": round(delta / before_cells * 100, 2) if before_cells else 0,
    }


def extract_timing(log_text: str) -> dict[str, Any]:
    lines = []
    for line in log_text.splitlines():
        lower = line.lower()
        if "slack" in lower or "critical" in lower or "arrival" in lower or "required" in lower:
            lines.append(line.strip())
    return {"lines": lines[:40]}


def compile_with_local_compiler(source: str, work_dir: Path) -> dict[str, Any]:
    input_path = work_dir / "input.v"
    output_path = work_dir / "compiler.il"
    write_text(input_path, source)
    if not COMPILER.exists():
        return {"available": False, "ok": False, "message": f"未找到自研编译器: {COMPILER}", "rtlil": ""}
    try:
        result = run_command([str(COMPILER), str(input_path), "-o", str(output_path)], EDA_ROOT, timeout=30)
    except subprocess.TimeoutExpired:
        return {"available": True, "ok": False, "message": "自研编译器执行超时。", "rtlil": ""}
    rtlil = output_path.read_text(encoding="utf-8", errors="replace") if output_path.exists() else ""
    return {
        "available": True,
        "ok": result.returncode == 0,
        "message": "自研编译完成。" if result.returncode == 0 else "自研编译失败。",
        "stdout": result.stdout,
        "stderr": result.stderr,
        "returncode": result.returncode,
        "rtlil": rtlil,
    }


def failed_yosys_payload(message: str, script: str, exe: Path | None, diagnostics: list[str] | None = None) -> dict[str, Any]:
    return {
        "available": exe is not None,
        "ok": False,
        "message": message,
        "executable": str(exe) if exe else "",
        "customPasses": False,
        "passDiagnostic": message,
        "script": script,
        "stdout": "",
        "stderr": "\n".join(diagnostics or []),
        "returncode": 127,
        "beforeRtlil": "",
        "afterRtlil": "",
        "json": "",
        "dot": "",
        "netlistSvg": "",
        "netlistPng": "",
        "stats": {"before": {}, "after": {}, "deltaCells": 0, "deltaPercent": 0},
        "timing": {"lines": []},
    }


def run_yosys(source: str, profile: str, work_dir: Path, algorithms: Any = None) -> dict[str, Any]:
    exe = yosys_path()
    input_path = work_dir / "yosys_input.v"
    output_prefix = work_dir / "out"
    script = build_yosys_script(input_path, output_prefix, profile, source, algorithms)
    if exe is None:
        return failed_yosys_payload("未找到 Yosys，无法执行自定义优化算法。", script, exe)
    custom_passes = probe_custom_passes(exe)
    if not custom_passes["available"]:
        return failed_yosys_payload(custom_passes["message"], script, exe, custom_passes.get("diagnostics", []))

    write_text(input_path, source)
    script_path = work_dir / "run.ys"
    write_text(script_path, script)
    try:
        result = run_command([str(exe), "-s", str(script_path)], work_dir, timeout=90)
    except subprocess.TimeoutExpired:
        return failed_yosys_payload("Yosys 执行超时。", script, exe)

    before = work_dir / "before.il"
    after = work_dir / "after.il"
    dot_path = work_dir / "netlist.dot"
    dot_text = dot_path.read_text(encoding="utf-8", errors="replace") if dot_path.exists() else ""
    log_text = result.stdout + "\n" + result.stderr
    message = "Yosys 优化流程完成。" if result.returncode == 0 else "Yosys 优化流程失败。"
    if result.returncode in (3221225781, -1073741515):
        message = "Yosys 启动失败：可执行文件缺少 Visual C++ 运行库或依赖 DLL。"

    return {
        "available": True,
        "ok": result.returncode == 0,
        "message": message,
        "executable": str(exe),
        "customPasses": True,
        "passDiagnostic": custom_passes["message"],
        "script": script,
        "stdout": result.stdout,
        "stderr": result.stderr,
        "returncode": result.returncode,
        "beforeRtlil": before.read_text(encoding="utf-8", errors="replace") if before.exists() else "",
        "afterRtlil": after.read_text(encoding="utf-8", errors="replace") if after.exists() else "",
        "json": "",
        "dot": dot_text,
        "netlistSvg": render_dot_svg(dot_text),
        "netlistPng": render_dot_png_data_uri(dot_text),
        "stats": summarize_stats(extract_stat_blocks(log_text)),
        "timing": extract_timing(log_text),
    }


def render_compiler_netlist(compiler: dict[str, Any], work_dir: Path) -> dict[str, Any]:
    exe = yosys_path()
    rtlil_path = work_dir / "compiler.il"
    output_prefix = work_dir / "compiler_out"
    script = build_compiler_graph_script(rtlil_path, output_prefix)
    if exe is None:
        return failed_yosys_payload("未找到 Yosys，无法将自研 RTLIL 渲染为真实网表图。", script, exe)
    if not compiler.get("ok") or not rtlil_path.exists():
        return failed_yosys_payload("自研编译未生成 RTLIL，无法生成网表图。", script, exe)

    script_path = work_dir / "compiler_graph.ys"
    write_text(script_path, script)
    try:
        result = run_command([str(exe), "-s", str(script_path)], work_dir, timeout=60)
    except subprocess.TimeoutExpired:
        return failed_yosys_payload("自研 RTLIL 网表图生成超时。", script, exe)

    dot_path = work_dir / "compiler_netlist.dot"
    dot_text = dot_path.read_text(encoding="utf-8", errors="replace") if dot_path.exists() else ""
    log_text = result.stdout + "\n" + result.stderr
    return {
        "available": True,
        "ok": result.returncode == 0 and bool(dot_text.strip()),
        "message": "自研 RTLIL 网表图生成完成。" if result.returncode == 0 and dot_text.strip() else "自研 RTLIL 网表图生成失败。",
        "executable": str(exe),
        "customPasses": False,
        "passDiagnostic": "自研编译流程只借助 Yosys show 渲染 RTLIL，不执行 Yosys 优化 pass。",
        "script": script,
        "stdout": result.stdout,
        "stderr": result.stderr,
        "returncode": result.returncode,
        "beforeRtlil": compiler.get("rtlil", ""),
        "afterRtlil": compiler.get("rtlil", ""),
        "json": "",
        "dot": dot_text,
        "netlistSvg": render_dot_svg(dot_text),
        "netlistPng": render_dot_png_data_uri(dot_text),
        "stats": summarize_stats(extract_stat_blocks(log_text)),
        "timing": extract_timing(log_text),
    }


def run_flow(source: str, profile: str, algorithms: Any = None) -> dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix="eda_web_", dir=str(WEB_ROOT)) as temp_dir:
        work_dir = Path(temp_dir)
        compiler = compile_with_local_compiler(source, work_dir)
        yosys = render_compiler_netlist(compiler, work_dir) if profile == "compiler" else run_yosys(source, profile, work_dir, algorithms)
    ok = bool(compiler.get("ok") and yosys.get("ok")) if profile == "compiler" else bool(yosys.get("ok"))
    return {
        "ok": ok,
        "profile": profile if profile in PROFILES else "full",
        "profileLabel": PROFILES.get(profile, PROFILES["full"])["label"],
        "selectedAlgorithms": normalize_algorithm_ids(profile, algorithms),
        "compiler": compiler,
        "yosys": yosys,
    }


def health_payload() -> dict[str, Any]:
    yp = yosys_path()
    gp = graphviz_path()
    yosys_probe = probe_yosys(yp)
    custom_passes = probe_custom_passes(yp) if yosys_probe["runnable"] else {"available": False, "message": "Yosys 不可运行。"}
    return {
        "ok": True,
        "paths": {
            "edaRoot": str(EDA_ROOT),
            "compiler": str(COMPILER),
            "yosys": str(yp) if yp else "",
            "graphviz": str(gp) if gp else "",
        },
        "available": {
            "compiler": COMPILER.exists(),
            "yosys": yosys_probe["runnable"],
            "yosysFound": yosys_probe["found"],
            "edaPasses": custom_passes["available"],
            "graphviz": gp is not None,
        },
        "diagnostics": {
            "yosys": yosys_probe["message"],
            "edaPasses": custom_passes["message"],
        },
        "profiles": PROFILES,
        "algorithms": ALGORITHMS,
        "examples": EXAMPLES,
    }


def json_bytes(payload: dict[str, Any]) -> bytes:
    return json.dumps(payload, ensure_ascii=False).encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/api/health":
            self.send_json(HTTPStatus.OK, health_payload())
            return
        self.serve_static(parsed.path)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path != "/api/run":
            self.send_json(HTTPStatus.NOT_FOUND, {"ok": False, "message": "未知 API"})
            return
        try:
            raw = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            payload = json.loads(raw.decode("utf-8"))
        except (ValueError, json.JSONDecodeError):
            self.send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "message": "请求体不是合法 JSON"})
            return
        source = payload.get("source", "")
        profile = payload.get("profile", "full")
        algorithms = payload.get("algorithms")
        if not isinstance(source, str) or not source.strip():
            self.send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "message": "Verilog 源码不能为空"})
            return
        if not isinstance(profile, str) or profile not in PROFILES:
            profile = "full"
        result = run_flow(source, profile, algorithms)
        self.send_json(HTTPStatus.OK if result["ok"] else HTTPStatus.BAD_REQUEST, result)

    def serve_static(self, path: str) -> None:
        relative = path.lstrip("/") or "index.html"
        file_path = (WEB_ROOT / relative).resolve()
        try:
            file_path.relative_to(WEB_ROOT)
        except ValueError:
            self.send_error(HTTPStatus.FORBIDDEN)
            return
        if not file_path.is_file():
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        body = file_path.read_bytes()
        mime, _ = mimetypes.guess_type(str(file_path))
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", f"{mime or 'application/octet-stream'}; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, status: HTTPStatus, payload: dict[str, Any]) -> None:
        body = json_bytes(payload)
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"{self.address_string()} - {fmt % args}")


def main() -> None:
    host = os.environ.get("EDA_WEB_HOST", "127.0.0.1")
    port = int(os.environ.get("EDA_WEB_PORT", "8000"))
    server = ThreadingHTTPServer((host, port), Handler)
    print(f"EDA Web 工作台: http://{host}:{port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
