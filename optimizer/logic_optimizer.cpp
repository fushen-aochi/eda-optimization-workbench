#include "logic_optimizer.h"

#include <algorithm>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
bool isTempSignal(const std::string &name)
{
  return name.rfind("__t", 0) == 0;
}

int localSignalWidth(const NetlistResult &netlist, const std::string &name)
{
  if (isDecimalConstText(name))
  {
    return decimalConstWidth(name);
  }
  return signalWidth(netlist, name);
}

std::unordered_map<std::string, size_t> buildDriverMap(const NetlistResult &netlist)
{
  std::unordered_map<std::string, size_t> drivers;
  for (size_t i = 0; i < netlist.gates.size(); ++i)
  {
    drivers[netlist.gates[i].out] = i;
  }
  return drivers;
}

std::unordered_map<std::string, int> buildUseCount(const NetlistResult &netlist)
{
  std::unordered_map<std::string, int> uses;
  for (const auto &gate : netlist.gates)
  {
    if (gate.op == "NOT")
    {
      ++uses[gate.in];
    }
    else
    {
      ++uses[gate.in1];
      ++uses[gate.in2];
    }
  }
  for (const auto &conn : netlist.connections)
  {
    ++uses[conn.from];
  }
  return uses;
}

bool matchCommonInput(const Gate &lhs,
                      const Gate &rhs,
                      std::string &common,
                      std::string &lhsOther,
                      std::string &rhsOther)
{
  if (lhs.in1 == rhs.in1)
  {
    common = lhs.in1;
    lhsOther = lhs.in2;
    rhsOther = rhs.in2;
    return true;
  }
  if (lhs.in1 == rhs.in2)
  {
    common = lhs.in1;
    lhsOther = lhs.in2;
    rhsOther = rhs.in1;
    return true;
  }
  if (lhs.in2 == rhs.in1)
  {
    common = lhs.in2;
    lhsOther = lhs.in1;
    rhsOther = rhs.in2;
    return true;
  }
  if (lhs.in2 == rhs.in2)
  {
    common = lhs.in2;
    lhsOther = lhs.in1;
    rhsOther = rhs.in1;
    return true;
  }
  return false;
}

void refreshGateWidths(Gate &gate, const NetlistResult &netlist)
{
  if (gate.op == "NOT")
  {
    gate.aWidth = localSignalWidth(netlist, gate.in);
    gate.yWidth = localSignalWidth(netlist, gate.out);
    return;
  }

  gate.aWidth = localSignalWidth(netlist, gate.in1);
  gate.bWidth = localSignalWidth(netlist, gate.in2);
  gate.yWidth = localSignalWidth(netlist, gate.out);
}

bool rewriteOne(NetlistResult &netlist)
{
  std::unordered_map<std::string, size_t> drivers = buildDriverMap(netlist);
  std::unordered_map<std::string, int> uses = buildUseCount(netlist);

  for (size_t topIndex = 0; topIndex < netlist.gates.size(); ++topIndex)
  {
    Gate &top = netlist.gates[topIndex];
    std::string childOp;
    if (top.op == "OR")
    {
      childOp = "AND";
    }
    else if (top.op == "AND")
    {
      childOp = "OR";
    }
    else
    {
      continue;
    }

    auto leftIt = drivers.find(top.in1);
    auto rightIt = drivers.find(top.in2);
    if (leftIt == drivers.end() || rightIt == drivers.end())
    {
      continue;
    }

    const size_t leftIndex = leftIt->second;
    const size_t rightIndex = rightIt->second;
    if (leftIndex == rightIndex)
    {
      continue;
    }

    Gate &left = netlist.gates[leftIndex];
    Gate &right = netlist.gates[rightIndex];
    if (left.op != childOp || right.op != childOp)
    {
      continue;
    }

    std::string common;
    std::string leftOther;
    std::string rightOther;
    if (!matchCommonInput(left, right, common, leftOther, rightOther))
    {
      continue;
    }

    const bool leftReusable = uses[left.out] == 1;
    const bool rightReusable = uses[right.out] == 1;
    if (!leftReusable && !rightReusable)
    {
      continue;
    }

    const bool reuseLeft = leftReusable;
    Gate &inner = reuseLeft ? left : right;

    inner.op = top.op;
    inner.in1 = leftOther;
    inner.in2 = rightOther;
    refreshGateWidths(inner, netlist);

    top.op = childOp;
    top.in1 = common;
    top.in2 = inner.out;
    refreshGateWidths(top, netlist);
    return true;
  }

  return false;
}

