#include "kernel/register.h"
#include "kernel/log.h"
#include "kernel/sigtools.h"
#include "kernel/modtools.h"
#include "kernel/celltypes.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

namespace
{
struct AlgebraLiteral
{
	SigSpec sig;
	bool neg = false;
};

struct AlgebraCube
{
	std::map<std::string, AlgebraLiteral> literals;
	bool zero = false;
};

struct AlgebraCover
{
	std::vector<AlgebraCube> cubes;
};

struct KernelPair
{
	AlgebraCover kernel;
	AlgebraCube cokernel;
};

struct DivisionResult
{
	AlgebraCover quotient;
	AlgebraCover remainder;
	bool valid = false;
};

struct FactorCandidate
{
	AlgebraCover divisor;
	AlgebraCover quotient;
	AlgebraCover remainder;
	int gain = 0;
};

struct RewriteCandidate
{
	RTLIL::Cell *cell = nullptr;
	RTLIL::IdString cell_name;
	SigSpec y;
	RTLIL::IdString root_op;
	RTLIL::IdString child_op;
	FactorCandidate factor;
	int gain = 0;
};

std::string signal_key(const SigSpec &sig)
{
	return log_signal(sig);
}

std::string literal_key(const AlgebraLiteral &lit)
{
	return std::string(lit.neg ? "!" : "") + signal_key(lit.sig);
}

bool is_const_signal(const SigSpec &sig, bool &value)
{
	SigSpec flat = sig;
	if (GetSize(flat) != 1 || flat[0].wire != nullptr)
		return false;
	if (flat[0].data == State::S0) {
		value = false;
		return true;
	}
	if (flat[0].data == State::S1) {
		value = true;
		return true;
	}
	return false;
}

bool add_literal(AlgebraCube &cube, const SigSpec &sig, bool neg)
{
	bool const_value = false;
	if (is_const_signal(sig, const_value)) {
		if (const_value == neg)
			cube.zero = true;
		return true;
	}

	std::string key = signal_key(sig);
	auto it = cube.literals.find(key);
	if (it == cube.literals.end()) {
		cube.literals[key] = {sig, neg};
		return true;
	}
	if (it->second.neg != neg)
		cube.zero = true;
	return true;
}

std::string cube_key(const AlgebraCube &cube)
{
	if (cube.zero)
		return "0";
	std::string key;
	for (auto &lit : cube.literals) {
		key += lit.second.neg ? "!" : "";
		key += lit.first;
		key += ";";
	}
	return key;
}

std::string cover_key(const AlgebraCover &cover)
{
	std::vector<std::string> keys;
	for (auto &cube : cover.cubes)
		if (!cube.zero)
			keys.push_back(cube_key(cube));
	std::sort(keys.begin(), keys.end());
	std::string out;
	for (auto &key : keys)
		out += key + "|";
	return out;
}

bool cube_contains_cube(const AlgebraCube &cube, const AlgebraCube &divisor)
{
	if (cube.zero)
		return false;
	if (divisor.zero)
		return true;
	for (auto &lit : divisor.literals) {
		auto it = cube.literals.find(lit.first);
		if (it == cube.literals.end() || it->second.neg != lit.second.neg)
			return false;
	}
	return true;
}

AlgebraCube subtract_cube(const AlgebraCube &cube, const AlgebraCube &divisor)
{
	AlgebraCube out = cube;
	for (auto &lit : divisor.literals)
		out.literals.erase(lit.first);
	return out;
}

bool multiply_cubes(const AlgebraCube &a, const AlgebraCube &b, AlgebraCube &out)
{
	out = a;
	if (a.zero || b.zero) {
		out.zero = true;
		return true;
	}
	for (auto &lit : b.literals) {
		auto it = out.literals.find(lit.first);
		if (it == out.literals.end()) {
			out.literals[lit.first] = lit.second;
			continue;
		}
		if (it->second.neg != lit.second.neg) {
			out.zero = true;
			return true;
		}
	}
	return true;
}

AlgebraCube intersect_cubes(const std::vector<AlgebraCube> &cubes)
{
	AlgebraCube common;
	if (cubes.empty())
		return common;
	common = cubes.front();
	for (size_t i = 1; i < cubes.size(); i++) {
		for (auto it = common.literals.begin(); it != common.literals.end();) {
			auto other = cubes[i].literals.find(it->first);
			if (other == cubes[i].literals.end() || other->second.neg != it->second.neg)
				it = common.literals.erase(it);
			else
				++it;
		}
	}
	common.zero = false;
	return common;
}

bool cube_free(const AlgebraCover &cover)
{
	std::vector<AlgebraCube> live;
	for (auto &cube : cover.cubes)
		if (!cube.zero)
			live.push_back(cube);
	if (live.empty())
		return false;
	return intersect_cubes(live).literals.empty();
}

void normalize_cover(AlgebraCover &cover)
{
	std::map<std::string, AlgebraCube> unique;
	for (auto &cube : cover.cubes) {
		if (cube.zero)
			continue;
		unique[cube_key(cube)] = cube;
	}
	cover.cubes.clear();
	for (auto &item : unique)
		cover.cubes.push_back(item.second);
}

bool cover_contains_cube_exact(const AlgebraCover &cover, const AlgebraCube &cube)
{
	std::string key = cube_key(cube);
	for (auto &item : cover.cubes)
		if (cube_key(item) == key)
			return true;
	return false;
}

std::vector<AlgebraLiteral> cover_support_literals(const AlgebraCover &cover)
{
	std::map<std::string, AlgebraLiteral> support;
	for (auto &cube : cover.cubes) {
		if (cube.zero)
			continue;
		for (auto &lit : cube.literals)
			support[literal_key(lit.second)] = lit.second;
	}
	std::vector<AlgebraLiteral> out;
	for (auto &item : support)
		out.push_back(item.second);
	return out;
}

bool ports_match(RTLIL::Cell *cell, RTLIL::IdString port, const SigMap &sigmap, const SigSpec &sig)
{
	return cell->hasPort(port) && sigmap(cell->getPort(port)) == sig;
}

bool get_single_driver_cell(const SigSpec &sig, RTLIL::Cell *&driver, const SigMap &sigmap, ModIndex &mi, CellTypes &ct)
{
	driver = nullptr;
	SigSpec flat = sigmap(sig);
	if (GetSize(flat) != 1)
		return false;

	RTLIL::IdString driver_port;
	for (auto bit : flat) {
		for (auto &pi : mi.query_ports(bit)) {
			if (pi.cell == nullptr || !ct.cell_known(pi.cell->type) || !ct.cell_output(pi.cell->type, pi.port))
				continue;
			if (driver != nullptr && driver != pi.cell)
				return false;
			driver = pi.cell;
			driver_port = pi.port;
		}
	}

	return driver != nullptr && ports_match(driver, driver_port, sigmap, flat);
}

bool collect_term(const SigSpec &sig,
                  RTLIL::IdString child_op,
                  AlgebraCube &term,
                  const SigMap &sigmap,
                  ModIndex &mi,
                  CellTypes &ct,
                  std::set<RTLIL::Cell*> &seen)
{
	SigSpec flat = sigmap(sig);
	if (GetSize(flat) != 1)
		return false;

	RTLIL::Cell *driver = nullptr;
	if (!get_single_driver_cell(flat, driver, sigmap, mi, ct))
		return add_literal(term, flat, false);

	if (seen.count(driver))
		return false;
	seen.insert(driver);

	bool ok = true;
	if (driver->type == child_op) {
		ok = collect_term(sigmap(driver->getPort(ID::A)), child_op, term, sigmap, mi, ct, seen) &&
		     collect_term(sigmap(driver->getPort(ID::B)), child_op, term, sigmap, mi, ct, seen);
	} else if (driver->type == ID($not)) {
		SigSpec a = sigmap(driver->getPort(ID::A));
		ok = GetSize(a) == 1 && add_literal(term, a, true);
	} else {
		ok = add_literal(term, flat, false);
	}

	seen.erase(driver);
	return ok;
}

bool collect_cover(const SigSpec &sig,
                   RTLIL::IdString root_op,
                   RTLIL::IdString child_op,
                   AlgebraCover &cover,
                   const SigMap &sigmap,
                   ModIndex &mi,
                   CellTypes &ct,
                   std::set<RTLIL::Cell*> &seen)
{
	SigSpec flat = sigmap(sig);
	if (GetSize(flat) != 1)
		return false;

	RTLIL::Cell *driver = nullptr;
	if (get_single_driver_cell(flat, driver, sigmap, mi, ct) && driver->type == root_op) {
		if (seen.count(driver))
			return false;
		seen.insert(driver);
		bool ok = collect_cover(sigmap(driver->getPort(ID::A)), root_op, child_op, cover, sigmap, mi, ct, seen) &&
		          collect_cover(sigmap(driver->getPort(ID::B)), root_op, child_op, cover, sigmap, mi, ct, seen);
		seen.erase(driver);
		return ok;
	}

	AlgebraCube term;
	std::set<RTLIL::Cell*> term_seen;
	if (!collect_term(flat, child_op, term, sigmap, mi, ct, term_seen))
		return false;
	cover.cubes.push_back(term);
	return true;
}

DivisionResult divide_by_cube(const AlgebraCover &dividend, const AlgebraCube &divisor)
{
	DivisionResult result;
	result.valid = true;
	for (auto &cube : dividend.cubes) {
		if (cube_contains_cube(cube, divisor))
			result.quotient.cubes.push_back(subtract_cube(cube, divisor));
		else
			result.remainder.cubes.push_back(cube);
	}
	normalize_cover(result.quotient);
	normalize_cover(result.remainder);
	return result;
}

DivisionResult algebraic_division(const AlgebraCover &dividend, const AlgebraCover &divisor)
{
	DivisionResult result;
	AlgebraCover normalized_dividend = dividend;
	AlgebraCover normalized_divisor = divisor;
	normalize_cover(normalized_dividend);
	normalize_cover(normalized_divisor);

	if (normalized_divisor.cubes.empty()) {
		result.remainder = normalized_dividend;
		return result;
	}

	const AlgebraCube &first_divisor_cube = normalized_divisor.cubes.front();
	std::map<std::string, AlgebraCube> quotient_candidates;
	for (auto &cube : normalized_dividend.cubes) {
		if (!cube_contains_cube(cube, first_divisor_cube))
			continue;
		AlgebraCube candidate = subtract_cube(cube, first_divisor_cube);
		bool ok = true;
		for (auto &divisor_cube : normalized_divisor.cubes) {
			AlgebraCube product;
			multiply_cubes(candidate, divisor_cube, product);
			if (product.zero || !cover_contains_cube_exact(normalized_dividend, product)) {
				ok = false;
				break;
			}
		}
		if (ok)
			quotient_candidates[cube_key(candidate)] = candidate;
	}

	std::set<std::string> covered_products;
	for (auto &q : quotient_candidates) {
		for (auto &d : normalized_divisor.cubes) {
			AlgebraCube product;
			multiply_cubes(q.second, d, product);
			if (!product.zero)
				covered_products.insert(cube_key(product));
		}
		result.quotient.cubes.push_back(q.second);
	}

	for (auto &cube : normalized_dividend.cubes)
		if (covered_products.count(cube_key(cube)) == 0)
			result.remainder.cubes.push_back(cube);

	normalize_cover(result.quotient);
	normalize_cover(result.remainder);
	result.valid = true;
	return result;
}

void enumerate_kernels_rec(const AlgebraCover &cover,
                           const std::vector<AlgebraLiteral> &support,
                           int index,
                           AlgebraCube &cokernel,
                           std::map<std::string, KernelPair> &out)
{
	if (index == (int)support.size()) {
		DivisionResult division = divide_by_cube(cover, cokernel);
		if (division.quotient.cubes.size() >= 2 && cube_free(division.quotient)) {
			KernelPair pair;
			pair.kernel = division.quotient;
			pair.cokernel = cokernel;
			out[cover_key(pair.kernel) + "/" + cube_key(pair.cokernel)] = pair;
		}
		return;
	}

	enumerate_kernels_rec(cover, support, index + 1, cokernel, out);

	AlgebraCube with_literal = cokernel;
	if (add_literal(with_literal, support[index].sig, support[index].neg) && !with_literal.zero) {
		DivisionResult quick = divide_by_cube(cover, with_literal);
		if (quick.quotient.cubes.size() >= 2)
			enumerate_kernels_rec(cover, support, index + 1, with_literal, out);
	}
}

std::vector<KernelPair> enumerate_kernels(const AlgebraCover &cover, int max_literals)
{
	std::vector<KernelPair> result;
	std::vector<AlgebraLiteral> support = cover_support_literals(cover);
	if ((int)support.size() > max_literals)
		return result;

	std::map<std::string, KernelPair> unique;
	AlgebraCube one;
	enumerate_kernels_rec(cover, support, 0, one, unique);
	for (auto &item : unique)
		result.push_back(item.second);
	return result;
}

int nary_cost(int count)
{
	return count <= 1 ? 0 : count - 1;
}

int cover_cost(const AlgebraCover &cover)
{
	int live_terms = 0;
	int cost = 0;
	for (auto &cube : cover.cubes) {
		if (cube.zero)
			continue;
		live_terms++;
		cost += (int)cube.literals.size();
		cost += nary_cost((int)cube.literals.size());
	}
	cost += nary_cost(live_terms);
	return cost;
}

bool cover_is_child_identity(const AlgebraCover &cover)
{
	return cover.cubes.size() == 1 && !cover.cubes.front().zero && cover.cubes.front().literals.empty();
}

int factored_cost(const FactorCandidate &candidate)
{
	int cost = 0;
	bool quotient_identity = cover_is_child_identity(candidate.quotient);
	bool divisor_identity = cover_is_child_identity(candidate.divisor);
	if (!quotient_identity)
		cost += cover_cost(candidate.quotient);
	if (!divisor_identity)
		cost += cover_cost(candidate.divisor);
	if (!quotient_identity && !divisor_identity)
		cost += 1;
	int outer_terms = (int)candidate.remainder.cubes.size() + 1;
	cost += nary_cost(outer_terms);
	for (auto &cube : candidate.remainder.cubes) {
		AlgebraCover single;
		single.cubes.push_back(cube);
		cost += cover_cost(single);
	}
	return cost;
}

bool find_best_candidate(const AlgebraCover &cover, int max_kernel_literals, FactorCandidate &best)
{
	AlgebraCover normalized = cover;
	normalize_cover(normalized);
	int original_cost = cover_cost(normalized);
	std::vector<KernelPair> kernels = enumerate_kernels(normalized, max_kernel_literals);

	std::map<std::string, AlgebraCover> divisors;
	for (auto &pair : kernels) {
		divisors[cover_key(pair.kernel)] = pair.kernel;
		AlgebraCover cokernel_cover;
		cokernel_cover.cubes.push_back(pair.cokernel);
		divisors[cover_key(cokernel_cover)] = cokernel_cover;
	}

	for (auto &item : divisors) {
		AlgebraCover divisor = item.second;
		normalize_cover(divisor);
		if (divisor.cubes.empty())
			continue;
		DivisionResult division = algebraic_division(normalized, divisor);
		if (!division.valid || division.quotient.cubes.empty())
			continue;

		FactorCandidate candidate;
		candidate.divisor = divisor;
		candidate.quotient = division.quotient;
		candidate.remainder = division.remainder;
		candidate.gain = original_cost - factored_cost(candidate);
		if (candidate.gain > best.gain)
			best = candidate;
	}

	return best.gain > 0;
}

RTLIL::Cell *add_binary_by_type(RTLIL::Module *module, RTLIL::IdString type, const SigSpec &a, const SigSpec &b, const SigSpec &y)
{
	if (type == ID($and))
		return module->addAnd(NEW_ID, a, b, y);
	if (type == ID($or))
		return module->addOr(NEW_ID, a, b, y);
	log_abort();
	return nullptr;
}

SigSpec child_identity(RTLIL::IdString child_op)
{
	return SigSpec(child_op == ID($and) ? State::S1 : State::S0);
}

SigSpec root_identity(RTLIL::IdString root_op)
{
	return SigSpec(root_op == ID($or) ? State::S0 : State::S1);
}

SigSpec build_literal(RTLIL::Module *module, const AlgebraLiteral &lit)
{
	if (!lit.neg)
		return lit.sig;
	RTLIL::Wire *out = module->addWire(NEW_ID, 1);
	module->addNot(NEW_ID, lit.sig, out);
	return out;
}

SigSpec build_cube(RTLIL::Module *module, const AlgebraCube &cube, RTLIL::IdString child_op)
{
	if (cube.zero)
		return child_op == ID($and) ? SigSpec(State::S0) : SigSpec(State::S1);
	if (cube.literals.empty())
		return child_identity(child_op);

	std::vector<SigSpec> items;
	for (auto &lit : cube.literals)
		items.push_back(build_literal(module, lit.second));

	SigSpec accum = items.front();
	for (size_t i = 1; i < items.size(); i++) {
		RTLIL::Wire *out = module->addWire(NEW_ID, 1);
		add_binary_by_type(module, child_op, accum, items[i], out);
		accum = out;
	}
	return accum;
}

SigSpec build_cover(RTLIL::Module *module, const AlgebraCover &cover, RTLIL::IdString root_op, RTLIL::IdString child_op)
{
	if (cover.cubes.empty())
		return root_identity(root_op);

	std::vector<SigSpec> terms;
	for (auto &cube : cover.cubes)
		terms.push_back(build_cube(module, cube, child_op));

	SigSpec accum = terms.front();
	for (size_t i = 1; i < terms.size(); i++) {
		RTLIL::Wire *out = module->addWire(NEW_ID, 1);
		add_binary_by_type(module, root_op, accum, terms[i], out);
		accum = out;
	}
	return accum;
}

SigSpec build_factored(RTLIL::Module *module,
                       const FactorCandidate &candidate,
                       RTLIL::IdString root_op,
                       RTLIL::IdString child_op)
{
	SigSpec factored;
	if (cover_is_child_identity(candidate.quotient)) {
		factored = build_cover(module, candidate.divisor, root_op, child_op);
	} else if (cover_is_child_identity(candidate.divisor)) {
		factored = build_cover(module, candidate.quotient, root_op, child_op);
	} else {
		SigSpec q = build_cover(module, candidate.quotient, root_op, child_op);
		SigSpec d = build_cover(module, candidate.divisor, root_op, child_op);
		RTLIL::Wire *out = module->addWire(NEW_ID, 1);
		add_binary_by_type(module, child_op, q, d, out);
		factored = out;
	}

	std::vector<SigSpec> outer;
	for (auto &cube : candidate.remainder.cubes)
		outer.push_back(build_cube(module, cube, child_op));
	outer.push_back(factored);

	SigSpec accum = outer.front();
	for (size_t i = 1; i < outer.size(); i++) {
		RTLIL::Wire *out = module->addWire(NEW_ID, 1);
		add_binary_by_type(module, root_op, accum, outer[i], out);
		accum = out;
	}
	return accum;
}

bool root_ops(RTLIL::Cell *cell, RTLIL::IdString &root_op, RTLIL::IdString &child_op)
{
	if (cell->type == ID($or)) {
		root_op = ID($or);
		child_op = ID($and);
		return true;
	}
	if (cell->type == ID($and)) {
		root_op = ID($and);
		child_op = ID($or);
		return true;
	}
	return false;
}

bool find_rewrite_candidate(RTLIL::Module *module,
                            RTLIL::Cell *cell,
                            const SigMap &sigmap,
                            ModIndex &mi,
                            CellTypes &ct,
                            int max_kernel_literals,
                            RewriteCandidate &rewrite)
{
	RTLIL::IdString root_op, child_op;
	if (!root_ops(cell, root_op, child_op))
		return false;

	SigSpec y = sigmap(cell->getPort(ID::Y));
	if (GetSize(y) != 1)
		return false;

	AlgebraCover cover;
	std::set<RTLIL::Cell*> seen;
	if (!collect_cover(y, root_op, child_op, cover, sigmap, mi, ct, seen))
		return false;
	normalize_cover(cover);
	if (cover.cubes.size() < 2)
		return false;

	FactorCandidate factor;
	if (!find_best_candidate(cover, max_kernel_literals, factor))
		return false;

	rewrite.cell = cell;
	rewrite.cell_name = cell->name;
	rewrite.y = y;
	rewrite.root_op = root_op;
	rewrite.child_op = child_op;
	rewrite.factor = factor;
	rewrite.gain = factor.gain;
	return true;
}

void apply_rewrite_candidate(RTLIL::Module *module, const RewriteCandidate &candidate)
{
	module->connect(candidate.y, build_factored(module, candidate.factor, candidate.root_op, candidate.child_op));
	module->remove(candidate.cell);
}

void run_multilevel_pass(RTLIL::Design *design, const char *label, int max_kernel_literals, int max_iter)
{
	log_header(design, "Executing %s pass (multi-level algebraic optimization).\n", label);
	int total_roots = 0;
	int total_gain = 0;

	for (auto module : design->selected_whole_modules_warn()) {
		if (module->has_processes_warn())
			continue;

		bool module_changed = false;
		for (int iter = 0; iter < max_iter; iter++) {
			bool changed_this_iter = false;
			RewriteCandidate candidate;
			bool found_candidate = false;

			{
				SigMap sigmap(module);
				ModIndex mi(module);
				CellTypes ct;
				ct.setup_internals();

				std::vector<RTLIL::Cell*> cells = module->selected_cells();
				for (auto cell : cells) {
					if (!find_rewrite_candidate(module, cell, sigmap, mi, ct, max_kernel_literals, candidate))
						continue;
					found_candidate = true;
					break;
				}
			}

			if (found_candidate) {
				apply_rewrite_candidate(module, candidate);
				total_roots++;
				total_gain += candidate.gain;
				module_changed = true;
				changed_this_iter = true;
				design->scratchpad_set_bool("opt.did_something", true);
				log("  Module %s: factored root %s with algebraic gain %d.\n", log_id(module), log_id(candidate.cell_name), candidate.gain);
			}

			if (!changed_this_iter)
				break;
		}

		if (module_changed) {
			module->fixup_ports();
			module->check();
		}
	}

	log("%s total factored roots: %d\n", label, total_roots);
	log("%s total estimated gain: %d\n", label, total_gain);
	log_header(design, "%s has been done!\n", label);
}

size_t parse_multilevel_args(const std::vector<std::string> &args,
                             int &max_kernel_literals,
                             int &max_iter)
{
	size_t argidx;
	for (argidx = 1; argidx < args.size(); argidx++) {
		if (args[argidx] == "-max_kernel_literals" && argidx + 1 < args.size()) {
			max_kernel_literals = atoi(args[++argidx].c_str());
			continue;
		}
		if (args[argidx] == "-max_iter" && argidx + 1 < args.size()) {
			max_iter = atoi(args[++argidx].c_str());
			continue;
		}
		break;
	}

	if (max_kernel_literals < 1 || max_kernel_literals > 20)
		log_cmd_error("multi-level EDA_opt pass: -max_kernel_literals must be in the range 1..20.\n");
	if (max_iter < 1)
		log_cmd_error("multi-level EDA_opt pass: -max_iter must be positive.\n");
	return argidx;
}
} // namespace

