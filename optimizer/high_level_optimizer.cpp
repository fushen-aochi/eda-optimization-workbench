#include "high_level_optimizer.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// =============================================================================
// optimizeHighLevel —— 高阶优化（High-level optimization）总览
//
// 作用对象：frontend 解析后的 ModuleIR（每条 assign 的右端为 ExprNode 表达式树）。
//
// 下列为实现的各「子优化方法」的标准名称、含义及在本文件中的入口（顺序即 optimizeHighLevel
// 外层的调用顺序；内部多次迭代直至不动点）：
//
// （1）强度削减 — Strength reduction
//      做法：将「变量 × 编译期常数」改为「左移与加法」之和（乘数按二进制展开）。
//      入口函数：lowerConstMultiplications、lowerConstMultiplicationExpr、buildConstMultiplyExpr
//
// （2）常量传播 — Constant propagation
//      做法：数据流迭代，推断每条 assign 左端信号是否为单一编译期常数；再在表达式中替换 ident。
//      入口函数：computeConstants（汇总各 lhs 的常数值）；propagateConstants（驱动 simplifyExpr）
//
// （3）常量折叠 — Constant folding
//      做法：当子树两侧操作数均为常量（或已由常量传播绑定），在编译期算出字面量并替换结点。
//      入口函数：evalConstValue（对 ~ & | ^ + - * << >> 等按位宽求值）
//
// （4）代数化简 / 窥孔优化 — Algebraic simplification / Peephole optimization
//      做法：布尔代数律（吸收、幂等、零元/幺元）、算术恒等式（加零、乘零/一）、双重否定等，
//            在无法用常量折叠整块替换时仍化简树结构。
//      入口函数：simplifyExpr（内含对 ident 的常量替换以配合常量传播）
//
// （5）拷贝传播 / 别名传播 — Copy propagation / Alias propagation
//      做法：若 assign 右端仅为另一标识符或字面常量，建立别名链并把表达式中的引用解析到规范目标。
//      入口函数：classifyTrivialExpr、computeTrivialValues、resolveTrivialValue、
//                propagateTrivialValuesInExpr、propagateAliases
//
// （6）公共子表达式删除 — Common Subexpression Elimination（CSE）
//      做法：在同一模块多条 assign 中找出语法结构相同的非平凡子表达式，提取为共享临时 wire。
//      入口函数：buildExprInfo（规范化子树键）、collectCseCandidates、extractOneCommonSubexpression
//
// （7）死代码删除 — Dead code elimination（DCE）
//      做法：从 module 的 output 端口反向标记存活赋值链，删除从不驱动输出的 assign 与无用 wire。
//      入口函数：collectReferencedSignals、removeDeadAssignmentsAndWires
//
// —— 辅助语义（贯穿多处，不单独成 Pass）——
// · 按位宽截断（Bit-width masking）：widthMask / applyWidthMask + exprWidth，
//   保证运算结果与 RTL 位宽一致；不是「声明级」缩小 wire 位宽。
//
// —— 未实现（前端无对应语法，故无代码）——
// · 比较器或其它关系运算相关的表达式重写（如 ==、<、数值比较化简等）。
// =============================================================================

