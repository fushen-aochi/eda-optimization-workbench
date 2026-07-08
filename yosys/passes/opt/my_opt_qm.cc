#include "kernel/register.h"
#include "kernel/log.h"
#include "kernel/sigtools.h"
#include "kernel/modtools.h"
#include "kernel/celltypes.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

namespace
{
struct SupportVar
{
	SigBit bit;
	std::string key;
};

struct BitValue
{
	bool known = true;
	bool value = false;
};

struct Cube
{
	uint32_t value = 0;
	uint32_t dc_mask = 0;
	bool used = false;
};

struct SearchState
{
	std::vector<Cube> current;
	std::vector<Cube> best;
	int best_cost = 0x7fffffff;
};

struct QmRewrite
{
	SigSig connection;
	RTLIL::Cell *old_root = nullptr;
};

struct ConeContext
{
	RTLIL::Module *module = nullptr;
	SigMap sigmap;
	ModIndex index;
	CellTypes celltypes;
	dict<SigBit, RTLIL::Cell*> driver;
	std::vector<SupportVar> vars;
	std::map<std::string, int> var_index;
	int max_vars = 8;

	ConeContext(RTLIL::Module *module, int max_vars)
	    : module(module), sigmap(module), index(module), max_vars(max_vars)
	{
		celltypes.setup_internals();
		for (auto cell : module->cells()) {
			for (auto &conn : cell->connections()) {
				if (!celltypes.cell_known(cell->type) || !celltypes.cell_output(cell->type, conn.first))
					continue;
				for (auto bit : sigmap(conn.second))
					driver[bit] = cell;
			}
		}
	}
};

std::string bit_key(const SigBit &bit)
{
	return log_signal(bit);
}

bool is_const_bit(const SigBit &bit, bool &value)
{
	if (bit.wire != nullptr)
		return false;
	if (bit.data == State::S0) {
		value = false;
		return true;
	}
	if (bit.data == State::S1) {
		value = true;
		return true;
	}
	return false;
}

bool is_unknown_const_bit(const SigBit &bit)
{
	return bit.wire == nullptr && bit.data != State::S0 && bit.data != State::S1;
}

bool get_var_index(ConeContext &ctx, const SigBit &bit, int &index)
{
	std::string key = bit_key(bit);
	auto it = ctx.var_index.find(key);
	if (it != ctx.var_index.end()) {
		index = it->second;
		return true;
	}
	if (int(ctx.vars.size()) >= ctx.max_vars)
		return false;
	index = int(ctx.vars.size());
	ctx.var_index[key] = index;
	ctx.vars.push_back({bit, key});
	return true;
}

BitValue bit_from_bool(bool value)
{
	BitValue out;
	out.value = value;
	return out;
}

std::vector<BitValue> unknown_bits(int width)
{
	std::vector<BitValue> bits(width);
	for (auto &bit : bits)
		bit.known = false;
	return bits;
}

uint64_t bits_to_u64(const std::vector<BitValue> &bits, bool &known)
{
	known = true;
	uint64_t value = 0;
	if (bits.size() > 63) {
		known = false;
		return 0;
	}
	for (size_t i = 0; i < bits.size(); i++) {
		if (!bits[i].known) {
			known = false;
			return 0;
		}
		if (bits[i].value)
			value |= uint64_t(1) << i;
	}
	return value;
}

std::vector<BitValue> u64_to_bits(uint64_t value, int width)
{
	std::vector<BitValue> bits(width);
	for (int i = 0; i < width; i++)
		bits[i] = bit_from_bool(((value >> i) & 1) != 0);
	return bits;
}

bool has_signed_param(RTLIL::Cell *cell)
{
	return (cell->parameters.count(ID::A_SIGNED) && cell->getParam(ID::A_SIGNED).as_bool()) ||
	       (cell->parameters.count(ID::B_SIGNED) && cell->getParam(ID::B_SIGNED).as_bool());
}

std::vector<BitValue> normalize_width(std::vector<BitValue> bits, int width)
{
	if (int(bits.size()) > width)
		bits.resize(width);
	while (int(bits.size()) < width)
		bits.push_back(bit_from_bool(false));
	return bits;
}

bool eval_sig(ConeContext &ctx,
              const SigSpec &sig,
              uint32_t minterm,
              std::set<RTLIL::Cell*> &visiting,
              std::vector<BitValue> &value);

bool eval_bit(ConeContext &ctx,
              const SigBit &raw_bit,
              uint32_t minterm,
              std::set<RTLIL::Cell*> &visiting,
              BitValue &value)
{
	SigBit bit = ctx.sigmap(raw_bit);
	bool const_value = false;
	if (is_const_bit(bit, const_value)) {
		value = bit_from_bool(const_value);
		return true;
	}
	if (is_unknown_const_bit(bit)) {
		value.known = false;
		value.value = false;
		return true;
	}

	auto driver_it = ctx.driver.find(bit);
	if (driver_it == ctx.driver.end()) {
		int index = -1;
		if (!get_var_index(ctx, bit, index))
			return false;
		value = bit_from_bool(((minterm >> index) & 1) != 0);
		return true;
	}

	RTLIL::Cell *cell = driver_it->second;
	if (visiting.count(cell))
		return false;
	if (!cell->hasPort(ID::Y))
		return false;
	visiting.insert(cell);

	SigSpec y = ctx.sigmap(cell->getPort(ID::Y));
	std::vector<BitValue> y_value;
	bool ok = false;

	if (cell->type == ID($not)) {
		std::vector<BitValue> a;
		ok = eval_sig(ctx, cell->getPort(ID::A), minterm, visiting, a);
		if (ok) {
			y_value.resize(GetSize(y));
			a = normalize_width(a, GetSize(y));
			for (int i = 0; i < GetSize(y); i++) {
				y_value[i].known = a[i].known;
				y_value[i].value = !a[i].value;
			}
		}
	} else if (cell->type.in(ID($and), ID($or), ID($xor), ID($xnor))) {
		std::vector<BitValue> a, b;
		ok = eval_sig(ctx, cell->getPort(ID::A), minterm, visiting, a) &&
		     eval_sig(ctx, cell->getPort(ID::B), minterm, visiting, b);
		if (ok) {
			int width = GetSize(y);
			a = normalize_width(a, width);
			b = normalize_width(b, width);
			y_value.resize(width);
			for (int i = 0; i < width; i++) {
				y_value[i].known = a[i].known && b[i].known;
				if (!y_value[i].known)
					continue;
				if (cell->type == ID($and))
					y_value[i].value = a[i].value && b[i].value;
				else if (cell->type == ID($or))
					y_value[i].value = a[i].value || b[i].value;
				else if (cell->type == ID($xor))
					y_value[i].value = a[i].value != b[i].value;
				else
					y_value[i].value = a[i].value == b[i].value;
			}
		}
	} else if (cell->type.in(ID($reduce_and), ID($reduce_or), ID($reduce_xor), ID($reduce_xnor), ID($reduce_bool))) {
		std::vector<BitValue> a;
		ok = eval_sig(ctx, cell->getPort(ID::A), minterm, visiting, a);
		if (ok) {
			BitValue reduced;
			reduced.known = true;
			if (cell->type == ID($reduce_and)) {
				reduced.value = true;
				for (auto bit_value : a) {
					reduced.known &= bit_value.known;
					reduced.value &= bit_value.value;
				}
			} else if (cell->type == ID($reduce_or) || cell->type == ID($reduce_bool)) {
				reduced.value = false;
				for (auto bit_value : a) {
					reduced.known &= bit_value.known;
					reduced.value |= bit_value.value;
				}
			} else {
				reduced.value = false;
				for (auto bit_value : a) {
					reduced.known &= bit_value.known;
					reduced.value = reduced.value != bit_value.value;
				}
				if (cell->type == ID($reduce_xnor))
					reduced.value = !reduced.value;
			}
			y_value.assign(GetSize(y), reduced);
		}
	} else if (cell->type.in(ID($logic_not), ID($logic_and), ID($logic_or))) {
		std::vector<BitValue> a, b;
		ok = eval_sig(ctx, cell->getPort(ID::A), minterm, visiting, a);
		if (ok && cell->type != ID($logic_not))
			ok = eval_sig(ctx, cell->getPort(ID::B), minterm, visiting, b);
		if (ok) {
			bool known_a = false, known_b = false;
			uint64_t va = bits_to_u64(a, known_a);
			uint64_t vb = cell->type == ID($logic_not) ? 0 : bits_to_u64(b, known_b);
			BitValue out;
			out.known = known_a && (cell->type == ID($logic_not) || known_b);
			if (out.known) {
				if (cell->type == ID($logic_not))
					out.value = va == 0;
				else if (cell->type == ID($logic_and))
					out.value = (va != 0) && (vb != 0);
				else
					out.value = (va != 0) || (vb != 0);
			}
			y_value.assign(GetSize(y), out);
		}
	} else if (cell->type.in(ID($add), ID($sub), ID($mul), ID($div), ID($mod), ID($shl), ID($shr), ID($sshl), ID($sshr))) {
		if (has_signed_param(cell)) {
			visiting.erase(cell);
			return false;
		}
		std::vector<BitValue> a, b;
		ok = eval_sig(ctx, cell->getPort(ID::A), minterm, visiting, a) &&
		     eval_sig(ctx, cell->getPort(ID::B), minterm, visiting, b);
		if (ok) {
			bool known_a = false, known_b = false;
			uint64_t va = bits_to_u64(a, known_a);
			uint64_t vb = bits_to_u64(b, known_b);
			if (known_a && known_b) {
				uint64_t result = 0;
				if (cell->type == ID($add))
					result = va + vb;
				else if (cell->type == ID($sub))
					result = va - vb;
				else if (cell->type == ID($mul))
					result = va * vb;
				else if (cell->type == ID($div))
					result = vb == 0 ? 0 : va / vb;
				else if (cell->type == ID($mod))
					result = vb == 0 ? 0 : va % vb;
				else if (cell->type.in(ID($shl), ID($sshl)))
					result = vb >= 64 ? 0 : va << vb;
				else
					result = vb >= 64 ? 0 : va >> vb;
				y_value = u64_to_bits(result, GetSize(y));
			} else {
				y_value = unknown_bits(GetSize(y));
			}
		}
	} else if (cell->type.in(ID($eq), ID($ne), ID($lt), ID($le), ID($gt), ID($ge))) {
		if (has_signed_param(cell)) {
			visiting.erase(cell);
			return false;
		}
		std::vector<BitValue> a, b;
		ok = eval_sig(ctx, cell->getPort(ID::A), minterm, visiting, a) &&
		     eval_sig(ctx, cell->getPort(ID::B), minterm, visiting, b);
		if (ok) {
			bool known_a = false, known_b = false;
			uint64_t va = bits_to_u64(a, known_a);
			uint64_t vb = bits_to_u64(b, known_b);
			BitValue out;
			out.known = known_a && known_b;
			if (out.known) {
				if (cell->type == ID($eq))
					out.value = va == vb;
				else if (cell->type == ID($ne))
					out.value = va != vb;
				else if (cell->type == ID($lt))
					out.value = va < vb;
				else if (cell->type == ID($le))
					out.value = va <= vb;
				else if (cell->type == ID($gt))
					out.value = va > vb;
				else
					out.value = va >= vb;
			}
			y_value.assign(GetSize(y), out);
		}
	} else if (cell->type == ID($mux)) {
		std::vector<BitValue> a, b, s;
		ok = eval_sig(ctx, cell->getPort(ID::A), minterm, visiting, a) &&
		     eval_sig(ctx, cell->getPort(ID::B), minterm, visiting, b) &&
		     eval_sig(ctx, cell->getPort(ID::S), minterm, visiting, s);
		if (ok) {
			a = normalize_width(a, GetSize(y));
			b = normalize_width(b, GetSize(y));
			if (s.empty() || !s[0].known)
				y_value = unknown_bits(GetSize(y));
			else
				y_value = s[0].value ? b : a;
		}
	}

	visiting.erase(cell);
	if (!ok)
		return false;

	SigBit target = ctx.sigmap(raw_bit);
	for (int i = 0; i < GetSize(y); i++) {
		if (ctx.sigmap(y[i]) == target) {
			value = y_value[i];
			return true;
		}
	}
	return false;
}

bool eval_sig(ConeContext &ctx,
              const SigSpec &sig,
              uint32_t minterm,
              std::set<RTLIL::Cell*> &visiting,
              std::vector<BitValue> &value)
{
	value.clear();
	SigSpec flat = ctx.sigmap(sig);
	for (auto bit : flat) {
		BitValue bit_value;
		if (!eval_bit(ctx, bit, minterm, visiting, bit_value))
			return false;
		value.push_back(bit_value);
	}
	return true;
}

bool same_cube(const Cube &a, const Cube &b)
{
	return a.value == b.value && a.dc_mask == b.dc_mask;
}

bool operator==(const Cube &a, const Cube &b)
{
	return same_cube(a, b);
}

void add_unique_cube(std::vector<Cube> &cubes, const Cube &cube)
{
	for (const auto &existing : cubes) {
		if (same_cube(existing, cube))
			return;
	}
	cubes.push_back(cube);
}

bool same_cube_vector(const std::vector<Cube> &a, const std::vector<Cube> &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++)
		if (!same_cube(a[i], b[i]))
			return false;
	return true;
}