struct EDAOptRewritePass : public Pass
{
	EDAOptRewritePass() : Pass("my_opt_rewrite", "multi-level algebraic rewriting using kernels and division") {}

	void help() override
	{
		log("\n");
		log("    my_opt_rewrite [options] [selection]\n");
		log("\n");
		log("Run multi-level algebraic optimization on one-bit $and/$or/$not SOP/POS\n");
		log("logic. The pass enumerates cube-free kernels, performs exact algebraic\n");
		log("division by candidate divisors, and iteratively rebuilds profitable\n");
		log("factored networks.\n");
		log("\n");
		log("    -max_kernel_literals <N>    exact kernel enumeration bound. Default: 12.\n");
		log("    -max_iter <N>               maximum rewrites per module. Default: 32.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		int max_kernel_literals = 12;
		int max_iter = 32;
		size_t argidx = parse_multilevel_args(args, max_kernel_literals, max_iter);
		extra_args(args, argidx, design);
		run_multilevel_pass(design, "my_opt_REWRITE", max_kernel_literals, max_iter);
	}
} EDAOptRewritePass;

struct EDAOptKernelPass : public Pass
{
	EDAOptKernelPass() : Pass("my_opt_kernel", "multi-level kernel extraction and factoring") {}

	void help() override
	{
		log("\n");
		log("    my_opt_kernel [options] [selection]\n");
		log("\n");
		log("Enumerate all cube-free kernels within the support bound and use the\n");
		log("best positive-gain kernel/co-kernel factorization for multi-level logic.\n");
		log("\n");
		log("    -max_kernel_literals <N>    exact kernel enumeration bound. Default: 12.\n");
		log("    -max_iter <N>               maximum rewrites per module. Default: 32.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		int max_kernel_literals = 12;
		int max_iter = 32;
		size_t argidx = parse_multilevel_args(args, max_kernel_literals, max_iter);
		extra_args(args, argidx, design);
		run_multilevel_pass(design, "my_opt_KERNEL", max_kernel_literals, max_iter);
	}
} EDAOptKernelPass;

struct EDAOptAlgDivPass : public Pass
{
	EDAOptAlgDivPass() : Pass("my_opt_algdiv", "multi-level algebraic division factoring") {}

	void help() override
	{
		log("\n");
		log("    my_opt_algdiv [options] [selection]\n");
		log("\n");
		log("Perform exact algebraic division of local SOP/POS covers by all generated\n");
		log("kernel and co-kernel divisors, then rebuild the best positive-gain\n");
		log("quotient * divisor + remainder form.\n");
		log("\n");
		log("    -max_kernel_literals <N>    exact kernel enumeration bound. Default: 12.\n");
		log("    -max_iter <N>               maximum rewrites per module. Default: 32.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		int max_kernel_literals = 12;
		int max_iter = 32;
		size_t argidx = parse_multilevel_args(args, max_kernel_literals, max_iter);
		extra_args(args, argidx, design);
		run_multilevel_pass(design, "my_opt_ALGDIV", max_kernel_literals, max_iter);
	}
} EDAOptAlgDivPass;

PRIVATE_NAMESPACE_END
