const profileSelect = document.getElementById("profileSelect");
const sourceInput = document.getElementById("sourceInput");
const runBtn = document.getElementById("runBtn");
const passList = document.getElementById("passList");
const messageBox = document.getElementById("messageBox");
const compilerStatus = document.getElementById("compilerStatus");
const yosysStatus = document.getElementById("yosysStatus");
const graphvizStatus = document.getElementById("graphvizStatus");
const cellMetric = document.getElementById("cellMetric");
const gainMetric = document.getElementById("gainMetric");
const flowMetric = document.getElementById("flowMetric");
const textOutput = document.getElementById("textOutput");
const graphOutput = document.getElementById("graphOutput");
const tabs = Array.from(document.querySelectorAll(".tab"));

let health = null;
let lastResult = null;
let activeView = "summary";

function stateText(available) {
  return available ? ["可用", "ok"] : ["缺失", "bad"];
}

function setStatus(el, available, label) {
  const [text, cls] = stateText(available);
  el.textContent = label || text;
  el.className = cls;
}

function textOrEmpty(value, fallback) {
  return value && String(value).trim() ? String(value) : fallback;
}

function selectedProfile() {
  return profileSelect.value || "full";
}

function selectedAlgorithms() {
  return Array.from(passList.querySelectorAll('input[type="checkbox"]:checked')).map((item) => item.value);
}

function renderPassList() {
  if (!health) return;
  const profile = health.profiles[selectedProfile()] || health.profiles.full;
  passList.innerHTML = "";
  const ids = profile.algorithmIds || [];
  if (!ids.length) {
    const item = document.createElement("div");
    item.className = "algorithm-empty";
    item.textContent = "parseModule / generateNetlist / writeRtlil / renderNetlist";
    passList.appendChild(item);
    return;
  }

  let currentGroup = "";
  ids.forEach((id) => {
    const info = health.algorithms?.[id] || { label: id, group: "算法", command: id };
    if (info.group !== currentGroup) {
      currentGroup = info.group;
      const group = document.createElement("div");
      group.className = "algorithm-group";
      group.textContent = currentGroup;
      passList.appendChild(group);
    }

    const label = document.createElement("label");
    label.className = "algorithm-item";

    const input = document.createElement("input");
    input.type = "checkbox";
    input.value = id;
    input.checked = true;

    const text = document.createElement("span");
    text.className = "algorithm-text";
    text.textContent = info.label || id;

    label.append(input, text);
    passList.appendChild(label);
  });
}

function fillSelects() {
  profileSelect.innerHTML = "";
  Object.entries(health.profiles).forEach(([key, value]) => {
    const opt = document.createElement("option");
    opt.value = key;
    opt.textContent = value.label;
    if (key === "full") opt.selected = true;
    profileSelect.appendChild(opt);
  });

  sourceInput.value = health.examples.logic || "";
}

async function loadHealth() {
  try {
    const res = await fetch("/api/health");
    health = await res.json();
    setStatus(compilerStatus, health.available.compiler);
    const yosysLabel = health.available.yosys ? "可用" : (health.available.yosysFound ? "缺依赖" : "缺失");
    setStatus(yosysStatus, health.available.yosys, yosysLabel);
    setStatus(graphvizStatus, health.available.graphviz);
    fillSelects();
    renderPassList();
    messageBox.textContent = health.available.yosys
      ? (health.available.edaPasses ? "环境可用，可以运行自定义优化算法。" : "当前 Yosys 未集成自定义优化算法，优化流程会失败。")
      : (health.diagnostics?.yosys || "Yosys 不可用。");
    textOutput.textContent = "选择流程和用例后点击“运行演示”。";
  } catch (error) {
    messageBox.textContent = "后端未连接";
    compilerStatus.textContent = "-";
    yosysStatus.textContent = "-";
    graphvizStatus.textContent = "-";
    textOutput.textContent = String(error);
  }
}

function renderMetrics(result) {
  const stats = result?.yosys?.stats;
  if (stats?.before?.cellTotal !== undefined) {
    cellMetric.textContent = `${stats.before.cellTotal} -> ${stats.after.cellTotal}`;
    gainMetric.textContent = `${stats.deltaPercent}%`;
  } else {
    cellMetric.textContent = "-";
    gainMetric.textContent = "-";
  }
  const compilerOk = Boolean(result?.compiler?.ok);
  const yosysOk = Boolean(result?.yosys?.ok);
  const requiresPasses = result?.profile !== "compiler";
  const status = requiresPasses
    ? (yosysOk ? "完成" : "失败")
    : (compilerOk && yosysOk ? "完成" : (compilerOk || yosysOk ? "部分完成" : "失败"));
  flowMetric.textContent = status;
  flowMetric.className = status === "失败" ? "bad" : "ok";
}

