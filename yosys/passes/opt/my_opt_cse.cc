#include "kernel/register.h"
#include "kernel/log.h"
#include "kernel/sigtools.h"

#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

namespace
{
bool is_supported_type(RTLIL::IdString type)
{
	return type.in(ID($not), ID($and), ID($or), ID($xor), ID($add), ID($sub), ID($mul), ID($shl), ID($shr));
}

bool is_commutative_type(RTLIL::IdString type)
{
	return type.in(ID($and), ID($or), ID($xor), ID($add), ID($mul));
}

std::string sig_key(const SigSpec &sig)
{
	return log_signal(sig);
}

void append_param(std::ostringstream &os, RTLIL::Cell *cell, RTLIL::IdString id)
{
	if (cell->parameters.count(id))
		os << "|" << id.str() << "=" << cell->parameters.at(id).as_int();
}

std::string build_key(RTLIL::Cell *cell, const SigMap &sigmap)
{
	if (!is_supported_type(cell->type))
		return std::string();

	std::ostringstream os;
	os << cell->type.str();
	append_param(os, cell, ID::A_SIGNED);
	append_param(os, cell, ID::B_SIGNED);
	append_param(os, cell, ID::A_WIDTH);
	append_param(os, cell, ID::B_WIDTH);
	append_param(os, cell, ID::Y_WIDTH);

	if (cell->type == ID($not)) {
		os << "|A=" << sig_key(sigmap(cell->getPort(ID::A)));
		return os.str();
	}

	std::string a = sig_key(sigmap(cell->getPort(ID::A)));
	std::string b = sig_key(sigmap(cell->getPort(ID::B)));
	if (is_commutative_type(cell->type) && b < a)
		std::swap(a, b);

	os << "|A=" << a;
	os << "|B=" << b;
	return os.str();
}
} // namespace

struct EDAOptCsePass : public Pass
{
	EDAOptCsePass() : Pass("my_opt_cse", "merge duplicated combinational cells") {}

	void help() override
	{
		log("\n");
		log("    my_opt_cse [selection]\n");
		log("\n");
		log("This pass performs a simple common-subexpression elimination on a subset\n");
		log("of internal combinational cells. When two selected cells have the same\n");
		log("type, parameters and canonicalized inputs, one of them is removed and its\n");
		log("output is connected to the representative cell output.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		extra_args(args, 1, design);
		log_header(design, "Executing my_opt_CSE pass (duplicate cell elimination).\n");

		int total_merged = 0;

		for (auto module : design->selected_whole_modules_warn()) {
			if (module->has_processes_warn())
				continue;

			SigMap sigmap(module);
			std::unordered_map<std::string, RTLIL::Cell*> representative;
			std::vector<RTLIL::Cell*> cells_to_remove;

			for (auto cell : module->selected_cells()) {
				std::string key = build_key(cell, sigmap);
				if (key.empty())
					continue;

				auto it = representative.find(key);
				if (it == representative.end()) {
					representative.emplace(key, cell);
					continue;
				}

				module->connect(sigmap(cell->getPort(ID::Y)), sigmap(it->second->getPort(ID::Y)));
				cells_to_remove.push_back(cell);
				total_merged++;
			}

			for (auto cell : cells_to_remove)
				module->remove(cell);

			if (!cells_to_remove.empty()) {
				module->fixup_ports();
				module->check();
				design->scratchpad_set_bool("opt.did_something", true);
				log("  Module %s: merged %d duplicated cells.\n", log_id(module), (int)cells_to_remove.size());
			}
		}

		log("my_opt_CSE total merged cells: %d\n", total_merged);
		log_header(design, "my_opt_CSE has been done!\n");
	}
} EDAOptCsePass;

PRIVATE_NAMESPACE_END
