#include "kernel/register.h"
#include "kernel/sigtools.h"
#include "kernel/modtools.h"
#include "kernel/celltypes.h"
#include "kernel/log.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct EDAOptSharePass : public Pass
{
  EDAOptSharePass() : Pass("my_opt_share", "EDA resource sharing") {}

  void help() override
  {
    log("\n");
    log("    my_opt_share [selection]\n");
    log("\n");
    log("This pass performs a small subset of resource sharing.\n");
    log("It only folds a $mux that selects between two identical\n");
    log("binary ops into a single shared op with muxed inputs.\n");
    log("Supported ops: $add, $sub, $and, $or, $xor, $mul.\n");
    log("\n");
  }

  static bool ports_match(RTLIL::Cell *cell, RTLIL::IdString port, const SigMap &sigmap, const SigSpec &sig)
  {
    if (!cell->hasPort(port))
      return false;
    return sigmap(cell->getPort(port)) == sig;
  }

  static bool params_match(RTLIL::Cell *a, RTLIL::Cell *b)
  {
    if (a->parameters.size() != b->parameters.size())
      return false;
    for (auto &it : a->parameters)
    {
      if (b->parameters.count(it.first) == 0)
        return false;
      if (b->parameters.at(it.first) != it.second)
        return false;
    }
    return true;
  }

  static bool get_single_driver_cell(const SigSpec &sig, RTLIL::Cell *&driver,
                                     const SigMap &sigmap, ModIndex &mi, CellTypes &ct)
  {
    driver = nullptr;
    SigSpec flat = sigmap(sig);
    for (auto bit : flat)
    {
      RTLIL::Cell *bit_driver = nullptr;
      RTLIL::IdString bit_port;

      for (auto &pi : mi.query_ports(bit))
      {
        if (pi.cell == nullptr)
          continue;
        if (!ct.cell_known(pi.cell->type))
          continue;
        if (!ct.cell_output(pi.cell->type, pi.port))
          continue;
        if (bit_driver != nullptr && pi.cell != bit_driver)
          return false;
        bit_driver = pi.cell;
        bit_port = pi.port;
      }

      if (bit_driver == nullptr)
        return false;

      if (driver == nullptr)
        driver = bit_driver;
      else if (driver != bit_driver)
        return false;

      if (!ports_match(driver, bit_port, sigmap, flat))
        return false;
    }
    return driver != nullptr;
  }

  static bool output_exclusive_to_mux(RTLIL::Cell *src, RTLIL::Cell *mux,
                                      RTLIL::IdString mux_port, const SigMap &sigmap, ModIndex &mi)
  {
    SigSpec src_y = sigmap(src->getPort(ID::Y));
    for (auto bit : src_y)
    {
      for (auto &pi : mi.query_ports(bit))
      {
        if (pi.cell == nullptr)
          return false;
        if (pi.cell == src)
          continue;
        if (pi.cell == mux && pi.port == mux_port)
          continue;
        return false;
      }
    }
    return true;
  }

  void execute(std::vector<std::string> args, RTLIL::Design *design) override
  {
    extra_args(args, 1, design);
    log_header(design, "Executing my_opt_SHARE pass (simple resource sharing).\n");

    int total_shared = 0;

    for (auto module : design->selected_whole_modules_warn())
    {
      if (module->has_processes_warn())
        continue;

      SigMap sigmap(module);
      ModIndex mi(module);
      CellTypes ct;
      ct.setup_internals();

      std::vector<RTLIL::Cell *> cells_to_remove;
      std::vector<RTLIL::Cell *> muxes_to_remove;

      for (auto cell : module->selected_cells())
      {
        if (cell->type != ID($mux))
          continue;

        SigSpec a = sigmap(cell->getPort(ID::A));
        SigSpec b = sigmap(cell->getPort(ID::B));
        SigSpec s = sigmap(cell->getPort(ID::S));
        SigSpec y = sigmap(cell->getPort(ID::Y));

        if (GetSize(a) == 0 || GetSize(b) == 0)
          continue;
        if (GetSize(a) != GetSize(b) || GetSize(a) != GetSize(y))
          continue;

        RTLIL::Cell *ca = nullptr;
        RTLIL::Cell *cb = nullptr;
        if (!get_single_driver_cell(a, ca, sigmap, mi, ct))
          continue;
        if (!get_single_driver_cell(b, cb, sigmap, mi, ct))
          continue;
        if (ca == cb)
          continue;

        if (ca->type != cb->type)
          continue;

        if (!ca->type.in(ID($add), ID($sub), ID($and), ID($or), ID($xor), ID($mul)))
          continue;

        if (!params_match(ca, cb))
          continue;

        if (!ports_match(ca, ID::Y, sigmap, a) || !ports_match(cb, ID::Y, sigmap, b))
          continue;

        if (!output_exclusive_to_mux(ca, cell, ID::A, sigmap, mi))
          continue;
        if (!output_exclusive_to_mux(cb, cell, ID::B, sigmap, mi))
          continue;

        SigSpec a1 = sigmap(ca->getPort(ID::A));
        SigSpec b1 = sigmap(ca->getPort(ID::B));
        SigSpec a2 = sigmap(cb->getPort(ID::A));
        SigSpec b2 = sigmap(cb->getPort(ID::B));

        if (GetSize(a1) != GetSize(a2) || GetSize(b1) != GetSize(b2))
          continue;

        RTLIL::Wire *new_a = module->addWire(NEW_ID, GetSize(a1));
        RTLIL::Wire *new_b = module->addWire(NEW_ID, GetSize(b1));
        module->addMux(NEW_ID, a2, a1, s, new_a);
        module->addMux(NEW_ID, b2, b1, s, new_b);

        RTLIL::Cell *shared = module->addCell(NEW_ID, ca->type);
        shared->parameters = ca->parameters;
        shared->setPort(ID::A, new_a);
        shared->setPort(ID::B, new_b);
        shared->setPort(ID::Y, y);
        shared->check();

        cells_to_remove.push_back(ca);
        cells_to_remove.push_back(cb);
        muxes_to_remove.push_back(cell);
        total_shared++;
      }

      for (auto cell : muxes_to_remove)
        module->remove(cell);
      for (auto cell : cells_to_remove)
        module->remove(cell);

      if (!cells_to_remove.empty() || !muxes_to_remove.empty())
      {
        module->fixup_ports();
        module->check();
        design->scratchpad_set_bool("opt.did_something", true);
      }
    }

    log("my_opt_SHARE total merged muxes: %d\n", total_shared);
    log_header(design, "my_opt_SHARE has been done!\n");
  }
} EDAOptSharePass;

PRIVATE_NAMESPACE_END