bool removeDeadGates(NetlistResult &netlist)
{
  std::unordered_map<std::string, size_t> drivers = buildDriverMap(netlist);
  std::unordered_map<std::string, int> uses = buildUseCount(netlist);
  std::vector<bool> live(netlist.gates.size(), true);
  std::queue<size_t> deadQueue;

  for (size_t i = 0; i < netlist.gates.size(); ++i)
  {
    if (uses[netlist.gates[i].out] == 0)
    {
      deadQueue.push(i);
    }
  }

  while (!deadQueue.empty())
  {
    size_t index = deadQueue.front();
    deadQueue.pop();

    if (!live[index])
    {
      continue;
    }
    if (uses[netlist.gates[index].out] != 0)
    {
      continue;
    }

    live[index] = false;
    const Gate &gate = netlist.gates[index];
    std::vector<std::string> inputs;
    if (gate.op == "NOT")
    {
      inputs.push_back(gate.in);
    }
    else
    {
      inputs.push_back(gate.in1);
      inputs.push_back(gate.in2);
    }

    for (const auto &input : inputs)
    {
      auto useIt = uses.find(input);
      if (useIt == uses.end() || useIt->second == 0)
      {
        continue;
      }
      --useIt->second;
      auto driverIt = drivers.find(input);
      if (driverIt != drivers.end() && useIt->second == 0)
      {
        deadQueue.push(driverIt->second);
      }
    }
  }

  bool changed = false;
  std::vector<Gate> keptGates;
  keptGates.reserve(netlist.gates.size());
  for (size_t i = 0; i < netlist.gates.size(); ++i)
  {
    if (!live[i])
    {
      changed = true;
      continue;
    }
    keptGates.push_back(netlist.gates[i]);
  }
  netlist.gates = std::move(keptGates);
  return changed;
}

void cleanupTempWires(NetlistResult &netlist)
{
  std::unordered_set<std::string> referenced;
  for (const auto &gate : netlist.gates)
  {
    referenced.insert(gate.out);
    if (gate.op == "NOT")
    {
      referenced.insert(gate.in);
    }
    else
    {
      referenced.insert(gate.in1);
      referenced.insert(gate.in2);
    }
  }
  for (const auto &conn : netlist.connections)
  {
    referenced.insert(conn.from);
    referenced.insert(conn.to);
  }

  std::vector<SignalDecl> kept;
  kept.reserve(netlist.allWires.size());
  for (const auto &wire : netlist.allWires)
  {
    if (isTempSignal(wire.name) && referenced.find(wire.name) == referenced.end())
    {
      continue;
    }
    kept.push_back(wire);
  }
  netlist.allWires = std::move(kept);
}
} // namespace

void optimizeLogic(NetlistResult &netlist)
{
  bool changed = true;
  while (changed)
  {
    changed = false;
    while (rewriteOne(netlist))
    {
      changed = true;
      removeDeadGates(netlist);
    }
    changed |= removeDeadGates(netlist);
  }

  cleanupTempWires(netlist);

  std::sort(netlist.allWires.begin(), netlist.allWires.end(),
            [](const SignalDecl &lhs, const SignalDecl &rhs)
            { return lhs.name < rhs.name; });
  netlist.allWires.erase(
      std::unique(netlist.allWires.begin(), netlist.allWires.end(),
                  [](const SignalDecl &lhs, const SignalDecl &rhs)
                  { return lhs.name == rhs.name; }),
      netlist.allWires.end());

  std::sort(netlist.connections.begin(), netlist.connections.end(),
            [](const Connection &lhs, const Connection &rhs)
            {
              if (lhs.to != rhs.to)
              {
                return lhs.to < rhs.to;
              }
              return lhs.from < rhs.from;
            });
  netlist.connections.erase(
      std::unique(netlist.connections.begin(), netlist.connections.end(),
                  [](const Connection &lhs, const Connection &rhs)
                  { return lhs.from == rhs.from && lhs.to == rhs.to; }),
      netlist.connections.end());
}