namespace
{
using ConstMap = std::unordered_map<std::string, std::optional<unsigned long long>>;

// ---------- 辅助类型定义（仅定义数据形状，算法见后续各 Pass）----------
// · TrivialKind / TrivialValue —— 用于「拷贝传播」：描述某 lhs 的右端是否为单一常量或单一标识符。
// · ExprInfo —— 用于「CSE」：子表达式的规范化字符串键 + 子树规模（用于估算提取收益）。
// · CseOccurrence / CseCandidate —— 用于「CSE」：同一规范化子树在各 assign 中的出现位置列表。
enum class TrivialKind
{
  unknown,
  ident,
  constant
};

struct TrivialValue
{
  TrivialKind kind = TrivialKind::unknown;
  std::string ident;
  std::string constantText;
};

struct ExprInfo
{
  std::string key;
  int size = 0;
};

struct CseOccurrence
{
  ExprNode *node = nullptr;
  size_t assignIndex = 0;
  bool isRoot = false;
};

struct CseCandidate
{
  ExprInfo info;
  std::vector<CseOccurrence> occurrences;
};

// ---------- 表达式结点生命周期与结构工具（供强度削减 / CSE / 常量折叠 等复用）----------
// optimizerNodeStorage：优化阶段新建的 ExprNode 统一由此释放。
// cloneExpr / sameExpr：CSE 克隆模板子树、判断结构相等。
// setConstText / setIdent / replaceWithNode：各类重写 Pass 就地改写结点。
std::vector<std::unique_ptr<ExprNode>> &optimizerNodeStorage()
{
  static std::vector<std::unique_ptr<ExprNode>> nodes;
  return nodes;
}

ExprNode *makeOwnedNode(const std::string &kind, const std::string &value)
{
  auto &nodes = optimizerNodeStorage();
  nodes.push_back(std::make_unique<ExprNode>(kind, value));
  return nodes.back().get();
}

ExprNode *cloneExpr(const ExprNode *node)
{
  if (node == nullptr)
  {
    return nullptr;
  }
  ExprNode *copy = makeOwnedNode(node->kind, node->value);
  copy->left = cloneExpr(node->left);
  copy->right = cloneExpr(node->right);
  return copy;
}

bool isConstText(const ExprNode *node, const std::string &value)
{
  return node != nullptr && node->kind == "const" && node->value == value;
}

bool isUnaryNot(const ExprNode *node)
{
  return node != nullptr && node->kind == "unary" && node->value == "~";
}

bool isCommutativeOp(const std::string &op)
{
  return op == "&" || op == "|" || op == "^" || op == "+" || op == "*";
}

void setConstText(ExprNode *node, const std::string &value)
{
  node->kind = "const";
  node->value = value;
  node->left = nullptr;
  node->right = nullptr;
}

void setIdent(ExprNode *node, const std::string &name)
{
  node->kind = "ident";
  node->value = name;
  node->left = nullptr;
  node->right = nullptr;
}

void replaceWithNode(ExprNode *node, const ExprNode *other)
{
  node->kind = other->kind;
  node->value = other->value;
  node->left = other->left;
  node->right = other->right;
}

bool sameExpr(const ExprNode *lhs, const ExprNode *rhs)
{
  if (lhs == rhs)
  {
    return true;
  }
  if (lhs == nullptr || rhs == nullptr)
  {
    return false;
  }
  if (lhs->kind != rhs->kind || lhs->value != rhs->value)
  {
    return false;
  }
  return sameExpr(lhs->left, rhs->left) && sameExpr(lhs->right, rhs->right);
}

// ---------- 按位宽语义：字面量解析与掩码（支撑常量折叠与代数化简中的「全 1 / 全 0」判定）----------
// parseConstValue：十进制无符号字面量；widthMask / applyWidthMask：按 exprWidth 截断。
std::optional<unsigned long long> parseConstValue(const std::string &text)
{
  if (!isDecimalConstText(text))
  {
    return std::nullopt;
  }
  unsigned long long value = 0;
  for (char ch : text)
  {
    value = value * 10ULL + static_cast<unsigned long long>(ch - '0');
  }
  return value;
}

unsigned long long widthMask(int width)
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

unsigned long long applyWidthMask(unsigned long long value, int width)
{
  return value & widthMask(width);
}

bool isSingleBitExpr(const ModuleIR &ir, const ExprNode *node)
{
  return exprWidth(ir, node) == 1;
}

bool isZeroConst(const ExprNode *node)
{
  return node != nullptr && node->kind == "const" && parseConstValue(node->value).value_or(1ULL) == 0ULL;
}

bool isAllOnesConst(const ExprNode *node, int width)
{
  if (node == nullptr || node->kind != "const")
  {
    return false;
  }
  std::optional<unsigned long long> value = parseConstValue(node->value);
  return value.has_value() && applyWidthMask(value.value(), width) == widthMask(width);
}

ExprNode *makeConstNode(unsigned long long value)
{
  return makeOwnedNode("const", std::to_string(value));
}

ExprNode *makeBinaryNode(const std::string &op, ExprNode *lhs, ExprNode *rhs)
{
  ExprNode *node = makeOwnedNode("binary", op);
  node->left = lhs;
  node->right = rhs;
  return node;
}

// ---------- 常量折叠（Constant folding）—— 求值引擎 evalConstValue ----------
// 在给定 ConstMap（已被常量传播标定为常数的 wire → 数值）下，对整棵 ExprNode 递归求无符号值；
// 运算包括一元 ~ 与二元 & | ^ + - * << >>，结果均经 applyWidthMask 按 exprWidth 截断。
// 被「常量传播」的 computeConstants 与 simplifyExpr 中的整结点折叠共同调用。
std::optional<unsigned long long> evalConstValue(const ModuleIR &ir, const ExprNode *node, const ConstMap &consts)
{
  if (node == nullptr)
  {
    return std::nullopt;
  }

  const int width = exprWidth(ir, node);
  if (node->kind == "const")
  {
    std::optional<unsigned long long> value = parseConstValue(node->value);
    if (!value.has_value())
    {
      return std::nullopt;
    }
    return applyWidthMask(value.value(), width);
  }

  if (node->kind == "ident")
  {
    auto it = consts.find(node->value);
    if (it == consts.end() || !it->second.has_value())
    {
      return std::nullopt;
    }
    return applyWidthMask(it->second.value(), width);
  }

  if (node->kind == "unary")
  {
    std::optional<unsigned long long> operand = evalConstValue(ir, node->left, consts);
    if (!operand.has_value())
    {
      return std::nullopt;
    }
    if (node->value == "~")
    {
      return applyWidthMask(~operand.value(), width);
    }
    return std::nullopt;
  }

  if (node->kind != "binary")
  {
    return std::nullopt;
  }

  std::optional<unsigned long long> lhs = evalConstValue(ir, node->left, consts);
  std::optional<unsigned long long> rhs = evalConstValue(ir, node->right, consts);
  if (!lhs.has_value() || !rhs.has_value())
  {
    return std::nullopt;
  }

  if (node->value == "&")
  {
    return applyWidthMask(lhs.value() & rhs.value(), width);
  }
  if (node->value == "|")
  {
    return applyWidthMask(lhs.value() | rhs.value(), width);
  }
  if (node->value == "^")
  {
    return applyWidthMask(lhs.value() ^ rhs.value(), width);
  }
  if (node->value == "+")
  {
    return applyWidthMask(lhs.value() + rhs.value(), width);
  }
  if (node->value == "-")
  {
    return applyWidthMask(lhs.value() - rhs.value(), width);
  }
  if (node->value == "*")
  {
    return applyWidthMask(lhs.value() * rhs.value(), width);
  }
  if (node->value == "<<")
  {
    return applyWidthMask(lhs.value() << rhs.value(), width);
  }
  if (node->value == ">>")
  {
    return applyWidthMask(lhs.value() >> rhs.value(), width);
  }
  return std::nullopt;
}

// ---------- 强度削减（Strength reduction）：常数乘法 → 左移 + 加法 ----------
// buildConstMultiplyExpr：将乘数按位展开，每项为 operand<<k 或 operand；多比特用 + 串接。
ExprNode *buildConstMultiplyExpr(const ExprNode *operand, unsigned long long multiplier)
{
  if (multiplier == 0ULL)
  {
    return makeConstNode(0ULL);
  }
  if (multiplier == 1ULL)
  {
    return cloneExpr(operand);
  }

  std::vector<int> shifts;
  for (int bit = 0; bit < 63; ++bit)
  {
    if (((multiplier >> bit) & 1ULL) != 0ULL)
    {
      shifts.push_back(bit);
    }
  }

  ExprNode *result = nullptr;
  for (int shift : shifts)
  {
    ExprNode *term = nullptr;
    if (shift == 0)
    {
      term = cloneExpr(operand);
    }
    else
    {
      term = makeBinaryNode("<<", cloneExpr(operand), makeConstNode(static_cast<unsigned long long>(shift)));
    }

    if (result == nullptr)
    {
      result = term;
    }
    else
    {
      result = makeBinaryNode("+", result, term);
    }
  }
  return result;
}

// 递归遍历表达式树：遇到「一侧为常数的 *」则折叠（两常数相乘）或替换为 buildConstMultiplyExpr 结果。
bool lowerConstMultiplicationExpr(ExprNode *node)
{
  if (node == nullptr)
  {
    return false;
  }

  bool changed = false;
  changed |= lowerConstMultiplicationExpr(node->left);
  changed |= lowerConstMultiplicationExpr(node->right);

  if (node->kind != "binary" || node->value != "*")
  {
    return changed;
  }

  const ExprNode *constNode = nullptr;
  const ExprNode *operandNode = nullptr;

  if (node->left != nullptr && node->left->kind == "const")
  {
    constNode = node->left;
    operandNode = node->right;
  }
  else if (node->right != nullptr && node->right->kind == "const")
  {
    constNode = node->right;
    operandNode = node->left;
  }
  else
  {
    return changed;
  }

  std::optional<unsigned long long> multiplier = parseConstValue(constNode->value);
  if (!multiplier.has_value())
  {
    return changed;
  }

  if (operandNode != nullptr && operandNode->kind == "const")
  {
    std::optional<unsigned long long> lhs = parseConstValue(operandNode->value);
    if (lhs.has_value())
    {
      replaceWithNode(node, makeConstNode(lhs.value() * multiplier.value()));
      return true;
    }
  }

  replaceWithNode(node, buildConstMultiplyExpr(operandNode, multiplier.value()));
  return true;
}

// 对每条 assign 反复应用上述强度削减，直到本轮无变化。
bool lowerConstMultiplications(ModuleIR &ir)
{
  bool changed = false;
  for (auto &assign : ir.assigns)
  {
    while (lowerConstMultiplicationExpr(assign.expr))
    {
      changed = true;
    }
  }
  return changed;
}

// ---------- 代数化简 / 窥孔优化（Algebraic / Peephole）—— simplifyExpr ----------
// 与常量折叠的分工：优先递归子结点；若 evalConstValue 可求得整条结点值则整体替换为字面量；
// 否则对一元 ~、二元 &|^+-*<<>> 套用代数律（吸收、幂等、幺元/零元等）。
// ident：若在 ConstMap 中为常数则代入字面量（常量传播的局部应用）。
bool simplifyExpr(const ModuleIR &ir, ExprNode *node, const ConstMap &consts)
{
  bool changed = false;
  if (node == nullptr)
  {
    return false;
  }

  if (node->kind == "ident")
  {
    auto it = consts.find(node->value);
    if (it != consts.end() && it->second.has_value())
    {
      setConstText(node, std::to_string(it->second.value()));
      return true;
    }
    return false;
  }

  if (node->kind == "const")
  {
    return false;
  }

  changed |= simplifyExpr(ir, node->left, consts);
  changed |= simplifyExpr(ir, node->right, consts);

  std::optional<unsigned long long> foldedValue = evalConstValue(ir, node, consts);
  if (foldedValue.has_value())
  {
    setConstText(node, std::to_string(foldedValue.value()));
    return true;
  }

  // 双重否定律：~~x → x（按位取反 involution）
  if (isUnaryNot(node))
  {
    if (isUnaryNot(node->left))
    {
      replaceWithNode(node, node->left->left);
      changed = true;
    }
    return changed;
  }

  if (node->kind != "binary")
  {
    return changed;
  }

  // 按位与 &：零元（遇 0）、幺元（遇全 1）、幂等（x & x → x）
  if (node->value == "&")
  {
    if (isZeroConst(node->left) || isZeroConst(node->right))
    {
      setConstText(node, "0");
      return true;
    }
    if (isAllOnesConst(node->left, exprWidth(ir, node->right)))
    {
      replaceWithNode(node, node->right);
      return true;
    }
    if (isAllOnesConst(node->right, exprWidth(ir, node->left)))
    {
      replaceWithNode(node, node->left);
      return true;
    }
    if (sameExpr(node->left, node->right))
    {
      replaceWithNode(node, node->left);
      return true;
    }
    return changed;
  }

  // 按位或 |：遇全 1、遇 0、幂等（x | x → x）
  if (node->value == "|")
  {
    if (isAllOnesConst(node->left, exprWidth(ir, node)) || isAllOnesConst(node->right, exprWidth(ir, node)))
    {
      setConstText(node, std::to_string(widthMask(exprWidth(ir, node))));
      return true;
    }
    if (isZeroConst(node->left))
    {
      replaceWithNode(node, node->right);
      return true;
    }
    if (isZeroConst(node->right))
    {
      replaceWithNode(node, node->left);
      return true;
    }
    if (sameExpr(node->left, node->right))
    {
      replaceWithNode(node, node->left);
      return true;
    }
    return changed;
  }

  // 按位异或 ^：遇 0、x^x→0；单比特下遇全 1 等价于 ~x
  if (node->value == "^")
  {
    if (isZeroConst(node->left))
    {
      replaceWithNode(node, node->right);
      return true;
    }
    if (isZeroConst(node->right))
    {
      replaceWithNode(node, node->left);
      return true;
    }
    if (sameExpr(node->left, node->right))
    {
      setConstText(node, "0");
      return true;
    }
    if (isAllOnesConst(node->left, exprWidth(ir, node)) || isAllOnesConst(node->right, exprWidth(ir, node)))
    {
      if (isSingleBitExpr(ir, node))
      {
        ExprNode *operand = isAllOnesConst(node->left, exprWidth(ir, node)) ? node->right : node->left;
        if (isUnaryNot(operand))
        {
          replaceWithNode(node, operand->left);
        }
        else
        {
          node->kind = "unary";
          node->value = "~";
          node->left = operand;
          node->right = nullptr;
        }
        return true;
      }
    }
  }

  // 加法 +：幺元（加 0）
  if (node->value == "+")
  {
    if (isZeroConst(node->left))
    {
      replaceWithNode(node, node->right);
      return true;
    }
    if (isZeroConst(node->right))
    {
      replaceWithNode(node, node->left);
      return true;
    }
    return changed;
  }

  // 减法 -：减 0；x−x→0
  if (node->value == "-")
  {
    if (isZeroConst(node->right))
    {
      replaceWithNode(node, node->left);
      return true;
    }
    if (sameExpr(node->left, node->right))
    {
      setConstText(node, "0");
      return true;
    }
    return changed;
  }

  // 移位 << >>：移位数为 0；被移数为全 0 → 0
  if (node->value == "<<"
      || node->value == ">>")
  {
    if (isZeroConst(node->right))
    {
      replaceWithNode(node, node->left);
      return true;
    }
    if (isZeroConst(node->left))
    {
      setConstText(node, "0");
      return true;
    }
    return changed;
  }

  // 乘法 *：遇 0；乘 1（完整强度削减在 lowerConstMultiplicationExpr）
  if (node->value == "*")
  {
    if (isZeroConst(node->left) || isZeroConst(node->right))
    {
      setConstText(node, "0");
      return true;
    }
    if (isConstText(node->left, "1"))
    {
      replaceWithNode(node, node->right);
      return true;
    }
    if (isConstText(node->right, "1"))
    {
      replaceWithNode(node, node->left);
      return true;
    }
  }

  return changed;
}

// ---------- 常量传播（Constant propagation）—— 迭代求解各 lhs 的编译期常数值 ----------
// computeConstants：对每条 assign，若右端在「当前已知各 lhs 常数值」下可被 evalConstValue 完全求出，
//                  则记入映射；多趟迭代直至映射稳定（冲突或非常量则置未知）。
ConstMap computeConstants(const ModuleIR &ir)
{
  ConstMap current;
  bool changed = true;

  while (changed)
  {
    changed = false;
    ConstMap next;

    for (const auto &assign : ir.assigns)
    {
      std::optional<unsigned long long> value = evalConstValue(ir, assign.expr, current);
      auto it = next.find(assign.lhs);
      if (it == next.end())
      {
        next.emplace(assign.lhs, value);
        continue;
      }

      if (!it->second.has_value() || !value.has_value())
      {
        it->second = std::nullopt;
        continue;
      }

      if (it->second.value() != value.value())
      {
        it->second = std::nullopt;
      }
    }

    if (next.size() != current.size())
    {
      changed = true;
    }
    else
    {
      for (const auto &entry : next)
      {
        auto it = current.find(entry.first);
        if (it == current.end())
        {
          changed = true;
          break;
        }
        if (it->second.has_value() != entry.second.has_value())
        {
          changed = true;
          break;
        }
        if (it->second.has_value() && it->second.value() != entry.second.value())
        {
          changed = true;
          break;
        }
      }
    }

    current = std::move(next);
  }

  return current;
}

// ---------- 拷贝传播 / 别名传播（Copy / Alias propagation）—— 平凡右端与别名解析 ----------
// classifyTrivialExpr：右端是否仅为常量或单一标识符。
// computeTrivialValues：每个 lhs 在所有 assign 中是否一致表现为平凡拷贝。
// resolveTrivialValue：沿 ident 链解析到常量或规范别名（防环）。
TrivialValue classifyTrivialExpr(const ExprNode *node)
{
  if (node == nullptr)
  {
    return {};
  }
  if (node->kind == "const")
  {
    TrivialValue value;
    value.kind = TrivialKind::constant;
    value.constantText = node->value;
    return value;
  }
  if (node->kind == "ident")
  {
    TrivialValue value;
    value.kind = TrivialKind::ident;
    value.ident = node->value;
    return value;
  }
  return {};
}

bool sameTrivialValue(const TrivialValue &lhs, const TrivialValue &rhs)
{
  if (lhs.kind != rhs.kind)
  {
    return false;
  }
  if (lhs.kind == TrivialKind::unknown)
  {
    return true;
  }
  if (lhs.kind == TrivialKind::constant)
  {
    return lhs.constantText == rhs.constantText;
  }
  return lhs.ident == rhs.ident;
}

std::unordered_map<std::string, TrivialValue> computeTrivialValues(const ModuleIR &ir)
{
  std::unordered_map<std::string, TrivialValue> values;
  for (const auto &assign : ir.assigns)
  {
    TrivialValue current = classifyTrivialExpr(assign.expr);
    auto it = values.find(assign.lhs);
    if (it == values.end())
    {
      values.emplace(assign.lhs, current);
      continue;
    }
    if (!sameTrivialValue(it->second, current))
    {
      it->second = {};
    }
  }
  return values;
}

TrivialValue resolveTrivialValue(const std::string &name,
                                const std::unordered_map<std::string, TrivialValue> &values,
                                std::unordered_set<std::string> &visiting)
{
  auto it = values.find(name);
  if (it == values.end() || it->second.kind == TrivialKind::unknown)
  {
    TrivialValue value;
    value.kind = TrivialKind::ident;
    value.ident = name;
    return value;
  }

  if (it->second.kind == TrivialKind::constant)
  {
    return it->second;
  }

  if (it->second.ident == name || visiting.find(name) != visiting.end())
  {
    TrivialValue value;
    value.kind = TrivialKind::ident;
    value.ident = name;
    return value;
  }

  visiting.insert(name);
  TrivialValue resolved = resolveTrivialValue(it->second.ident, values, visiting);
  visiting.erase(name);
  return resolved;
}

// 递归遍历表达式树：遇到 ident 则按别名表解析为常量或「规范」标识符（别名传播的单步替换）。
bool propagateTrivialValuesInExpr(ExprNode *node, const std::unordered_map<std::string, TrivialValue> &values)
{
  if (node == nullptr)
  {
    return false;
  }

  bool changed = false;
  changed |= propagateTrivialValuesInExpr(node->left, values);
  changed |= propagateTrivialValuesInExpr(node->right, values);

  if (node->kind != "ident")
  {
    return changed;
  }

  std::unordered_set<std::string> visiting;
  TrivialValue resolved = resolveTrivialValue(node->value, values, visiting);
  if (resolved.kind == TrivialKind::constant)
  {
    setConstText(node, resolved.constantText);
    return true;
  }
  if (resolved.kind == TrivialKind::ident && resolved.ident != node->value)
  {
    setIdent(node, resolved.ident);
    return true;
  }
  return changed;
}

// 常量传播 — 代入阶段：将 computeConstants 得到的映射代入每条 assign 右端，反复 simplifyExpr 直至不动点。
bool propagateConstants(ModuleIR &ir)
{
  bool changed = false;
  ConstMap consts = computeConstants(ir);
  for (auto &assign : ir.assigns)
  {
    while (simplifyExpr(ir, assign.expr, consts))
    {
      changed = true;
    }
  }
  return changed;
}

// 别名传播 — 驱动：先 computeTrivialValues，再对各表达式反复 propagateTrivialValuesInExpr。
bool propagateAliases(ModuleIR &ir)
{
  bool changed = false;
  std::unordered_map<std::string, TrivialValue> values = computeTrivialValues(ir);
  for (auto &assign : ir.assigns)
  {
    while (propagateTrivialValuesInExpr(assign.expr, values))
    {
      changed = true;
    }
  }
  return changed;
}

// ---------- 公共子表达式删除 CSE（Common Subexpression Elimination）----------
// buildExprInfo：可交换运算对子树键排序，得到规范串作为「子表达式身份」。
// collectCseCandidates：统计每种身份在哪些 assign、哪棵子树出现。
// isReusableRepresentative / allocateCseWireName / extractOneCommonSubexpression：选收益最大的一
// 组重复，用共享 lhs 或新建 __cse* wire 与 assign，其它出现处改为引用该信号。
ExprInfo buildExprInfo(const ExprNode *node)
{
  if (node == nullptr)
  {
    return {"null", 0};
  }
  if (node->kind == "const")
  {
    return {"C:" + node->value, 1};
  }
  if (node->kind == "ident")
  {
    return {"I:" + node->value, 1};
  }
  if (node->kind == "unary")
  {
    ExprInfo child = buildExprInfo(node->left);
    return {"U:" + node->value + "(" + child.key + ")", child.size + 1};
  }

  ExprInfo lhs = buildExprInfo(node->left);
  ExprInfo rhs = buildExprInfo(node->right);
  std::string leftKey = lhs.key;
  std::string rightKey = rhs.key;
  if (isCommutativeOp(node->value) && rightKey < leftKey)
  {
    std::swap(leftKey, rightKey);
  }
  return {"B:" + node->value + "(" + leftKey + "," + rightKey + ")", lhs.size + rhs.size + 1};
}

void collectCseCandidates(ExprNode *node,
                          size_t assignIndex,
                          bool isRoot,
                          std::unordered_map<std::string, CseCandidate> &candidates)
{
  if (node == nullptr)
  {
    return;
  }

  collectCseCandidates(node->left, assignIndex, false, candidates);
  collectCseCandidates(node->right, assignIndex, false, candidates);

  if (node->kind == "ident" || node->kind == "const")
  {
    return;
  }

  ExprInfo info = buildExprInfo(node);
  auto &candidate = candidates[info.key];
  candidate.info = info;
  candidate.occurrences.push_back({node, assignIndex, isRoot});
}

// CSE 辅助：判断某条 assign 的 lhs 是否可作提取后的「代表」（避免占用模块 input/output 端口名）。
bool isReusableRepresentative(const ModuleIR &ir, size_t assignIndex)
{
  std::unordered_set<std::string> inputSet;
  for (const auto &signal : ir.inputs)
  {
    inputSet.insert(signal.name);
  }
  std::unordered_set<std::string> outputSet;
  for (const auto &signal : ir.outputs)
  {
    outputSet.insert(signal.name);
  }
  const std::string &lhs = ir.assigns[assignIndex].lhs;
  return inputSet.find(lhs) == inputSet.end() && outputSet.find(lhs) == outputSet.end();
}

// CSE 辅助：分配不与现有 input/output/wire/assign 左端冲突的临时名 __cseN。
std::string allocateCseWireName(const ModuleIR &ir)
{
  std::unordered_set<std::string> declared;
  for (const auto &name : ir.inputs)
  {
    declared.insert(name.name);
  }
  for (const auto &name : ir.outputs)
  {
    declared.insert(name.name);
  }
  for (const auto &name : ir.wires)
  {
    declared.insert(name.name);
  }
  for (const auto &assign : ir.assigns)
  {
    declared.insert(assign.lhs);
  }

  int index = 1;
  while (true)
  {
    std::string name = "__cse" + std::to_string(index);
    if (declared.find(name) == declared.end())
    {
      return name;
    }
    ++index;
  }
}

// CSE 主步（一轮只处理一组）：按估计增益选出重复子表达式，插入共享 wire/assign 或复用已有 lhs。
bool extractOneCommonSubexpression(ModuleIR &ir)
{
  std::unordered_map<std::string, CseCandidate> candidates;
  for (size_t i = 0; i < ir.assigns.size(); ++i)
  {
    collectCseCandidates(ir.assigns[i].expr, i, true, candidates);
  }

  CseCandidate *best = nullptr;
  int bestGain = 0;
  for (auto &entry : candidates)
  {
    CseCandidate &candidate = entry.second;
    if (candidate.occurrences.size() < 2)
    {
      continue;
    }
    int gain = (candidate.info.size - 1) * static_cast<int>(candidate.occurrences.size() - 1);
    if (gain > bestGain)
    {
      bestGain = gain;
      best = &candidate;
    }
  }

  if (best == nullptr)
  {
    return false;
  }

  std::optional<size_t> representativeAssignIndex;
  for (const auto &occurrence : best->occurrences)
  {
    if (occurrence.isRoot && isReusableRepresentative(ir, occurrence.assignIndex))
    {
      representativeAssignIndex = occurrence.assignIndex;
      break;
    }
  }

  std::string representativeName;
  bool keepRepresentativeRoot = false;

  if (representativeAssignIndex.has_value())
  {
    representativeName = ir.assigns[representativeAssignIndex.value()].lhs;
    keepRepresentativeRoot = true;
  }
  else
  {
    representativeName = allocateCseWireName(ir);
  }

  ExprNode *templateExpr = cloneExpr(best->occurrences.front().node);
  for (const auto &occurrence : best->occurrences)
  {
    if (keepRepresentativeRoot && occurrence.isRoot && occurrence.assignIndex == representativeAssignIndex.value())
    {
      continue;
    }
    setIdent(occurrence.node, representativeName);
  }

  if (!keepRepresentativeRoot)
  {
    Assignment sharedAssign;
    sharedAssign.lhs = representativeName;
    sharedAssign.expr = templateExpr;

    size_t insertIndex = best->occurrences.front().assignIndex;
    for (const auto &occurrence : best->occurrences)
    {
      insertIndex = std::min(insertIndex, occurrence.assignIndex);
    }

    ir.wires.emplace_back(representativeName, exprWidth(ir, templateExpr));
    ir.assigns.insert(ir.assigns.begin() + static_cast<std::ptrdiff_t>(insertIndex), sharedAssign);
  }

  return true;
}

// ---------- 死代码删除 DCE（Dead code elimination）----------
// 从 output 集合反向标记哪些 assign 活在「驱动输出」的锥上；删除未标记赋值。
// 再据存活赋值与输出用到的信号集合删除从未引用的 wire 声明。
void collectReferencedSignals(const ExprNode *node, std::unordered_set<std::string> &signals)
{
  if (node == nullptr)
  {
    return;
  }
  if (node->kind == "ident")
  {
    signals.insert(node->value);
  }
  collectReferencedSignals(node->left, signals);
  collectReferencedSignals(node->right, signals);
}

bool removeDeadAssignmentsAndWires(ModuleIR &ir)
{
  std::unordered_set<std::string> needed;
  for (const auto &signal : ir.outputs)
  {
    needed.insert(signal.name);
  }
  std::vector<bool> live(ir.assigns.size(), false);
  bool changed = true;

  while (changed)
  {
    changed = false;
    for (size_t i = 0; i < ir.assigns.size(); ++i)
    {
      if (live[i] || needed.find(ir.assigns[i].lhs) == needed.end())
      {
        continue;
      }
      live[i] = true;
      collectReferencedSignals(ir.assigns[i].expr, needed);
      changed = true;
    }
  }

  bool removedAssign = false;
  std::vector<Assignment> keptAssigns;
  keptAssigns.reserve(ir.assigns.size());
  for (size_t i = 0; i < ir.assigns.size(); ++i)
  {
    if (!live[i])
    {
      removedAssign = true;
      continue;
    }
    keptAssigns.push_back(ir.assigns[i]);
  }
  ir.assigns = std::move(keptAssigns);

  std::unordered_set<std::string> liveSignals;
  for (const auto &signal : ir.outputs)
  {
    liveSignals.insert(signal.name);
  }
  for (const auto &assign : ir.assigns)
  {
    liveSignals.insert(assign.lhs);
    collectReferencedSignals(assign.expr, liveSignals);
  }

  bool removedWire = false;
  std::vector<SignalDecl> keptWires;
  keptWires.reserve(ir.wires.size());
  for (const auto &wire : ir.wires)
  {
    if (liveSignals.find(wire.name) == liveSignals.end())
    {
      removedWire = true;
      continue;
    }
    keptWires.push_back(wire);
  }
  ir.wires = std::move(keptWires);

  return removedAssign || removedWire;
}
} // namespace（以上为 optimizeHighLevel 的内部实现）

// =============================================================================
// 入口 optimizeHighLevel：高阶优化管线（多趟迭代直至本轮无任何变换）
//
// 单轮顺序：
//   ① 强度削减（乘法 → 移位+加）
//   ② 常量传播 + 常量折叠 + 代数化简（propagateConstants → simplifyExpr）
//   ③ 拷贝/别名传播（propagateAliases）
//   ④ 反复执行一步 CSE；每步后回到 ②③ 以便新 wire 继续被常量/别名优化
//   ⑤ 死代码删除 DCE
//
// 外圈 while：若本轮发生过任一变换则再跑一整轮，直至达到不动点。
// =============================================================================
void optimizeHighLevel(ModuleIR &ir)
{
  bool changed = true;
  while (changed)
  {
    changed = false;
    changed |= lowerConstMultiplications(ir);
    changed |= propagateConstants(ir);
    changed |= propagateAliases(ir);
    while (extractOneCommonSubexpression(ir))
    {
      changed = true;
      changed |= propagateConstants(ir);
      changed |= propagateAliases(ir);
    }
    changed |= removeDeadAssignmentsAndWires(ir);
  }
}
