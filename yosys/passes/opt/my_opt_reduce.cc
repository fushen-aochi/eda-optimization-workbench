#include "kernel/register.h"
#include "kernel/sigtools.h"
#include "kernel/celltypes.h"
#include "kernel/log.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct EDAOptReducePass : public Pass
{
  EDAOptReducePass() : Pass("my_opt_reduce", "EDA reduce optimization") {}

  void help() override
  {
    log("\n");
    log("    my_opt_reduce [selection]\n");
    log("\n");
    log("This pass performs a small subset of reduce-cell optimizations.\n");
    log("It only folds $reduce_and, $reduce_or, $reduce_xor, $reduce_xnor,\n");
    log("and $reduce_bool when the input is fully constant.\n");
    log("\n");
  }

  void execute(std::vector<std::string> args, RTLIL::Design *design) override
  {
    extra_args(args, 1, design);
    log_header(design, "Executing my_opt_REDUCE pass (simple reduce cleanup).\n");

    int total_fold_count = 0;

    for (auto module : design->selected_whole_modules_warn())
    {
      if (module->has_processes_warn())
        continue;

      SigMap sigmap(module);
      std::vector<RTLIL::Cell *> cells_to_remove;

      auto is_all_bit = [](const SigSpec &sig, RTLIL::State bit)
      {
        if (!sig.is_fully_const())
          return false;
        for (auto b : sig.as_const())
          if (b != bit)
            return false;
        return true;
      };

      for (auto cell : module->selected_cells())
      {
        SigSpec y = sigmap(cell->getPort(ID::Y));
        SigSpec a = sigmap(cell->getPort(ID::A));
        int width = GetSize(a);

        if (!a.is_fully_const())
          continue;

        bool value = false;
        bool can_fold = true;

        if (cell->type == ID($reduce_and))
        {
          value = true;
          for (auto bit : a.as_const())
          {
            if (bit == State::S0)
            {
              value = false;
              break;
            }
            if (bit != State::S1)
            {
              can_fold = false;
              break;
            }
          }
        }
        else if (cell->type == ID($reduce_or))
        {
          value = false;
          for (auto bit : a.as_const())
          {
            if (bit == State::S1)
            {
              value = true;
              break;
            }
            if (bit != State::S0)
            {
              can_fold = false;
              break;
            }
          }
        }
        else if (cell->type == ID($reduce_xor) || cell->type == ID($reduce_xnor))
        {
          int parity = 0;
          for (auto bit : a.as_const())
          {
            if (bit == State::S1)
            {
              parity ^= 1;
            }
            else if (bit != State::S0)
            {
              can_fold = false;
              break;
            }
          }
          value = parity != 0;
          if (cell->type == ID($reduce_xnor))
            value = !value;
        }
        else if (cell->type == ID($reduce_bool))
        {
          value = false;
          for (auto bit : a.as_const())
          {
            if (bit == State::S1)
            {
              value = true;
              break;
            }
            if (bit != State::S0)
            {
              can_fold = false;
              break;
            }
          }
        }
        else
        {
          continue;
        }

        if (!can_fold)
          continue;

        module->connect(y, Const(value ? State::S1 : State::S0, GetSize(y)));
        cells_to_remove.push_back(cell);
      }

      for (auto cell : cells_to_remove)
        module->remove(cell);

      if (!cells_to_remove.empty())
      {
        module->fixup_ports();
        module->check();
        design->scratchpad_set_bool("opt.did_something", true);
      }

      total_fold_count += (int)cells_to_remove.size();
      log("  Module %s: folded %d cells.\n", log_id(module), (int)cells_to_remove.size());
    }

    log("my_opt_REDUCE total folded cells: %d\n", total_fold_count);
    log_header(design, "my_opt_REDUCE has been done!\n");
  }
} EDAOptReducePass;

PRIVATE_NAMESPACE_END