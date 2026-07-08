#include "compile_error.h"

CompileError::CompileError(const std::string &msg) : std::runtime_error(msg) {}
