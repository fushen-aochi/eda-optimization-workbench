#include "kernel/register.h"
#include "kernel/log.h"
#include "kernel/sigtools.h"

#include <stdint.h>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

namespace
{
bool const_to_u64(const SigSpec &sig, uint64_t &value)
{
	value = 0;
	if (GetSize(sig) == 0 || GetSize(sig) > 63 || !sig.is_fully_const())
		return false;

	Const bits = sig.as_const();
	for (int i = 0; i < GetSize(bits); i++) {
		if (bits[i] == State::S1)
			value |= uint64_t(1) << i;
		else if (bits[i] != State::S0)
			return false;
	}

	return true;
}

int shift_const_width(int shift_amount)
{
	int width = 1;
	while ((1 << width) <= shift_amount && width < 31)
		width++;
	return width;
}

SigSpec normalize_width(SigSpec sig, int width)
{
	sig.extend_u0(width, false);
	if (GetSize(sig) > width)
		sig = sig.extract(0, width);
	return sig;
}
} // namespace

struct EDAOptStrengthPass : public Pass
{
	EDAOptStrengthPass() : Pass("my_opt_strength", "lower constant multiplications into shifts and adds") {}

	void help() override
	{
		log("\n");
		log("    my_opt_strength [selection]\n");
		log("\n");
		log("This pass performs a simple strength-reduction pass on $mul cells.\n");
		log("When one operand is a fully-defined unsigned constant, it rewrites the\n");
		log("multiplication into a sequence of $shl and $add cells.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		extra_args(args, 1, design);
		log_header(design, "Executing my_opt_STRENGTH pass (constant multiply lowering).\n");

		int total_rewritten = 0;

		for (auto module : design->selected_whole_modules_warn()) {
			if (module->has_processes_warn())
				continue;

			SigMap sigmap(module);
			std::vector<RTLIL::Cell*> cells_to_remove;

			for (auto cell : module->selected_cells()) {
				if (cell->type != ID($mul))
					continue;

				if (cell->getParam(ID::A_SIGNED).as_bool() || cell->getParam(ID::B_SIGNED).as_bool())
					continue;

				SigSpec sig_a = sigmap(cell->getPort(ID::A));
				SigSpec sig_b = sigmap(cell->getPort(ID::B));
				SigSpec sig_y = sigmap(cell->getPort(ID::Y));
				const int y_width = GetSize(sig_y);

				uint64_t const_value = 0;
				SigSpec variable_sig;
				if (const_to_u64(sig_b, const_value))
					variable_sig = sig_a;
				else if (const_to_u64(sig_a, const_value))
					variable_sig = sig_b;
				else
					continue;

				SigSpec base_sig = normalize_width(variable_sig, y_width);

				if (const_value == 0) {
					module->connect(sig_y, Const(0, y_width));
					cells_to_remove.push_back(cell);
					total_rewritten++;
					continue;
				}

				if (const_value == 1) {
					module->connect(sig_y, base_sig);
					cells_to_remove.push_back(cell);
					total_rewritten++;
					continue;
				}

				std::vector<SigSpec> terms;
				for (int bit = 0; bit < 63; bit++) {
					if (((const_value >> bit) & 1) == 0)
						continue;

					if (bit == 0) {
						terms.push_back(base_sig);
						continue;
					}

					RTLIL::Wire *shifted = module->addWire(NEW_ID, y_width);
					module->addShl(NEW_ID, base_sig, Const(bit, shift_const_width(bit)), shifted, false);
					terms.push_back(shifted);
				}

				if (terms.empty())
					continue;

				if (terms.size() == 1) {
					module->connect(sig_y, terms.front());
					cells_to_remove.push_back(cell);
					total_rewritten++;
					continue;
				}

				SigSpec accum = terms.front();
				for (size_t i = 1; i < terms.size(); i++) {
					SigSpec out = (i + 1 == terms.size()) ? sig_y : SigSpec(module->addWire(NEW_ID, y_width));
					module->addAdd(NEW_ID, accum, terms[i], out, false);
					accum = out;
				}

				cells_to_remove.push_back(cell);
				total_rewritten++;
			}

			for (auto cell : cells_to_remove)
				module->remove(cell);

			if (!cells_to_remove.empty()) {
				module->fixup_ports();
				module->check();
				design->scratchpad_set_bool("opt.did_something", true);
				log("  Module %s: lowered %d constant multipliers.\n", log_id(module), (int)cells_to_remove.size());
			}
		}

		log("my_opt_STRENGTH total rewritten cells: %d\n", total_rewritten);
		log_header(design, "my_opt_STRENGTH has been done!\n");
	}
} EDAOptStrengthPass;

PRIVATE_NAMESPACE_END
