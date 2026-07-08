#include "intermediate_representation.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <utility>

SignalDecl::SignalDecl(std::string n, int w) : name(std::move(n)), width(w) {}

ExprNode::ExprNode(std::string k, std::string v) : kind(std::move(k)), value(std::move(v)) {}

NodePool::~NodePool()
{
  for (ExprNode *n : nodes)
  {
    delete n;
  }
}

void NodePool::add(const std::vector<ExprNode *> &created)
{
  nodes.insert(nodes.end(), created.begin(), created.end());
}

std::vector<std::string> signalNames(const std::vector<SignalDecl> &signals)
{
  std::vector<std::string> names;
  names.reserve(signals.size());
  for (const auto &signal : signals)
  {
    names.push_back(signal.name);
  }
  return names;
}

namespace
{
const SignalDecl *findByName(const std::vector<SignalDecl> &signals, const std::string &name)
{
  auto it = std::find_if(signals.begin(), signals.end(), [&](const SignalDecl &signal)
                         { return signal.name == name; });
  if (it == signals.end())
  {
    return nullptr;
  }
  return &(*it);
}
} // namespace

const SignalDecl *findSignalDecl(const ModuleIR &ir, const std::string &name)
{
  if (const SignalDecl *signal = findByName(ir.inputs, name))
  {
    return signal;
  }
  if (const SignalDecl *signal = findByName(ir.outputs, name))
  {
    return signal;
  }
  return findByName(ir.wires, name);
}

const SignalDecl *findSignalDecl(const NetlistResult &netlist, const std::string &name)
{
  return findByName(netlist.allWires, name);
}

int signalWidth(const ModuleIR &ir, const std::string &name)
{
  if (const SignalDecl *signal = findSignalDecl(ir, name))
  {
    return signal->width;
  }
  return 1;
}

int signalWidth(const NetlistResult &netlist, const std::string &name)
{
  if (const SignalDecl *signal = findSignalDecl(netlist, name))
  {
    return signal->width;
  }
  return 1;
}

bool isDecimalConstText(const std::string &value)
{
  return !value.empty() && std::all_of(value.begin(), value.end(), [](char ch)
                                       { return std::isdigit(static_cast<unsigned char>(ch)) != 0; });
}

namespace
{
bool parseBasedConst(const std::string &value, int &width, int &base, std::string &digits)
{
  static const std::regex re("^(\\d+)'([bBdDhH])([0-9a-fA-F_]+)$");
  std::smatch match;
  if (!std::regex_match(value, match, re))
  {
    return false;
  }
  width = std::stoi(match[1].str());
  char baseCh = static_cast<char>(std::tolower(static_cast<unsigned char>(match[2].str()[0])));
  base = baseCh == 'b' ? 2 : (baseCh == 'd' ? 10 : 16);
  digits = match[3].str();
  digits.erase(std::remove(digits.begin(), digits.end(), '_'), digits.end());
  return width > 0 && !digits.empty();
}
} // namespace

bool isConstText(const std::string &value)
{
  if (isDecimalConstText(value))
  {
    return true;
  }
  int width = 0;
  int base = 10;
  std::string digits;
  return parseBasedConst(value, width, base, digits);
}

unsigned long long constValue(const std::string &value)
{
  unsigned long long parsed = 0;
  if (isDecimalConstText(value))
  {
    for (char ch : value)
    {
      parsed = parsed * 10ULL + static_cast<unsigned long long>(ch - '0');
    }
    return parsed;
  }

  int width = 0;
  int base = 10;
  std::string digits;
  if (!parseBasedConst(value, width, base, digits))
  {
    return 0;
  }
  for (char ch : digits)
  {
    int digit = 0;
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0)
    {
      digit = ch - '0';
    }
    else
    {
      digit = 10 + std::tolower(static_cast<unsigned char>(ch)) - 'a';
    }
    parsed = parsed * static_cast<unsigned long long>(base) + static_cast<unsigned long long>(digit);
  }
  return parsed;
}

int decimalConstWidth(const std::string &value)
{
  if (!isDecimalConstText(value))
  {
    return 1;
  }

  unsigned long long parsed = 0;
  for (char ch : value)
  {
    parsed = parsed * 10ULL + static_cast<unsigned long long>(ch - '0');
  }

  int width = 1;
  while (width < 63 && (1ULL << width) <= parsed)
  {
    ++width;
  }
  return width;
}

int constWidth(const std::string &value)
{
  if (isDecimalConstText(value))
  {
    return decimalConstWidth(value);
  }
  int width = 0;
  int base = 10;
  std::string digits;
  if (parseBasedConst(value, width, base, digits))
  {
    return width;
  }
  return 1;
}

int exprWidth(const ModuleIR &ir, const ExprNode *node)
{
  if (node == nullptr)
  {
    return 1;
  }
  if (node->kind == "const")
  {
    return constWidth(node->value);
  }
  if (node->kind == "ident")
  {
    return signalWidth(ir, node->value);
  }
  if (node->kind == "unary")
  {
    if (node->value == "&" || node->value == "|" || node->value == "^")
    {
      return 1;
    }
    return exprWidth(ir, node->left);
  }
  if (node->kind == "ternary")
  {
    return std::max(exprWidth(ir, node->right->left), exprWidth(ir, node->right->right));
  }
  if (node->kind == "binary")
  {
    if (node->value == "+" || node->value == "-" || node->value == "*" || node->value == "&" || node->value == "|" || node->value == "^")
    {
      return std::max(exprWidth(ir, node->left), exprWidth(ir, node->right));
    }
    if (node->value == "<<" || node->value == ">>")
    {
      return exprWidth(ir, node->left);
    }
  }
  return 1;
}