bool combine_cubes(const Cube &a, const Cube &b, Cube &out)
{
	if (a.dc_mask != b.dc_mask)
		return false;
	uint32_t diff = (a.value ^ b.value) & ~a.dc_mask;
	if (diff == 0 || (diff & (diff - 1)) != 0)
		return false;
	out.value = a.value & ~diff;
	out.dc_mask = a.dc_mask | diff;
	out.used = false;
	return true;
}

std::vector<Cube> prime_implicants(const std::set<uint32_t> &on_set)
{
	std::vector<Cube> current;
	for (uint32_t m : on_set) {
		Cube cube;
		cube.value = m;
		current.push_back(cube);
	}

	std::vector<Cube> primes;
	while (!current.empty()) {
		for (auto &cube : current)
			cube.used = false;

		std::vector<Cube> next;
		for (size_t i = 0; i < current.size(); i++) {
			for (size_t j = i + 1; j < current.size(); j++) {
				Cube combined;
				if (!combine_cubes(current[i], current[j], combined))
					continue;
				current[i].used = true;
				current[j].used = true;
				add_unique_cube(next, combined);
			}
		}

		for (const auto &cube : current) {
			if (!cube.used)
				add_unique_cube(primes, cube);
		}
		current = next;
	}
	return primes;
}

