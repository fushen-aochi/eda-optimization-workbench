#include "rtlil_writer.h"

#include <algorithm>
#include <ostream>
#include <set>
#include <string>

#include "compile_error.h"

namespace
{
std::string rtlilEscapeId(const std::string &id)
{
  return "\\" + id;
}

bool isDecimalConst(const std::string &value)
{
  return isConstText(value);
}

int decimalConstWidth(const std::string &value)
{
  return constWidth(value);
}

unsigned long long localWidthMask(int width)
{
  if (width <= 0)
  {
    return 0ULL;
  }
  if (width >= 64)
  {
    return ~0ULL;
  }
  return (1ULL << width) - 1ULL;
}

std::string decimalToBinary(unsigned long long value, int width)
{
  std::string bits;
  bits.reserve(static_cast<size_t>(width));
  for (int i = 0; i < width; ++i)
  {
    bits.push_back((value & 1ULL) != 0ULL ? '1' : '0');
    value >>= 1ULL;
  }
  return bits;
}

std::string rtlilConst(unsigned long long value, int width)
{
  return std::to_string(width) + "'" + decimalToBinary(value & localWidthMask(width), width);
}

std::string rtlilSig(const std::string &s, int widthHint = 0)
{
  if (isDecimalConst(s))
  {
    unsigned long long parsed = constValue(s);
    int width = widthHint > 0 ? widthHint : decimalConstWidth(s);
    return rtlilConst(parsed, width);
  }
  return rtlilEscapeId(s);
}

std::set<std::string> signalNameSet(const std::vector<SignalDecl> &signals)
{
  std::set<std::string> names;
  for (const auto &signal : signals)
  {
    names.insert(signal.name);
  }
  return names;
}
} // namespace

void writeRtlil(std::ostream &os, const ModuleIR &ir, const NetlistResult &netlist)
{
  os << "autoidx 1\n";
  os << "module " << rtlilEscapeId(ir.name) << "\n";

  std::set<std::string> inputSet = signalNameSet(ir.inputs);
  std::set<std::string> outputSet = signalNameSet(ir.outputs);

  int portId = 1;
  for (const auto &p : ir.ports)
  {
    int width = signalWidth(ir, p);
    os << "  wire";
    if (inputSet.find(p) != inputSet.end())
    {
      os << " input " << portId;
    }
    if (outputSet.find(p) != outputSet.end())
    {
      os << " output " << portId;
    }
    if (width > 1)
    {
      os << " width " << width;
    }
    os << " " << rtlilEscapeId(p) << "\n";
    ++portId;
  }

  for (const auto &w : netlist.allWires)
  {
    if (inputSet.find(w.name) != inputSet.end() || outputSet.find(w.name) != outputSet.end())
    {
      continue;
    }
    os << "  wire";
    if (w.width > 1)
    {
      os << " width " << w.width;
    }
    os << " " << rtlilEscapeId(w.name) << "\n";
  }

  int cellIdx = 1;
  for (const auto &g : netlist.gates)
  {
    std::string cellType;
    if (g.op == "AND")
    {
      cellType = "$and";
    }
    else if (g.op == "OR")
    {
      cellType = "$or";
    }
    else if (g.op == "XOR")
    {
      cellType = "$xor";
    }
    else if (g.op == "NOT")
    {
      cellType = "$not";
    }
    else if (g.op == "ADD")
    {
      cellType = "$add";
    }
    else if (g.op == "SUB")
    {
      cellType = "$sub";
    }
    else if (g.op == "MUL")
    {
      cellType = "$mul";
    }
    else if (g.op == "SHL")
    {
      cellType = "$shl";
    }
    else if (g.op == "SHR")
    {
      cellType = "$shr";
    }
    else if (g.op == "MUX")
    {
      cellType = "$mux";
    }
    else if (g.op == "REDUCE_AND")
    {
      cellType = "$reduce_and";
    }
    else if (g.op == "REDUCE_OR")
    {
      cellType = "$reduce_or";
    }
    else if (g.op == "REDUCE_XOR")
    {
      cellType = "$reduce_xor";
    }
    else
    {
      throw CompileError("cannot emit RTLIL for unknown gate type: " + g.op);
    }

    os << "  cell " << cellType << " " << rtlilEscapeId(cellType + std::string("_") + std::to_string(cellIdx))
       << "\n";
    if (g.op == "NOT" || g.op == "REDUCE_AND" || g.op == "REDUCE_OR" || g.op == "REDUCE_XOR")
    {
      os << "    parameter \\A_SIGNED 0\n";
      os << "    parameter \\A_WIDTH " << g.aWidth << "\n";
      os << "    parameter \\Y_WIDTH " << g.yWidth << "\n";
      os << "    connect \\A " << rtlilSig(g.in, g.aWidth) << "\n";
      os << "    connect \\Y " << rtlilSig(g.out, g.yWidth) << "\n";
    }
    else if (g.op == "MUX")
    {
      os << "    parameter \\WIDTH " << g.yWidth << "\n";
      os << "    connect \\A " << rtlilSig(g.in1, g.yWidth) << "\n";
      os << "    connect \\B " << rtlilSig(g.in2, g.yWidth) << "\n";
      os << "    connect \\S " << rtlilSig(g.in3, g.sWidth) << "\n";
      os << "    connect \\Y " << rtlilSig(g.out, g.yWidth) << "\n";
    }
    else
    {
      os << "    parameter \\A_SIGNED 0\n";
      os << "    parameter \\A_WIDTH " << g.aWidth << "\n";
      os << "    parameter \\B_SIGNED 0\n";
      os << "    parameter \\B_WIDTH " << g.bWidth << "\n";
      os << "    parameter \\Y_WIDTH " << g.yWidth << "\n";
      os << "    connect \\A " << rtlilSig(g.in1, g.aWidth) << "\n";
      os << "    connect \\B " << rtlilSig(g.in2, g.bWidth) << "\n";
      os << "    connect \\Y " << rtlilSig(g.out, g.yWidth) << "\n";
    }
    os << "  end\n";
    ++cellIdx;
  }

  for (const auto &c : netlist.connections)
  {
    os << "  connect " << rtlilSig(c.to, c.width) << " " << rtlilSig(c.from, c.width) << "\n";
  }

  os << "end\n";
}
