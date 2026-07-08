#pragma once

#include <string>

#include "intermediate_representation.h"

ModuleIR parseModule(const std::string &verilog, NodePool &pool);