bool covers(const Cube &cube, uint32_t minterm)
{
	return ((minterm ^ cube.value) & ~cube.dc_mask) == 0;
}

int literal_count(const Cube &cube, int num_vars)
{
	int count = 0;
	for (int i = 0; i < num_vars; i++) {
		if (((cube.dc_mask >> i) & 1) == 0)
			count++;
	}
	return count;
}

int sop_cost(const std::vector<Cube> &cover, int num_vars)
{
	int cost = int(cover.size()) * 64;
	for (const auto &cube : cover)
		cost += literal_count(cube, num_vars);
	return cost;
}

std::set<uint32_t> covered_minterms(const std::vector<Cube> &cover, const std::set<uint32_t> &on_set)
{
	std::set<uint32_t> covered;
	for (const auto &cube : cover) {
		for (uint32_t minterm : on_set) {
			if (covers(cube, minterm))
				covered.insert(minterm);
		}
	}
	return covered;
}

bool minterm_set_subset(const std::set<uint32_t> &lhs, const std::set<uint32_t> &rhs)
{
	for (uint32_t value : lhs) {
		if (rhs.count(value) == 0)
			return false;
	}
	return true;
}

std::set<uint32_t> cube_cover_set(const Cube &cube, const std::set<uint32_t> &on_set)
{
	std::set<uint32_t> result;
	for (uint32_t minterm : on_set) {
		if (covers(cube, minterm))
			result.insert(minterm);
	}
	return result;
}

void simplify_prime_chart(std::vector<Cube> &primes, std::set<uint32_t> &remaining_on_set, std::vector<Cube> &selected, int num_vars)
{
	bool changed = true;
	while (changed) {
		changed = false;

		for (auto minterm_it = remaining_on_set.begin(); minterm_it != remaining_on_set.end();) {
			uint32_t minterm = *minterm_it;
			int count = 0;
			Cube only;
			for (const auto &cube : primes) {
				if (covers(cube, minterm)) {
					count++;
					only = cube;
				}
			}
			if (count == 1) {
				add_unique_cube(selected, only);
				std::set<uint32_t> covered = cube_cover_set(only, remaining_on_set);
				for (uint32_t covered_minterm : covered)
					remaining_on_set.erase(covered_minterm);
				changed = true;
				minterm_it = remaining_on_set.begin();
				continue;
			}
			++minterm_it;
		}

		std::vector<Cube> filtered_primes;
		for (const auto &cube : primes) {
			bool useful = false;
			for (uint32_t minterm : remaining_on_set) {
				if (covers(cube, minterm)) {
					useful = true;
					break;
				}
			}
			if (useful)
				add_unique_cube(filtered_primes, cube);
		}
		if (filtered_primes.size() != primes.size()) {
			primes = filtered_primes;
			changed = true;
		}

		std::vector<bool> remove_prime(primes.size(), false);
		for (size_t i = 0; i < primes.size(); i++) {
			std::set<uint32_t> cover_i = cube_cover_set(primes[i], remaining_on_set);
			for (size_t j = 0; j < primes.size(); j++) {
				if (i == j || remove_prime[j])
					continue;
				std::set<uint32_t> cover_j = cube_cover_set(primes[j], remaining_on_set);
				if (minterm_set_subset(cover_j, cover_i) &&
				    literal_count(primes[i], num_vars) <= literal_count(primes[j], num_vars)) {
					remove_prime[j] = true;
					changed = true;
				}
			}
		}
		filtered_primes.clear();
		for (size_t i = 0; i < primes.size(); i++)
			if (!remove_prime[i])
				add_unique_cube(filtered_primes, primes[i]);
		primes = filtered_primes;

		std::vector<uint32_t> rows(remaining_on_set.begin(), remaining_on_set.end());
		std::set<uint32_t> remove_rows;
		for (uint32_t row_i : rows) {
			std::set<int> covering_i;
			for (size_t p = 0; p < primes.size(); p++)
				if (covers(primes[p], row_i))
					covering_i.insert((int)p);
			for (uint32_t row_j : rows) {
				if (row_i == row_j || remove_rows.count(row_j))
					continue;
				std::set<int> covering_j;
				for (size_t p = 0; p < primes.size(); p++)
					if (covers(primes[p], row_j))
						covering_j.insert((int)p);
				bool subset = true;
				for (int idx : covering_i) {
					if (covering_j.count(idx) == 0) {
						subset = false;
						break;
					}
				}
				if (subset) {
					remove_rows.insert(row_j);
					changed = true;
				}
			}
		}
		for (uint32_t row : remove_rows)
			remaining_on_set.erase(row);
	}
}

