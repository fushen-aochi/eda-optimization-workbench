#include "frontend_analyzer.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include "compile_error.h"

namespace
{
bool isIdent(const std::string &s)
{
  static const std::regex r("[A-Za-z_][A-Za-z0-9_]*");
  return std::regex_match(s, r);
}

std::string trim(const std::string &s)
{
  size_t i = 0;
  size_t j = s.size();
  while (i < j && std::isspace(static_cast<unsigned char>(s[i])) != 0)
  {
    ++i;
  }
  while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1])) != 0)
  {
    --j;
  }
  return s.substr(i, j - i);
}

std::string stripComments(const std::string &text)
{
  std::string out;
  enum class State
  {
    Normal,
    LineComment,
    BlockComment
  };
  State state = State::Normal;

  for (size_t i = 0; i < text.size(); ++i)
  {
    char c = text[i];
    char next = (i + 1 < text.size()) ? text[i + 1] : '\0';

    if (state == State::LineComment)
    {
      if (c == '\n')
      {
        out.push_back(c);
        state = State::Normal;
      }
      continue;
    }

    if (state == State::BlockComment)
    {
      if (c == '*' && next == '/')
      {
        ++i;
        state = State::Normal;
        continue;
      }
      if (c == '\n')
      {
        out.push_back(c);
      }
      continue;
    }

    if (c == '/' && next == '/')
    {
      ++i;
      state = State::LineComment;
      continue;
    }
    if (c == '/' && next == '*')
    {
      ++i;
      state = State::BlockComment;
      continue;
    }
    out.push_back(c);
  }

  if (state == State::BlockComment)
  {
    throw CompileError("unterminated block comment");
  }

  if (out.size() >= 3 &&
      static_cast<unsigned char>(out[0]) == 0xEF &&
      static_cast<unsigned char>(out[1]) == 0xBB &&
      static_cast<unsigned char>(out[2]) == 0xBF)
  {
    out.erase(0, 3);
  }

  return out;
}

std::vector<std::string> splitStatements(const std::string &verilog)
{
  std::string text = stripComments(verilog);
  std::vector<std::string> res;
  size_t start = 0;
  while (start <= text.size())
  {
    size_t p = text.find(';', start);
    if (p == std::string::npos)
    {
      std::string part = trim(text.substr(start));
      if (!part.empty())
      {
        res.push_back(part);
      }
      break;
    }
    std::string part = trim(text.substr(start, p - start));
    if (!part.empty())
    {
      res.push_back(part);
    }
    start = p + 1;
  }
  return res;
}

std::vector<std::string> parseSignalList(const std::string &text)
{
  std::vector<std::string> names;
  size_t start = 0;
  while (start <= text.size())
  {
    size_t p = text.find(',', start);
    std::string token;
    if (p == std::string::npos)
    {
      token = trim(text.substr(start));
      start = text.size() + 1;
    }
    else
    {
      token = trim(text.substr(start, p - start));
      start = p + 1;
    }
    if (!token.empty())
    {
      if (!isIdent(token))
      {
        throw CompileError("invalid signal name: " + token);
      }
      names.push_back(token);
    }
  }
  if (names.empty())
  {
    throw CompileError("empty signal list");
  }
  return names;
}

std::vector<SignalDecl> parseSignalDeclList(const std::string &text)
{
  std::string rest = trim(text);
  int width = 1;

  static const std::regex widthRe("^\\[(\\d+)\\s*:\\s*(\\d+)\\]\\s*(.*)$");
  std::smatch widthMatch;
  if (std::regex_match(rest, widthMatch, widthRe))
  {
    int msb = std::stoi(widthMatch[1].str());
    int lsb = std::stoi(widthMatch[2].str());
    if (msb < lsb)
    {
      throw CompileError("only descending width ranges [msb:lsb] are supported");
    }
    width = msb - lsb + 1;
    rest = trim(widthMatch[3].str());
  }

  std::vector<std::string> names = parseSignalList(rest);
  std::vector<SignalDecl> decls;
  decls.reserve(names.size());
  for (const auto &name : names)
  {
    decls.emplace_back(name, width);
  }
  return decls;
}