function summaryText(result) {
  const lines = [];
  lines.push(`流程: ${result.profileLabel}`);
  lines.push("");
  lines.push("【自研编译器】");
  lines.push(`状态: ${result.compiler.available ? (result.compiler.ok ? "成功" : "失败") : "不可用"}`);
  lines.push(`信息: ${result.compiler.message || "-"}`);
  if (result.compiler.stderr) lines.push(`stderr: ${result.compiler.stderr.trim()}`);
  lines.push("");
  lines.push(selectedProfile() === "compiler" ? "【自研 RTLIL 网表图渲染】" : "【Yosys 优化流程】");
  lines.push(`状态: ${result.yosys.available ? (result.yosys.ok ? "成功" : "失败") : "不可用"}`);
  lines.push(`信息: ${result.yosys.message || "-"}`);
  if (result.yosys.executable) lines.push(`路径: ${result.yosys.executable}`);
  if (result.yosys.passDiagnostic) lines.push(`Pass: ${result.yosys.passDiagnostic}`);
  const stats = result.yosys.stats;
  if (stats?.before?.cellTotal !== undefined) {
    lines.push(`Cell: ${stats.before.cellTotal} -> ${stats.after.cellTotal}`);
    lines.push(`收益: ${stats.deltaCells} 个 cell / ${stats.deltaPercent}%`);
  }
  lines.push("");
  lines.push("【可演示能力】");
  lines.push("- 自研 Verilog 子集编译到 RTLIL");
  lines.push("- 可选算法脚本生成与执行");
  lines.push("- RTLIL、日志、统计指标、时序文本和真实网表图片展示");
  return lines.join("\n");
}

function cleanCellName(raw) {
  const cellMatch = raw.match(/([^:\s]+):([A-Za-z_$][A-Za-z0-9_$]*)\s+delay=/);
  const rawName = cellMatch?.[1] || raw.split(/\s+/)[0];
  const type = cellMatch?.[2] || rawName;
  const typeNames = {
    "$add": "加法器",
    "$sub": "减法器",
    "$mul": "乘法器",
    "$and": "与门",
    "$or": "或门",
    "$xor": "异或门",
    "$not": "非门",
    "$mux": "选择器",
    "$reduce_and": "归约与",
    "$reduce_or": "归约或",
    "$reduce_xor": "归约异或",
    "$shl": "左移器",
    "$shr": "右移器",
    "$sshl": "算术左移器",
    "$sshr": "算术右移器",
  };
  const displayType = typeNames[type] || type;
  const nodeName = rawName.replace(/^\\/, "");
  const isTemp = nodeName.startsWith("__");
  const prefix = isTemp ? "临时结点" : "结点";
  return `${prefix} ${nodeName}（${displayType}）`;
}

function formatTimingReport(lines) {
  if (!lines || !lines.length) return "当前没有时序分析输出。";

  const required = lines.find((line) => line.startsWith("required time:"))?.split(":").pop()?.trim();
  const arrival = lines.find((line) => line.startsWith("worst arrival:"))?.split(":").pop()?.trim();
  const slackLine = lines.find((line) => line.startsWith("worst slack:"));
  const slackMatch = slackLine?.match(/worst slack:\s*([-\d.]+)\s+at\s+(.+)/);
  const slack = slackMatch?.[1];
  const endpoint = slackMatch?.[2];

  const pathRows = lines
    .filter((line) => line.includes(" delay=") && line.includes(" arrival=") && !line.includes("fanout="))
    .map((line, index) => {
      const delay = line.match(/delay=([-\d.]+)/)?.[1] || "-";
      const arr = line.match(/arrival=([-\d.]+)/)?.[1] || "-";
      const req = line.match(/required=([-\d.]+)/)?.[1] || "-";
      const slk = line.match(/slack=([-\d.]+)/)?.[1] || "-";
      return `${index + 1}. ${cleanCellName(line)}: 延迟 ${delay}，到达 ${arr}，要求 ${req}，裕量 ${slk}`;
    });

  const endpointRows = lines
    .filter((line) => line.startsWith("output ") || line.startsWith("endpoint output "))
    .map((line) => line.replace("endpoint output", "输出端点").replace("output", "输出"));

  const lowSlackCells = lines
    .filter((line) => line.includes("fanout="))
    .slice(0, 5)
    .map((line, index) => {
      const slackValue = line.match(/slack=([-\d.]+)/)?.[1] || "-";
      const fanout = line.match(/fanout=([-\d.]+)/)?.[1] || "-";
      return `${index + 1}. ${cleanCellName(line)}: 裕量 ${slackValue}，扇出 ${fanout}`;
    });

  const status = Number(slack) < 0 ? "未满足时序约束" : "满足时序约束";
  const output = [];
  output.push("【时序分析摘要】");
  if (required) output.push(`时钟/约束时间: ${required}`);
  if (arrival) output.push(`最慢路径到达时间: ${arrival}`);
  if (slack !== undefined) output.push(`最差裕量: ${slack}，${status}`);
  if (endpoint) output.push(`最差输出端点: ${endpoint}`);
  output.push("");
  output.push("【关键路径】");
  output.push(...(pathRows.length ? pathRows : ["没有识别到关键路径单元。"]));
  if (endpointRows.length) {
    output.push("");
    output.push("【端点结果】");
    output.push(...endpointRows);
  }
  if (lowSlackCells.length) {
    output.push("");
    output.push("【低裕量单元 Top 5】");
    output.push(...lowSlackCells);
  }
  return output.join("\n");
}