void exact_cover_search(const std::vector<Cube> &primes, const std::set<uint32_t> &on_set, int num_vars, SearchState &state)
{
	int current_cost = sop_cost(state.current, num_vars);
	if (current_cost >= state.best_cost)
		return;

	std::set<uint32_t> covered = covered_minterms(state.current, on_set);
	if (covered.size() == on_set.size()) {
		state.best = state.current;
		state.best_cost = current_cost;
		return;
	}

	uint32_t target = 0;
	size_t fewest_choices = std::numeric_limits<size_t>::max();
	for (uint32_t minterm : on_set) {
		if (covered.count(minterm))
			continue;
		size_t choices = 0;
		for (const auto &cube : primes) {
			if (covers(cube, minterm))
				choices++;
		}
		if (choices < fewest_choices) {
			fewest_choices = choices;
			target = minterm;
		}
	}

	std::vector<Cube> candidates;
	for (const auto &cube : primes) {
		if (covers(cube, target))
			candidates.push_back(cube);
	}
	std::sort(candidates.begin(), candidates.end(), [&](const Cube &a, const Cube &b) {
		return literal_count(a, num_vars) < literal_count(b, num_vars);
	});

	for (const auto &cube : candidates) {
		bool duplicate = false;
		for (const auto &selected : state.current) {
			if (same_cube(selected, cube)) {
				duplicate = true;
				break;
			}
		}
		if (duplicate)
			continue;
		state.current.push_back(cube);
		exact_cover_search(primes, on_set, num_vars, state);
		state.current.pop_back();
	}
}

std::vector<Cube> exact_minimum_cover(const std::vector<Cube> &primes, const std::set<uint32_t> &on_set, int num_vars)
{
	std::vector<Cube> reduced_primes = primes;
	std::set<uint32_t> remaining_on_set = on_set;
	std::vector<Cube> essential;
	simplify_prime_chart(reduced_primes, remaining_on_set, essential, num_vars);

	if (remaining_on_set.empty())
		return essential;

	SearchState state;
	state.current = essential;
	state.best_cost = 0x7fffffff;
	exact_cover_search(reduced_primes, remaining_on_set, num_vars, state);
	return state.best;
}

SigSpec build_cube(RTLIL::Module *module, const Cube &cube, const std::vector<SupportVar> &vars)
{
	std::vector<SigSpec> factors;
	for (size_t i = 0; i < vars.size(); i++) {
		if (((cube.dc_mask >> i) & 1) != 0)
			continue;
		if (((cube.value >> i) & 1) != 0) {
			factors.push_back(SigSpec(vars[i].bit));
		} else {
			RTLIL::Wire *inv = module->addWire(NEW_ID, 1);
			module->addNot(NEW_ID, SigSpec(vars[i].bit), inv);
			factors.push_back(inv);
		}
	}
	if (factors.empty())
		return SigSpec(State::S1);
	if (factors.size() == 1)
		return factors.front();

	SigSpec accum = factors.front();
	for (size_t i = 1; i < factors.size(); i++) {
		RTLIL::Wire *out = module->addWire(NEW_ID, 1);
		module->addAnd(NEW_ID, accum, factors[i], out);
		accum = out;
	}
	return accum;
}

SigSpec build_sop(RTLIL::Module *module, const std::vector<Cube> &cover, const std::vector<SupportVar> &vars)
{
	if (cover.empty())
		return SigSpec(State::S0);

	std::vector<SigSpec> terms;
	for (const auto &cube : cover)
		terms.push_back(build_cube(module, cube, vars));

	if (terms.size() == 1)
		return terms.front();

	SigSpec accum = terms.front();
	for (size_t i = 1; i < terms.size(); i++) {
		RTLIL::Wire *out = module->addWire(NEW_ID, 1);
		module->addOr(NEW_ID, accum, terms[i], out);
		accum = out;
	}
	return accum;
}

int estimate_network_cost(const std::vector<Cube> &cover, int num_vars)
{
	int cost = 0;
	for (const auto &cube : cover) {
		int lits = literal_count(cube, num_vars);
		if (lits == 0)
			continue;
		for (int i = 0; i < num_vars; i++) {
			if (((cube.dc_mask >> i) & 1) == 0 && ((cube.value >> i) & 1) == 0)
				cost++;
		}
		if (lits > 1)
			cost += lits - 1;
	}
	if (cover.size() > 1)
		cost += int(cover.size()) - 1;
	return cost;
}

void collect_cone_cells(ConeContext &ctx, const SigBit &raw_bit, std::set<RTLIL::Cell*> &cells, std::set<RTLIL::Cell*> &visiting)
{
	SigBit bit = ctx.sigmap(raw_bit);
	auto driver_it = ctx.driver.find(bit);
	if (driver_it == ctx.driver.end())
		return;

	RTLIL::Cell *cell = driver_it->second;
	if (visiting.count(cell))
		return;
	visiting.insert(cell);
	cells.insert(cell);

	for (auto &conn : cell->connections()) {
		if (ctx.celltypes.cell_known(cell->type) && !ctx.celltypes.cell_input(cell->type, conn.first))
			continue;
		for (auto input_bit : ctx.sigmap(conn.second))
			collect_cone_cells(ctx, input_bit, cells, visiting);
	}

	visiting.erase(cell);
}

int estimate_existing_cone_cost(ConeContext &ctx, const SigBit &output_bit)
{
	std::set<RTLIL::Cell*> cells;
	std::set<RTLIL::Cell*> visiting;
	collect_cone_cells(ctx, output_bit, cells, visiting);
	return (int)cells.size();
}

