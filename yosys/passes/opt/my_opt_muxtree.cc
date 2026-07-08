#include "kernel/register.h"
#include "kernel/sigtools.h"
#include "kernel/celltypes.h"
#include "kernel/log.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct EDAOptMuxtreePass : public Pass {
    EDAOptMuxtreePass() : Pass("my_opt_muxtree", "EDA mux tree optimization") { }

    void help() override
    {
        log("\n");
        log("    my_opt_muxtree [selection]\n");
        log("\n");
        log("This pass performs a small subset of mux-tree optimization.\n");
        log("It only folds $mux and $pmux cells when the select signals are fully constant.\n");
        log("\n");
    }

    void execute(std::vector<std::string> args, RTLIL::Design *design) override
    {
        extra_args(args, 1, design);
        log_header(design, "Executing my_opt_MUXTREE pass (simple mux cleanup).\n");

        int total_fold_count = 0;

        for (auto module : design->selected_whole_modules_warn()) {
            if (module->has_processes_warn())
                continue;

            SigMap sigmap(module);
            std::vector<RTLIL::Cell*> cells_to_remove;

            auto is_all_bit = [](const SigSpec &sig, RTLIL::State bit) {
                if (!sig.is_fully_const())
                    return false;
                for (auto b : sig.as_const())
                    if (b != bit)
                        return false;
                return true;
            };

            for (auto cell : module->selected_cells()) {
                SigSpec y = sigmap(cell->getPort(ID::Y));

                if (cell->type == ID($mux)) {
                    SigSpec a = sigmap(cell->getPort(ID::A));
                    SigSpec b = sigmap(cell->getPort(ID::B));
                    SigSpec s = sigmap(cell->getPort(ID::S));

                    if (is_all_bit(s, State::S0)) {
                        module->connect(y, a);
                        cells_to_remove.push_back(cell);
                    } else if (is_all_bit(s, State::S1)) {
                        module->connect(y, b);
                        cells_to_remove.push_back(cell);
                    }

                    continue;
                }

                if (cell->type == ID($pmux)) {
                    SigSpec a = sigmap(cell->getPort(ID::A));
                    SigSpec b = sigmap(cell->getPort(ID::B));
                    SigSpec s = sigmap(cell->getPort(ID::S));
                    int width = GetSize(a);

                    if (is_all_bit(s, State::S0)) {
                        module->connect(y, a);
                        cells_to_remove.push_back(cell);
                        continue;
                    }

                    if (s.is_fully_const()) {
                        Const sel = s.as_const();
                        for (int i = 0; i < GetSize(sel); i++) {
                            if (sel[i] != State::S1)
                                continue;
                            module->connect(y, b.extract(i * width, width));
                            cells_to_remove.push_back(cell);
                            break;
                        }
                    }
                }
            }

            for (auto cell : cells_to_remove)
                module->remove(cell);

            if (!cells_to_remove.empty()) {
                module->fixup_ports();
                module->check();
                design->scratchpad_set_bool("opt.did_something", true);
            }

            total_fold_count += (int)cells_to_remove.size();
            log("  Module %s: folded %d cells.\n", log_id(module), (int)cells_to_remove.size());
        }

        log("my_opt_MUXTREE total folded cells: %d\n", total_fold_count);
        log_header(design, "my_opt_MUXTREE has been done!\n");
    }
} EDAOptMuxtreePass;

PRIVATE_NAMESPACE_END