#pragma once

#include <string>
#include <vector>

struct SignalDecl
{
  std::string name;
  int width = 1;

  SignalDecl() = default;
  SignalDecl(std::string n, int w);
};

struct ExprNode
{
  std::string kind;
  std::string value;
  ExprNode *left = nullptr;
  ExprNode *right = nullptr;

  ExprNode(std::string k, std::string v);
};

struct Assignment
{
  std::string lhs;
  ExprNode *expr = nullptr;
};

struct ModuleIR
{
  std::string name;
  std::vector<std::string> ports;
  std::vector<SignalDecl> inputs;
  std::vector<SignalDecl> outputs;
  std::vector<SignalDecl> wires;
  std::vector<Assignment> assigns;
};

struct Gate
{
  std::string op;
  std::string in;
  std::string in1;
  std::string in2;
  std::string in3;
  std::string out;
  int aWidth = 1;
  int bWidth = 1;
  int sWidth = 1;
  int yWidth = 1;
};

struct Connection
{
  std::string from;
  std::string to;
  int width = 1;
};

struct NetlistResult
{
  std::vector<SignalDecl> allWires;
  std::vector<Gate> gates;
  std::vector<Connection> connections;
};

class NodePool
{
public:
  ~NodePool();
  void add(const std::vector<ExprNode *> &created);

private:
  std::vector<ExprNode *> nodes;
};

std::vector<std::string> signalNames(const std::vector<SignalDecl> &signals);
const SignalDecl *findSignalDecl(const ModuleIR &ir, const std::string &name);
const SignalDecl *findSignalDecl(const NetlistResult &netlist, const std::string &name);
int signalWidth(const ModuleIR &ir, const std::string &name);
int signalWidth(const NetlistResult &netlist, const std::string &name);
bool isDecimalConstText(const std::string &value);
bool isConstText(const std::string &value);
unsigned long long constValue(const std::string &value);
int decimalConstWidth(const std::string &value);
int constWidth(const std::string &value);
int exprWidth(const ModuleIR &ir, const ExprNode *node);