bool optimize_output_bit(ConeContext &ctx, const SigBit &output_bit, SigSpec &replacement, int &vars_used, int &cover_terms)
{
	ctx.vars.clear();
	ctx.var_index.clear();

	std::set<uint32_t> on_set;
	std::set<uint32_t> dc_set;
	const int max_minterms = 1 << ctx.max_vars;
	for (int minterm = 0; minterm < max_minterms; minterm++) {
		std::set<RTLIL::Cell*> visiting;
		BitValue value;
		if (!eval_bit(ctx, output_bit, uint32_t(minterm), visiting, value))
			return false;
		if (!value.known)
			dc_set.insert(uint32_t(minterm));
		else if (value.value)
			on_set.insert(uint32_t(minterm));
	}

	vars_used = int(ctx.vars.size());
	if (vars_used == 0)
		return false;

	std::set<uint32_t> trimmed_on_set;
	std::set<uint32_t> trimmed_dc_set;
	const int real_minterms = 1 << vars_used;
	for (uint32_t minterm : on_set) {
		if (int(minterm) < real_minterms)
			trimmed_on_set.insert(minterm);
	}
	for (uint32_t minterm : dc_set) {
		if (int(minterm) < real_minterms)
			trimmed_dc_set.insert(minterm);
	}

	std::vector<Cube> cover;
	if (!trimmed_on_set.empty()) {
		std::set<uint32_t> implicant_seed = trimmed_on_set;
		implicant_seed.insert(trimmed_dc_set.begin(), trimmed_dc_set.end());
		std::vector<Cube> primes = prime_implicants(implicant_seed);
		cover = exact_minimum_cover(primes, trimmed_on_set, vars_used);
		if (cover.empty())
			return false;
	}

	cover_terms = int(cover.size());
	if (estimate_network_cost(cover, vars_used) >= estimate_existing_cone_cost(ctx, output_bit))
		return false;

	replacement = build_sop(ctx.module, cover, ctx.vars);
	return true;
}

std::vector<SigBit> selected_two_level_target_bits(RTLIL::Module *module, const SigMap &sigmap, const CellTypes &celltypes)
{
	std::vector<SigBit> bits;
	for (auto wire : module->wires()) {
		if (!wire->port_output)
			continue;
		for (auto bit : sigmap(SigSpec(wire)))
			bits.push_back(bit);
	}
	std::sort(bits.begin(), bits.end());
	bits.erase(std::unique(bits.begin(), bits.end()), bits.end());
	return bits;
}
} // namespace

struct EDAOptQmPass : public Pass
{
	EDAOptQmPass() : Pass("my_opt_qm", "complete Quine-McCluskey minimization for RTLIL combinational functions") {}

	void help() override
	{
		log("\n");
		log("    my_opt_qm [options] [selection]\n");
		log("\n");
		log("This pass performs exact Quine-McCluskey two-level minimization for\n");
		log("selected combinational RTLIL output cones. It evaluates supported cell cones\n");
		log("semantically, enumerates the truth table, computes prime implicants,\n");
		log("selects essential implicants, solves the remaining cover exactly with\n");
		log("branch-and-bound, then rebuilds the output bit as minimized SOP logic.\n");
		log("\n");
		log("Supported cone cells: $not, $and, $or, $xor, $xnor, reductions,\n");
		log("logic ops, unsigned add/sub/mul/div/mod/shift, comparisons, and $mux.\n");
		log("\n");
		log("    -max_vars <N>\n");
		log("        maximum Boolean support size per optimized output bit. Default: 8.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		int max_vars = 8;
		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++) {
			if (args[argidx] == "-max_vars" && argidx + 1 < args.size()) {
				max_vars = atoi(args[++argidx].c_str());
				continue;
			}
			break;
		}
		extra_args(args, argidx, design);

		if (max_vars < 1 || max_vars > 12)
			log_cmd_error("my_opt_qm: -max_vars must be in the range 1..12.\n");

		log_header(design, "Executing my_opt_QM pass (exact Quine-McCluskey minimization).\n");

		int total_bits_seen = 0;
		int total_bits_rewritten = 0;

		for (auto module : design->selected_whole_modules_warn()) {
			if (module->has_processes_warn())
				continue;

			ConeContext ctx(module, max_vars);
			std::vector<QmRewrite> rewrites;

			for (auto bit : selected_two_level_target_bits(module, ctx.sigmap, ctx.celltypes)) {
				total_bits_seen++;
				SigSpec replacement;
				int vars_used = 0;
				int cover_terms = 0;
				if (!optimize_output_bit(ctx, bit, replacement, vars_used, cover_terms))
					continue;
				RTLIL::Cell *old_root = nullptr;
				auto driver_it = ctx.driver.find(ctx.sigmap(bit));
				if (driver_it != ctx.driver.end())
					old_root = driver_it->second;
				rewrites.push_back({SigSig(bit, replacement), old_root});
				total_bits_rewritten++;
				log("  Module %s: target bit %s minimized with %d vars and %d cover terms.\n",
				    log_id(module), log_signal(bit), vars_used, cover_terms);
			}

			for (auto &rewrite : rewrites)
				module->connect(rewrite.connection.first, rewrite.connection.second);

			for (auto &rewrite : rewrites) {
				if (rewrite.old_root != nullptr && module->cells_.count(rewrite.old_root->name))
					module->remove(rewrite.old_root);
			}

			if (!rewrites.empty()) {
				module->fixup_ports();
				module->check();
				design->scratchpad_set_bool("opt.did_something", true);
			}
		}

		log("my_opt_QM target bits examined: %d\n", total_bits_seen);
		log("my_opt_QM target bits rewritten: %d\n", total_bits_rewritten);
		log_header(design, "my_opt_QM has been done!\n");
	}
} EDAOptQmPass;

struct CoverLiteral
{
	SigSpec sig;
	bool neg = false;
};

struct CoverTerm
{
	std::map<std::string, CoverLiteral> literals;
	bool zero = false;
};

std::string cover_sig_key(const SigSpec &sig)
{
	return log_signal(sig);
}

bool cover_ports_match(RTLIL::Cell *cell, RTLIL::IdString port, const SigMap &sigmap, const SigSpec &sig)
{
	return cell->hasPort(port) && sigmap(cell->getPort(port)) == sig;
}