std::vector<std::string> tokenizeExpr(const std::string &expr)
{
  std::vector<std::string> tokens;
  size_t i = 0;
  while (i < expr.size())
  {
    if (std::isspace(static_cast<unsigned char>(expr[i])) != 0)
    {
      ++i;
      continue;
    }

    char c = expr[i];
    if (i + 1 < expr.size())
    {
      std::string two = expr.substr(i, 2);
      if (two == "<<" || two == ">>")
      {
        tokens.push_back(two);
        i += 2;
        continue;
      }
    }

    if (std::isdigit(static_cast<unsigned char>(c)) != 0)
    {
      size_t j = i + 1;
      while (j < expr.size() && std::isdigit(static_cast<unsigned char>(expr[j])) != 0)
      {
        ++j;
      }
      if (j + 1 < expr.size() && expr[j] == '\'' &&
          (expr[j + 1] == 'b' || expr[j + 1] == 'B' || expr[j + 1] == 'd' || expr[j + 1] == 'D' || expr[j + 1] == 'h' || expr[j + 1] == 'H'))
      {
        j += 2;
        while (j < expr.size())
        {
          char t = expr[j];
          if (std::isxdigit(static_cast<unsigned char>(t)) == 0 && t != '_')
          {
            break;
          }
          ++j;
        }
      }
      tokens.push_back(expr.substr(i, j - i));
      i = j;
      continue;
    }

    if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_')
    {
      size_t j = i + 1;
      while (j < expr.size())
      {
        char t = expr[j];
        if (std::isalnum(static_cast<unsigned char>(t)) == 0 && t != '_')
        {
          break;
        }
        ++j;
      }
      tokens.push_back(expr.substr(i, j - i));
      i = j;
      continue;
    }

    if (c == '(' || c == ')' || c == '?' || c == ':' || c == '~' || c == '&' || c == '^' || c == '|' || c == '+' || c == '-' || c == '*')
    {
      tokens.emplace_back(1, c);
      ++i;
      continue;
    }

    throw CompileError("unrecognized character in expression: " + std::string(1, c));
  }
  return tokens;
}

class ExprParser
{
public:
  explicit ExprParser(std::vector<std::string> tks) : tokens(std::move(tks)) {}

  ExprNode *parse()
  {
    ExprNode *node = parseTernary();
    if (peek().has_value())
    {
      throw CompileError("extra token at end of expression: " + peek().value());
    }
    return node;
  }

  const std::vector<ExprNode *> &ownedNodes() const
  {
    return nodes;
  }

private:
  std::vector<std::string> tokens;
  size_t pos = 0;
  std::vector<ExprNode *> nodes;

  ExprNode *makeNode(const std::string &kind, const std::string &value)
  {
    ExprNode *n = new ExprNode(kind, value);
    nodes.push_back(n);
    return n;
  }

  std::optional<std::string> peek() const
  {
    if (pos >= tokens.size())
    {
      return std::nullopt;
    }
    return tokens[pos];
  }

  std::string take()
  {
    if (pos >= tokens.size())
    {
      throw CompileError("unexpected end of expression");
    }
    return tokens[pos++];
  }

  ExprNode *parseTernary()
  {
    ExprNode *cond = parseOr();
    if (peek().has_value() && peek().value() == "?")
    {
      take();
      ExprNode *trueExpr = parseTernary();
      if (!peek().has_value() || peek().value() != ":")
      {
        throw CompileError("missing ':' in ternary expression");
      }
      take();
      ExprNode *falseExpr = parseTernary();
      ExprNode *pair = makeNode("pair", ":");
      pair->left = trueExpr;
      pair->right = falseExpr;
      ExprNode *parent = makeNode("ternary", "?");
      parent->left = cond;
      parent->right = pair;
      return parent;
    }
    return cond;
  }

