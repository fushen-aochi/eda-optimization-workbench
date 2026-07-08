#pragma once

#include <iosfwd>

#include "intermediate_representation.h"

void writeRtlil(std::ostream &os, const ModuleIR &ir, const NetlistResult &netlist);
