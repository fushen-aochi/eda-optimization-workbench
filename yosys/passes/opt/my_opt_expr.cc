#include "kernel/yosys.h"
#include "kernel/sigtools.h"

USING_YOSYS_NAMESPACE

struct EDAOptExprPass : public Pass {
    EDAOptExprPass() : Pass("my_opt_expr", "EDA constant propagation") { }

    void execute(std::vector<std::string> args, RTLIL::Design *design) override
    {
        extra_args(args, 1, design);
        log_header(design, "Running my_opt_EXPR\n");

        int total_fold_count = 0;

        for (auto module : design->selected_modules()) {
            SigMap sigmap(module);
            std::vector<RTLIL::Cell*> cells_to_remove;
            //锟斤拷锟竭猴拷锟斤拷锟斤拷锟叫讹拷 SigSpec 锟角凤拷全为某锟斤拷锟斤拷锟斤拷值
            auto is_all_bit = [](const SigSpec &sig, RTLIL::State bit) {
                if (!sig.is_fully_const())
                    return false;
                for (auto b : sig.as_const())
                    if (b != bit)
                        return false;
                return true;
            };
            //锟斤拷锟竭猴拷锟斤拷锟斤拷取锟斤拷一锟斤拷全为锟斤拷锟斤拷值锟斤拷 SigSpec
            auto invert_const = [](const SigSpec &sig) {
                Const in = sig.as_const();
                for (auto &b : in.bits) {
                    if (b == State::S0)
                        b = State::S1;
                    else if (b == State::S1)
                        b = State::S0;
                }
                return SigSpec(in);
            };

            // 锟斤拷锟斤拷锟斤拷锟角帮拷锟窖★拷锟?cell锟斤拷锟斤拷证 pass 锟斤拷为锟斤拷选锟斤拷锟斤拷锟斤拷锟捷★拷
            for (auto cell : module->selected_cells()) {
                SigSpec y = sigmap(cell->getPort(ID::Y));
                
                if (cell->type == ID($not)) {
                    SigSpec a = sigmap(cell->getPort(ID::A));
                    if (a.is_fully_const()) {
                        module->connect(y, invert_const(a));
                        cells_to_remove.push_back(cell);
                    }
                    continue;
                }

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

                if (cell->type == ID($and) || cell->type == ID($or) || cell->type == ID($xor)) {
                    SigSpec a = sigmap(cell->getPort(ID::A));
                    SigSpec b = sigmap(cell->getPort(ID::B));
                    int width = GetSize(y);

                    if (cell->type == ID($and)) {
                        if (is_all_bit(a, State::S0) || is_all_bit(b, State::S0)) {
                            module->connect(y, SigSpec(State::S0, width));
                            cells_to_remove.push_back(cell);
                        } else if (is_all_bit(a, State::S1)) {
                            module->connect(y, b);
                            cells_to_remove.push_back(cell);
                        } else if (is_all_bit(b, State::S1)) {
                            module->connect(y, a);
                            cells_to_remove.push_back(cell);
                        }
                    } else if (cell->type == ID($or)) {
                        if (is_all_bit(a, State::S1) || is_all_bit(b, State::S1)) {
                            module->connect(y, SigSpec(State::S1, width));
                            cells_to_remove.push_back(cell);
                        } else if (is_all_bit(a, State::S0)) {
                            module->connect(y, b);
                            cells_to_remove.push_back(cell);
                        } else if (is_all_bit(b, State::S0)) {
                            module->connect(y, a);
                            cells_to_remove.push_back(cell);
                        }
                    } else {
                        // $xor
                        if (is_all_bit(a, State::S0)) {
                            module->connect(y, b);
                            cells_to_remove.push_back(cell);
                        } else if (is_all_bit(b, State::S0)) {
                            module->connect(y, a);
                            cells_to_remove.push_back(cell);
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

        log("my_opt_EXPR total folded cells: %d\n", total_fold_count);
        log_header(design, "my_opt_EXPR has been done!\n");
    }
} EDAOptExprPass;