  ExprNode *parseOr()
  {
    ExprNode *node = parseXor();
    while (peek().has_value() && peek().value() == "|")
    {
      std::string op = take();
      ExprNode *rhs = parseXor();
      ExprNode *parent = makeNode("binary", op);
      parent->left = node;
      parent->right = rhs;
      node = parent;
    }
    return node;
  }

  ExprNode *parseXor()
  {
    ExprNode *node = parseAnd();
    while (peek().has_value() && peek().value() == "^")
    {
      std::string op = take();
      ExprNode *rhs = parseAnd();
      ExprNode *parent = makeNode("binary", op);
      parent->left = node;
      parent->right = rhs;
      node = parent;
    }
    return node;
  }

  ExprNode *parseAnd()
  {
    ExprNode *node = parseShift();
    while (peek().has_value() && peek().value() == "&")
    {
      std::string op = take();
      ExprNode *rhs = parseShift();
      ExprNode *parent = makeNode("binary", op);
      parent->left = node;
      parent->right = rhs;
      node = parent;
    }
    return node;
  }

  ExprNode *parseShift()
  {
    ExprNode *node = parseAddSub();
    while (peek().has_value() && (peek().value() == "<<" || peek().value() == ">>"))
    {
      std::string op = take();
      ExprNode *rhs = parseAddSub();
      ExprNode *parent = makeNode("binary", op);
      parent->left = node;
      parent->right = rhs;
      node = parent;
    }
    return node;
  }

  ExprNode *parseAddSub()
  {
    ExprNode *node = parseMul();
    while (peek().has_value() && (peek().value() == "+" || peek().value() == "-"))
    {
      std::string op = take();
      ExprNode *rhs = parseMul();
      ExprNode *parent = makeNode("binary", op);
      parent->left = node;
      parent->right = rhs;
      node = parent;
    }
    return node;
  }

  ExprNode *parseMul()
  {
    ExprNode *node = parseUnary();
    while (peek().has_value() && peek().value() == "*")
    {
      std::string op = take();
      ExprNode *rhs = parseUnary();
      ExprNode *parent = makeNode("binary", op);
      parent->left = node;
      parent->right = rhs;
      node = parent;
    }
    return node;
  }

  ExprNode *parseUnary()
  {
    if (peek().has_value() && (peek().value() == "~" || peek().value() == "&" || peek().value() == "|" || peek().value() == "^"))
    {
      std::string op = take();
      ExprNode *operand = parseUnary();
      ExprNode *parent = makeNode("unary", op);
      parent->left = operand;
      return parent;
    }
    return parsePrimary();
  }

  ExprNode *parsePrimary()
  {
    if (!peek().has_value())
    {
      throw CompileError("missing expression operand");
    }
    std::string tok = peek().value();
    if (tok == "(")
    {
      take();
      ExprNode *node = parseTernary();
      std::string right = take();
      if (right != ")")
      {
        throw CompileError("missing ')'");
      }
      return node;
    }
    if (isConstText(tok))
    {
      take();
      return makeNode("const", tok);
    }
    if (isIdent(tok))
    {
      take();
      return makeNode("ident", tok);
    }
    throw CompileError("invalid token: " + tok);
  }
};

void checkExpr(const ExprNode *node, const std::set<std::string> &declared)
{
  if (node == nullptr)
  {
    throw CompileError("expression node is null");
  }
  if (node->kind == "ident")
  {
    if (declared.find(node->value) == declared.end())
    {
      throw CompileError("undeclared signal in expression: " + node->value);
    }
    return;
  }
  if (node->kind == "const")
  {
    return;
  }
  if (node->kind == "unary")
  {
    checkExpr(node->left, declared);
    return;
  }
  if (node->kind == "binary")
  {
    checkExpr(node->left, declared);
    checkExpr(node->right, declared);
    return;
  }
  if (node->kind == "ternary")
  {
    checkExpr(node->left, declared);
    checkExpr(node->right->left, declared);
    checkExpr(node->right->right, declared);
    return;
  }
  throw CompileError("unknown expression node kind: " + node->kind);
}
} // namespace