bool cover_driver_cell(const SigSpec &sig, RTLIL::Cell *&driver, const SigMap &sigmap, ModIndex &mi, CellTypes &ct)
{
	driver = nullptr;
	SigSpec flat = sigmap(sig);
	if (GetSize(flat) != 1)
		return false;

	for (auto bit : flat) {
		RTLIL::Cell *bit_driver = nullptr;
		RTLIL::IdString bit_port;
		for (auto &pi : mi.query_ports(bit)) {
			if (pi.cell == nullptr || !ct.cell_known(pi.cell->type) || !ct.cell_output(pi.cell->type, pi.port))
				continue;
			if (bit_driver != nullptr && bit_driver != pi.cell)
				return false;
			bit_driver = pi.cell;
			bit_port = pi.port;
		}
		if (bit_driver == nullptr || !cover_ports_match(bit_driver, bit_port, sigmap, flat))
			return false;
		driver = bit_driver;
	}
	return driver != nullptr;
}

bool cover_add_literal(CoverTerm &term, const SigSpec &sig, bool neg)
{
	bool const_value = false;
	SigSpec flat = sig;
	if (GetSize(flat) == 1 && is_const_bit(flat[0], const_value)) {
		if (const_value == neg)
			term.zero = true;
		return true;
	}

	std::string key = cover_sig_key(sig);
	auto it = term.literals.find(key);
	if (it == term.literals.end()) {
		term.literals[key] = {sig, neg};
		return true;
	}
	if (it->second.neg != neg)
		term.zero = true;
	return true;
}

bool cover_collect_product(const SigSpec &sig, CoverTerm &term, const SigMap &sigmap, ModIndex &mi, CellTypes &ct, std::set<RTLIL::Cell*> &seen)
{
	SigSpec flat = sigmap(sig);
	if (GetSize(flat) != 1)
		return false;

	RTLIL::Cell *driver = nullptr;
	if (!cover_driver_cell(flat, driver, sigmap, mi, ct))
		return cover_add_literal(term, flat, false);

	if (seen.count(driver))
		return false;
	seen.insert(driver);

	bool ok = true;
	if (driver->type == ID($and)) {
		ok = cover_collect_product(sigmap(driver->getPort(ID::A)), term, sigmap, mi, ct, seen) &&
		     cover_collect_product(sigmap(driver->getPort(ID::B)), term, sigmap, mi, ct, seen);
	} else if (driver->type == ID($not)) {
		SigSpec a = sigmap(driver->getPort(ID::A));
		ok = GetSize(a) == 1 && cover_add_literal(term, a, true);
	} else {
		ok = cover_add_literal(term, flat, false);
	}

	seen.erase(driver);
	return ok;
}

bool cover_collect_sop(const SigSpec &sig, std::vector<CoverTerm> &terms, const SigMap &sigmap, ModIndex &mi, CellTypes &ct, std::set<RTLIL::Cell*> &seen)
{
	SigSpec flat = sigmap(sig);
	if (GetSize(flat) != 1)
		return false;

	RTLIL::Cell *driver = nullptr;
	if (cover_driver_cell(flat, driver, sigmap, mi, ct) && driver->type == ID($or)) {
		if (seen.count(driver))
			return false;
		seen.insert(driver);
		bool ok = cover_collect_sop(sigmap(driver->getPort(ID::A)), terms, sigmap, mi, ct, seen) &&
		          cover_collect_sop(sigmap(driver->getPort(ID::B)), terms, sigmap, mi, ct, seen);
		seen.erase(driver);
		return ok;
	}

	CoverTerm term;
	std::set<RTLIL::Cell*> product_seen;
	if (!cover_collect_product(flat, term, sigmap, mi, ct, product_seen))
		return false;
	terms.push_back(term);
	return true;
}

SigSpec cover_literal_signal(RTLIL::Module *module, const CoverLiteral &lit)
{
	if (!lit.neg)
		return lit.sig;
	RTLIL::Wire *out = module->addWire(NEW_ID, 1);
	module->addNot(NEW_ID, lit.sig, out);
	return out;
}

SigSpec cover_build_product(RTLIL::Module *module, const CoverTerm &term)
{
	if (term.zero)
		return SigSpec(State::S0);
	if (term.literals.empty())
		return SigSpec(State::S1);

	std::vector<SigSpec> factors;
	for (auto &lit : term.literals)
		factors.push_back(cover_literal_signal(module, lit.second));

	SigSpec accum = factors.front();
	for (size_t i = 1; i < factors.size(); i++) {
		RTLIL::Wire *out = module->addWire(NEW_ID, 1);
		module->addAnd(NEW_ID, accum, factors[i], out);
		accum = out;
	}
	return accum;
}

SigSpec cover_build_sop(RTLIL::Module *module, const std::vector<CoverTerm> &terms)
{
	if (terms.empty())
		return SigSpec(State::S0);

	std::vector<SigSpec> product_sigs;
	for (auto &term : terms)
		product_sigs.push_back(cover_build_product(module, term));

	SigSpec accum = product_sigs.front();
	for (size_t i = 1; i < product_sigs.size(); i++) {
		RTLIL::Wire *out = module->addWire(NEW_ID, 1);
		module->addOr(NEW_ID, accum, product_sigs[i], out);
		accum = out;
	}
	return accum;
}

int cover_literal_count(const CoverTerm &term)
{
	return term.zero ? 0 : (int)term.literals.size();
}

int cover_matrix_cost(const std::vector<CoverTerm> &terms)
{
	int cost = int(terms.size()) * 64;
	for (auto &term : terms)
		cost += cover_literal_count(term);
	return cost;
}

bool cover_build_support(const std::vector<CoverTerm> &terms, std::vector<CoverLiteral> &support, std::map<std::string, int> &support_index)
{
	support.clear();
	support_index.clear();
	for (auto &term : terms) {
		if (term.zero)
			continue;
		for (auto &lit : term.literals) {
			if (support_index.count(lit.first) != 0)
				continue;
			support_index[lit.first] = (int)support.size();
			support.push_back(lit.second);
		}
	}
	return true;
}

