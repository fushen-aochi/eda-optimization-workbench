#pragma once

#include <stdexcept>
#include <string>

class CompileError : public std::runtime_error
{
public:
  explicit CompileError(const std::string &msg);
};
