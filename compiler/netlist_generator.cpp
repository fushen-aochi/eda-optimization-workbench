#include "netlist_generator.h"

#include <set>
#include <string>
#include <unordered_map>

#include "compile_error.h"

namespace
{
class NetlistBuilder
{
public:
  explicit NetlistBuilder(const ModuleIR &moduleIr, const std::set<std::string> &declaredSignals)
      : ir(moduleIr), declared(declaredSignals) {}

  std::string compileExpr(const ExprNode *node)
  {
    if (node->kind == "ident" || node->kind == "const")
    {
      return node->value;
    }
    if (node->kind == "unary")
    {
      std::string src = compileExpr(node->left);
      std::string out = newTemp();
      Gate g;
      if (node->value == "~")
      {
        g.op = "NOT";
      }
      else if (node->value == "&")
      {
        g.op = "REDUCE_AND";
      }
      else if (node->value == "|")
      {
        g.op = "REDUCE_OR";
      }
      else if (node->value == "^")
      {
        g.op = "REDUCE_XOR";
      }
      else
      {
        throw CompileError("unsupported unary operator: " + node->value);
      }
      g.in = src;
      g.out = out;
      g.aWidth = exprWidth(ir, node->left);
      g.yWidth = exprWidth(ir, node);
      gates.push_back(g);
      tempWidths[out] = g.yWidth;
      return out;
    }
    if (node->kind == "ternary")
    {
      std::string cond = compileExpr(node->left);
      std::string lhs = compileExpr(node->right->left);
      std::string rhs = compileExpr(node->right->right);
      std::string out = newTemp();
      Gate g;
      g.op = "MUX";
      g.in1 = rhs;
      g.in2 = lhs;
      g.in3 = cond;
      g.out = out;
      g.aWidth = exprWidth(ir, node->right->right);
      g.bWidth = exprWidth(ir, node->right->left);
      g.sWidth = exprWidth(ir, node->left);
      g.yWidth = exprWidth(ir, node);
      gates.push_back(g);
      tempWidths[out] = g.yWidth;
      return out;
    }
    if (node->kind == "binary")
    {
      std::string lhs = compileExpr(node->left);
      std::string rhs = compileExpr(node->right);
      std::string out = newTemp();
      Gate g;
      if (node->value == "&")
      {
        g.op = "AND";
      }
      else if (node->value == "|")
      {
        g.op = "OR";
      }
      else if (node->value == "^")
      {
        g.op = "XOR";
      }
      else if (node->value == "+")
      {
        g.op = "ADD";
      }
      else if (node->value == "-")
      {
        g.op = "SUB";
      }
      else if (node->value == "*")
      {
        g.op = "MUL";
      }
      else if (node->value == "<<")
      {
        g.op = "SHL";
      }
      else if (node->value == ">>")
      {
        g.op = "SHR";
      }
      else
      {
        throw CompileError("unsupported binary operator: " + node->value);
      }
      g.in1 = lhs;
      g.in2 = rhs;
      g.out = out;
      g.aWidth = exprWidth(ir, node->left);
      g.bWidth = exprWidth(ir, node->right);
      g.yWidth = exprWidth(ir, node);
      if ((g.op == "AND" || g.op == "OR" || g.op == "XOR") && node->left != nullptr && node->right != nullptr)
      {
        g.yWidth = std::max(g.aWidth, g.bWidth);
      }
      gates.push_back(g);
      tempWidths[out] = g.yWidth;
      return out;
    }
    throw CompileError("cannot compile expression node: " + node->kind);
  }

  const std::vector<Gate> &getGates() const
  {
    return gates;
  }

  const std::set<std::string> &getDeclared() const
  {
    return declared;
  }

  int getSignalWidth(const std::string &name) const
  {
    auto it = tempWidths.find(name);
    if (it != tempWidths.end())
    {
      return it->second;
    }
    return signalWidth(ir, name);
  }

private:
  const ModuleIR &ir;
  std::vector<Gate> gates;
  int tempCount = 0;
  std::set<std::string> declared;
  std::unordered_map<std::string, int> tempWidths;

  std::string newTemp()
  {
    while (true)
    {
      ++tempCount;
      std::string name = "__t" + std::to_string(tempCount);
      if (declared.find(name) == declared.end())
      {
        declared.insert(name);
        return name;
      }
    }
  }
};
} // namespace

NetlistResult generateNetlist(const ModuleIR &ir)
{
  std::set<std::string> declared;
  for (const auto &x : ir.inputs)
  {
    declared.insert(x.name);
  }
  for (const auto &x : ir.outputs)
  {
    declared.insert(x.name);
  }
  for (const auto &x : ir.wires)
  {
    declared.insert(x.name);
  }

  NetlistBuilder builder(ir, declared);
  NetlistResult result;
  result.allWires = ir.wires;
  for (const auto &a : ir.assigns)
  {
    std::string src = builder.compileExpr(a.expr);
    if (src != a.lhs)
    {
      Connection c;
      c.from = src;
      c.to = a.lhs;
      c.width = signalWidth(ir, a.lhs);
      result.connections.push_back(c);
    }
  }

  result.gates = builder.getGates();
  for (const auto &x : builder.getDeclared())
  {
    if (x.rfind("__t", 0) == 0)
    {
      result.allWires.emplace_back(x, builder.getSignalWidth(x));
    }
  }
  return result;
}