function renderView() {
  if (!lastResult) {
    textOutput.classList.remove("hidden");
    graphOutput.classList.add("hidden");
    textOutput.textContent = "暂无结果。";
    return;
  }
  textOutput.classList.toggle("hidden", activeView === "graph");
  graphOutput.classList.toggle("hidden", activeView !== "graph");

  if (activeView === "summary") {
    textOutput.textContent = summaryText(lastResult);
  } else if (activeView === "rtlil") {
    textOutput.textContent = textOrEmpty(lastResult.yosys.afterRtlil || lastResult.compiler.rtlil, "没有 RTLIL 输出。");
  } else if (activeView === "script") {
    textOutput.textContent = textOrEmpty(lastResult.yosys.script, "没有生成 Yosys 脚本。");
  } else if (activeView === "log") {
    textOutput.textContent = textOrEmpty(
      `${lastResult.yosys.stdout || ""}\n${lastResult.yosys.stderr || ""}`.trim(),
      lastResult.yosys.message || "没有 Yosys 日志。"
    );
  } else if (activeView === "timing") {
    textOutput.textContent = formatTimingReport(lastResult.yosys.timing?.lines || []);
  } else if (activeView === "graph") {
    const png = lastResult.yosys.netlistPng;
    if (png) {
      graphOutput.innerHTML = "";
      const img = document.createElement("img");
      img.className = "netlist-image";
      img.alt = "网表图";
      img.src = png;
      graphOutput.appendChild(img);
    } else {
      graphOutput.innerHTML = '<div class="empty-graph">当前没有可显示的网表图片。请确认 Yosys 和 Graphviz 可用。</div>';
    }
  }
}

async function runFlow() {
  runBtn.disabled = true;
  messageBox.textContent = "正在运行...";
  textOutput.textContent = "运行中，请稍候。";
  try {
    const res = await fetch("/api/run", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source: sourceInput.value, profile: selectedProfile(), algorithms: selectedAlgorithms() }),
    });
    const payload = await res.json();
    lastResult = payload;
    renderMetrics(payload);
    const compilerOk = Boolean(payload?.compiler?.ok);
    const yosysOk = Boolean(payload?.yosys?.ok);
    const requiresPasses = payload?.profile !== "compiler";
    messageBox.textContent = requiresPasses
      ? (yosysOk ? "运行完成" : payload.yosys?.message || "优化算法未执行")
      : (compilerOk && yosysOk ? "运行完成" : (compilerOk || yosysOk ? "部分完成，请查看摘要" : payload.yosys?.message || payload.compiler?.message || "运行失败"));
    activeView = "summary";
    tabs.forEach((tab) => tab.classList.toggle("active", tab.dataset.view === activeView));
    renderView();
  } catch (error) {
    messageBox.textContent = "请求失败";
    textOutput.textContent = String(error);
  } finally {
    runBtn.disabled = false;
  }
}

profileSelect.addEventListener("change", renderPassList);
runBtn.addEventListener("click", runFlow);
tabs.forEach((tab) => {
  tab.addEventListener("click", () => {
    activeView = tab.dataset.view;
    tabs.forEach((item) => item.classList.toggle("active", item === tab));
    renderView();
  });
});

loadHealth();