ModuleIR parseModule(const std::string &verilog, NodePool &pool)
{
  std::vector<std::string> stmts = splitStatements(verilog);
  if (stmts.empty())
  {
    throw CompileError("empty input");
  }

  static const std::regex moduleRe("module\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\((.*?)\\)", std::regex::icase);
  std::smatch mm;
  if (!std::regex_match(stmts[0], mm, moduleRe))
  {
    throw CompileError("first statement must be a module declaration");
  }

  ModuleIR ir;
  ir.name = mm[1].str();
  ir.ports = parseSignalList(mm[2].str());

  if (stmts.back() != "endmodule")
  {
    throw CompileError("missing endmodule");
  }

  static const std::regex assignRe("([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*(.+)", std::regex::icase);

  for (size_t i = 1; i + 1 < stmts.size(); ++i)
  {
    const std::string &stmt = stmts[i];
    if (stmt.rfind("input ", 0) == 0)
    {
      std::vector<SignalDecl> xs = parseSignalDeclList(stmt.substr(6));
      ir.inputs.insert(ir.inputs.end(), xs.begin(), xs.end());
      continue;
    }
    if (stmt.rfind("output ", 0) == 0)
    {
      std::vector<SignalDecl> xs = parseSignalDeclList(stmt.substr(7));
      ir.outputs.insert(ir.outputs.end(), xs.begin(), xs.end());
      continue;
    }
    if (stmt.rfind("wire ", 0) == 0)
    {
      std::vector<SignalDecl> xs = parseSignalDeclList(stmt.substr(5));
      ir.wires.insert(ir.wires.end(), xs.begin(), xs.end());
      continue;
    }
    if (stmt.rfind("assign ", 0) == 0)
    {
      std::string content = trim(stmt.substr(7));
      std::smatch am;
      if (!std::regex_match(content, am, assignRe))
      {
        throw CompileError("invalid assign statement: " + stmt);
      }
      Assignment a;
      a.lhs = am[1].str();
      std::string exprText = trim(am[2].str());
      ExprParser parser(tokenizeExpr(exprText));
      a.expr = parser.parse();
      pool.add(parser.ownedNodes());
      ir.assigns.push_back(a);
      continue;
    }
    throw CompileError("unsupported statement: " + stmt);
  }

  std::set<std::string> declared;
  for (const auto &x : ir.inputs)
  {
    if (!declared.insert(x.name).second)
    {
      throw CompileError("duplicate signal declaration: " + x.name);
    }
  }
  for (const auto &x : ir.outputs)
  {
    if (!declared.insert(x.name).second)
    {
      throw CompileError("duplicate signal declaration: " + x.name);
    }
  }
  for (const auto &x : ir.wires)
  {
    if (!declared.insert(x.name).second)
    {
      throw CompileError("duplicate signal declaration: " + x.name);
    }
  }

  std::set<std::string> ioSet;
  for (const auto &x : ir.inputs)
  {
    ioSet.insert(x.name);
  }
  for (const auto &x : ir.outputs)
  {
    ioSet.insert(x.name);
  }
  std::set<std::string> portSet(ir.ports.begin(), ir.ports.end());
  if (ioSet != portSet)
  {
    throw CompileError("module port list must match input/output declarations");
  }

  for (const auto &a : ir.assigns)
  {
    if (declared.find(a.lhs) == declared.end())
    {
      throw CompileError("assignment target is undeclared: " + a.lhs);
    }
    checkExpr(a.expr, declared);
  }

  return ir;
}