Cube cover_term_to_cube(const CoverTerm &term, const std::map<std::string, int> &support_index, int num_vars)
{
	Cube cube;
	cube.value = 0;
	cube.dc_mask = (uint32_t(1) << num_vars) - 1;
	if (term.zero)
		return cube;

	for (auto &lit : term.literals) {
		auto it = support_index.find(lit.first);
		if (it == support_index.end())
			continue;
		uint32_t bit = uint32_t(1) << it->second;
		cube.dc_mask &= ~bit;
		if (!lit.second.neg)
			cube.value |= bit;
	}
	return cube;
}

CoverTerm cover_cube_to_term(const Cube &cube, const std::vector<CoverLiteral> &support)
{
	CoverTerm term;
	for (size_t i = 0; i < support.size(); i++) {
		if (((cube.dc_mask >> i) & 1) != 0)
			continue;
		CoverLiteral lit = support[i];
		lit.neg = ((cube.value >> i) & 1) == 0;
		term.literals[cover_sig_key(lit.sig)] = lit;
	}
	return term;
}

std::set<uint32_t> cover_terms_to_on_set(const std::vector<CoverTerm> &terms, const std::map<std::string, int> &support_index, int num_vars)
{
	std::set<uint32_t> on_set;
	uint32_t limit = uint32_t(1) << num_vars;
	for (auto &term : terms) {
		if (term.zero)
			continue;
		Cube cube = cover_term_to_cube(term, support_index, num_vars);
		for (uint32_t minterm = 0; minterm < limit; minterm++)
			if (covers(cube, minterm))
				on_set.insert(minterm);
	}
	return on_set;
}

bool cover_cube_is_valid_implicant(const Cube &cube, const std::set<uint32_t> &on_set, int num_vars)
{
	uint32_t limit = uint32_t(1) << num_vars;
	for (uint32_t minterm = 0; minterm < limit; minterm++)
		if (covers(cube, minterm) && on_set.count(minterm) == 0)
			return false;
	return true;
}

bool cover_cubes_cover_on_set(const std::vector<Cube> &cubes, const std::set<uint32_t> &on_set)
{
	for (uint32_t minterm : on_set) {
		bool hit = false;
		for (auto &cube : cubes) {
			if (covers(cube, minterm)) {
				hit = true;
				break;
			}
		}
		if (!hit)
			return false;
	}
	return true;
}

bool cover_cube_contains_cube(const Cube &general, const Cube &specific, int num_vars)
{
	for (int i = 0; i < num_vars; i++) {
		uint32_t bit = uint32_t(1) << i;
		if ((general.dc_mask & bit) != 0)
			continue;
		if ((specific.dc_mask & bit) != 0)
			return false;
		if ((general.value & bit) != (specific.value & bit))
			return false;
	}
	return true;
}

std::vector<Cube> cover_normalize_cube_matrix(std::vector<Cube> cubes, int num_vars)
{
	std::vector<Cube> unique;
	for (auto &cube : cubes)
		add_unique_cube(unique, cube);

	std::vector<bool> remove(unique.size(), false);
	for (size_t i = 0; i < unique.size(); i++) {
		for (size_t j = 0; j < unique.size(); j++) {
			if (i == j || remove[i])
				continue;
			if (cover_cube_contains_cube(unique[j], unique[i], num_vars))
				remove[i] = true;
		}
	}

	std::vector<Cube> out;
	for (size_t i = 0; i < unique.size(); i++)
		if (!remove[i])
			out.push_back(unique[i]);
	return out;
}

std::vector<Cube> cover_expand_matrix(std::vector<Cube> cubes, const std::set<uint32_t> &on_set, int num_vars)
{
	for (auto &cube : cubes) {
		bool progress = true;
		while (progress) {
			progress = false;
			std::vector<int> vars;
			for (int i = 0; i < num_vars; i++)
				if (((cube.dc_mask >> i) & 1) == 0)
					vars.push_back(i);
			std::sort(vars.begin(), vars.end(), [&](int a, int b) {
				Cube ca = cube, cb = cube;
				ca.dc_mask |= uint32_t(1) << a;
				cb.dc_mask |= uint32_t(1) << b;
				return literal_count(ca, num_vars) < literal_count(cb, num_vars);
			});
			for (int var : vars) {
				Cube candidate = cube;
				uint32_t bit = uint32_t(1) << var;
				candidate.dc_mask |= bit;
				candidate.value &= ~bit;
				if (cover_cube_is_valid_implicant(candidate, on_set, num_vars)) {
					cube = candidate;
					progress = true;
				}
			}
		}
	}
	return cover_normalize_cube_matrix(cubes, num_vars);
}

std::vector<Cube> cover_irredundant_matrix(std::vector<Cube> cubes, const std::set<uint32_t> &on_set, int num_vars)
{
	cubes = cover_normalize_cube_matrix(cubes, num_vars);
	bool progress = true;
	while (progress) {
		progress = false;
		std::sort(cubes.begin(), cubes.end(), [&](const Cube &a, const Cube &b) {
			return literal_count(a, num_vars) > literal_count(b, num_vars);
		});
		for (size_t i = 0; i < cubes.size(); i++) {
			std::vector<Cube> trial;
			for (size_t j = 0; j < cubes.size(); j++)
				if (i != j)
					trial.push_back(cubes[j]);
			if (cover_cubes_cover_on_set(trial, on_set)) {
				cubes = trial;
				progress = true;
				break;
			}
		}
	}
	return cubes;
}

std::vector<Cube> cover_reduce_matrix(const std::vector<Cube> &cubes, const std::set<uint32_t> &on_set, int num_vars)
{
	std::vector<Cube> reduced = cubes;
	for (size_t i = 0; i < cubes.size(); i++) {
		bool progress = true;
		while (progress) {
			progress = false;
			std::vector<int> vars;
			for (int var = 0; var < num_vars; var++) {
				if (((reduced[i].dc_mask >> var) & 1) != 0)
					vars.push_back(var);
			}
			for (int var : vars) {
				uint32_t bit = uint32_t(1) << var;
				for (int value = 0; value <= 1; value++) {
					Cube candidate = reduced[i];
					candidate.dc_mask &= ~bit;
					if (value)
						candidate.value |= bit;
					else
						candidate.value &= ~bit;

					std::vector<Cube> trial = reduced;
					trial[i] = candidate;
					if (cover_cubes_cover_on_set(trial, on_set)) {
						reduced[i] = candidate;
						progress = true;
						break;
					}
				}
				if (progress)
					break;
			}
		}
	}
	return cover_normalize_cube_matrix(reduced, num_vars);
}

