#include "kernel/register.h"
#include "kernel/log.h"
#include "kernel/sigtools.h"
#include "kernel/celltypes.h"

#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

namespace
{
bool is_supported_type(RTLIL::IdString type)
{
	return type.in(
		ID($not), ID($pos), ID($neg),
		ID($and), ID($or), ID($xor), ID($xnor),
		ID($logic_not), ID($logic_and), ID($logic_or),
		ID($reduce_and), ID($reduce_or), ID($reduce_xor), ID($reduce_xnor), ID($reduce_bool),
		ID($shl), ID($shr), ID($sshl), ID($sshr), ID($shift), ID($shiftx),
		ID($add), ID($sub), ID($mul), ID($div), ID($mod), ID($pow),
		ID($eq), ID($ne), ID($lt), ID($le), ID($ge), ID($gt),
		ID($mux), ID($pmux)
	);
}

void add_uses(dict<SigBit, int> &uses, const SigSpec &sig)
{
	for (auto bit : sig)
		uses[bit]++;
}

std::vector<SigBit> collect_input_bits(RTLIL::Cell *cell, const SigMap &sigmap, const CellTypes &ct)
{
	std::vector<SigBit> bits;
	for (auto &conn : cell->connections()) {
		if (!ct.cell_known(cell->type)) {
			for (auto bit : sigmap(conn.second))
				bits.push_back(bit);
			continue;
		}

		if (!ct.cell_input(cell->type, conn.first))
			continue;

		for (auto bit : sigmap(conn.second))
			bits.push_back(bit);
	}
	return bits;
}

std::vector<SigBit> collect_output_bits(RTLIL::Cell *cell, const SigMap &sigmap, const CellTypes &ct)
{
	std::vector<SigBit> bits;
	for (auto &conn : cell->connections()) {
		if (!ct.cell_known(cell->type) || !ct.cell_output(cell->type, conn.first))
			continue;

		for (auto bit : sigmap(conn.second))
			bits.push_back(bit);
	}
	return bits;
}

bool outputs_are_dead(RTLIL::Cell *cell, const SigMap &sigmap, const CellTypes &ct, const dict<SigBit, int> &uses)
{
	for (auto bit : collect_output_bits(cell, sigmap, ct)) {
		auto it = uses.find(bit);
		if (it != uses.end() && it->second > 0)
			return false;
	}
	return true;
}
} // namespace

struct EDAOptDcePass : public Pass
{
	EDAOptDcePass() : Pass("my_opt_dce", "remove unused combinational cells") {}

	void help() override
	{
		log("\n");
		log("    my_opt_dce [selection]\n");
		log("\n");
		log("This pass performs a simple dead-code elimination on selected\n");
		log("combinational internal cells. Cells whose outputs do not contribute to\n");
		log("any other cell input or module output are removed recursively.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		extra_args(args, 1, design);
		log_header(design, "Executing my_opt_DCE pass (dead combinational cell elimination).\n");

		int total_removed = 0;

		for (auto module : design->selected_whole_modules_warn()) {
			if (module->has_processes_warn())
				continue;

			SigMap sigmap(module);
			CellTypes ct;
			ct.setup_internals();

			std::vector<RTLIL::Cell*> candidates;
			std::unordered_set<RTLIL::Cell*> candidate_set;
			dict<SigBit, RTLIL::Cell*> bit_driver;
			dict<SigBit, int> uses;

			for (auto cell : module->selected_cells()) {
				if (!is_supported_type(cell->type))
					continue;

				candidates.push_back(cell);
				candidate_set.insert(cell);
				for (auto bit : collect_output_bits(cell, sigmap, ct))
					bit_driver[bit] = cell;
			}

			for (auto cell : module->cells()) {
				if (!ct.cell_known(cell->type)) {
					for (auto &conn : cell->connections())
						add_uses(uses, sigmap(conn.second));
					continue;
				}

				for (auto &conn : cell->connections()) {
					if (ct.cell_input(cell->type, conn.first))
						add_uses(uses, sigmap(conn.second));
				}
			}

			for (auto wire : module->wires()) {
				if (!wire->port_output)
					continue;
				add_uses(uses, sigmap(SigSpec(wire)));
			}

			std::queue<RTLIL::Cell*> dead_queue;
			for (auto cell : candidates) {
				if (outputs_are_dead(cell, sigmap, ct, uses))
					dead_queue.push(cell);
			}

			std::unordered_set<RTLIL::Cell*> removed;
			while (!dead_queue.empty()) {
				RTLIL::Cell *cell = dead_queue.front();
				dead_queue.pop();

				if (removed.count(cell))
					continue;
				if (!outputs_are_dead(cell, sigmap, ct, uses))
					continue;

				removed.insert(cell);
				for (auto bit : collect_input_bits(cell, sigmap, ct)) {
					auto it = uses.find(bit);
					if (it == uses.end() || it->second == 0)
						continue;
					it->second--;
					if (it->second == 0 && bit_driver.count(bit) && candidate_set.count(bit_driver.at(bit)))
						dead_queue.push(bit_driver.at(bit));
				}
			}

			for (auto cell : removed)
				module->remove(cell);

			if (!removed.empty()) {
				module->fixup_ports();
				module->check();
				design->scratchpad_set_bool("opt.did_something", true);
				total_removed += (int)removed.size();
				log("  Module %s: removed %d dead cells.\n", log_id(module), (int)removed.size());
			}
		}

		log("my_opt_DCE total removed cells: %d\n", total_removed);
		log_header(design, "my_opt_DCE has been done!\n");
	}
} EDAOptDcePass;

PRIVATE_NAMESPACE_END