std::vector<CoverTerm> cover_cubes_to_terms(const std::vector<Cube> &cubes, const std::vector<CoverLiteral> &support)
{
	std::vector<CoverTerm> terms;
	for (auto &cube : cubes)
		terms.push_back(cover_cube_to_term(cube, support));
	return terms;
}

std::vector<CoverTerm> cover_matrix_minimize(const std::vector<CoverTerm> &terms,
                                             const std::vector<CoverLiteral> &support,
                                             const std::map<std::string, int> &support_index,
                                             int max_iter)
{
	int num_vars = (int)support.size();
	std::set<uint32_t> on_set = cover_terms_to_on_set(terms, support_index, num_vars);
	std::vector<Cube> current;
	for (auto &term : terms)
		if (!term.zero)
			current.push_back(cover_term_to_cube(term, support_index, num_vars));
	current = cover_normalize_cube_matrix(current, num_vars);

	std::vector<Cube> best = current;
	int best_cost = cover_matrix_cost(cover_cubes_to_terms(best, support));

	for (int iter = 0; iter < max_iter; iter++) {
		current = cover_expand_matrix(current, on_set, num_vars);
		current = cover_irredundant_matrix(current, on_set, num_vars);
		int current_cost = cover_matrix_cost(cover_cubes_to_terms(current, support));
		if (current_cost < best_cost) {
			best = current;
			best_cost = current_cost;
		}

		std::vector<Cube> reduced = cover_reduce_matrix(current, on_set, num_vars);
		if (reduced.empty() || same_cube_vector(reduced, current))
			break;
		current = reduced;
	}

	best = cover_expand_matrix(best, on_set, num_vars);
	best = cover_irredundant_matrix(best, on_set, num_vars);
	return cover_cubes_to_terms(best, support);
}

struct EDAOptCoverPass : public Pass
{
	EDAOptCoverPass() : Pass("my_opt_cover", "two-level heuristic cover minimization with expand/reduce/irredundant") {}

	void help() override
	{
		log("\n");
		log("    my_opt_cover [options] [selection]\n");
		log("\n");
		log("Flatten one-bit $and/$or/$not SOP covers into a cube matrix, then run\n");
		log("two-level heuristic minimization: EXPAND, IRREDUNDANT, and REDUCE.\n");
		log("All legality checks are exact within the configured support bound.\n");
		log("\n");
		log("    -max_vars <N>\n");
		log("        maximum support size for exact cover-matrix enumeration. Default: 12.\n");
		log("\n");
		log("    -max_iter <N>\n");
		log("        maximum expand/irredundant/reduce iterations. Default: 16.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		int max_vars = 12;
		int max_iter = 16;
		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++) {
			if (args[argidx] == "-max_vars" && argidx + 1 < args.size()) {
				max_vars = atoi(args[++argidx].c_str());
				continue;
			}
			if (args[argidx] == "-max_iter" && argidx + 1 < args.size()) {
				max_iter = atoi(args[++argidx].c_str());
				continue;
			}
			break;
		}
		extra_args(args, argidx, design);

		if (max_vars < 1 || max_vars > 20)
			log_cmd_error("my_opt_cover: -max_vars must be in the range 1..20.\n");
		if (max_iter < 1)
			log_cmd_error("my_opt_cover: -max_iter must be positive.\n");

		log_header(design, "Executing my_opt_COVER pass (two-level heuristic cover minimization).\n");
		int total_roots = 0;
		int total_removed_terms = 0;
		int total_cost_gain = 0;

		for (auto module : design->selected_whole_modules_warn()) {
			if (module->has_processes_warn())
				continue;

			SigMap sigmap(module);
			ModIndex mi(module);
			CellTypes ct;
			ct.setup_internals();
			std::vector<RTLIL::Cell*> roots_to_remove;
			std::vector<RTLIL::Cell*> root_cells;
			for (auto cell : module->selected_cells())
				if (cell->type == ID($or))
					root_cells.push_back(cell);

			for (auto cell : root_cells) {
				SigSpec y = sigmap(cell->getPort(ID::Y));
				if (GetSize(y) != 1)
					continue;

				std::vector<CoverTerm> terms;
				std::set<RTLIL::Cell*> seen;
				if (!cover_collect_sop(y, terms, sigmap, mi, ct, seen) || terms.size() < 2)
					continue;

				std::vector<CoverLiteral> support;
				std::map<std::string, int> support_index;
				cover_build_support(terms, support, support_index);
				if (support.empty() || (int)support.size() > max_vars)
					continue;

				std::vector<CoverTerm> kept = cover_matrix_minimize(terms, support, support_index, max_iter);
				int old_cost = cover_matrix_cost(terms);
				int new_cost = cover_matrix_cost(kept);
				if (new_cost >= old_cost)
					continue;

				module->connect(y, cover_build_sop(module, kept));
				roots_to_remove.push_back(cell);
				total_roots++;
				total_removed_terms += int(terms.size() - kept.size());
				total_cost_gain += old_cost - new_cost;
				log("  Module %s: SOP root %s minimized from %d terms/%d cost to %d terms/%d cost.\n",
				    log_id(module), log_id(cell->name), (int)terms.size(), old_cost, (int)kept.size(), new_cost);
			}

			for (auto cell : roots_to_remove)
				module->remove(cell);
			if (!roots_to_remove.empty()) {
				module->fixup_ports();
				module->check();
				design->scratchpad_set_bool("opt.did_something", true);
				log("  Module %s: optimized %d cover roots.\n", log_id(module), (int)roots_to_remove.size());
			}
		}

		log("my_opt_COVER total optimized roots: %d\n", total_roots);
		log("my_opt_COVER total removed product terms: %d\n", total_removed_terms);
		log("my_opt_COVER total cover cost gain: %d\n", total_cost_gain);
		log_header(design, "my_opt_COVER has been done!\n");
	}
} EDAOptCoverPass;

PRIVATE_NAMESPACE_END
