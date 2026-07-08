#include <filesystem>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "compile_error.h"
#include "frontend_analyzer.h"
#include "netlist_generator.h"
#include "rtlil_writer.h"

namespace
{
std::string readFileText(const std::filesystem::path &path)
{
#ifdef _WIN32
  FILE *file = _wfopen(path.wstring().c_str(), L"rb");
  if (file == nullptr)
  {
    return {};
  }
  std::string text;
  char buffer[4096];
  size_t readCount = 0;
  while ((readCount = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
  {
    text.append(buffer, readCount);
  }
  std::fclose(file);
  return text;
#else
  std::ifstream fin(path, std::ios::binary);
  if (!fin)
  {
    return {};
  }
  std::ostringstream buffer;
  buffer << fin.rdbuf();
  return buffer.str();
#endif
}

bool writeFileText(const std::filesystem::path &path, const std::string &text)
{
#ifdef _WIN32
  FILE *file = _wfopen(path.wstring().c_str(), L"wb");
  if (file == nullptr)
  {
    return false;
  }
  const bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
  std::fclose(file);
  return ok;
#else
  std::ofstream fout(path, std::ios::binary);
  if (!fout)
  {
    return false;
  }
  fout.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(fout);
#endif
}
} // namespace

int main(int argc, char **argv)
{
  namespace fs = std::filesystem;

  if (argc != 2 && argc != 4)
  {
    std::cerr << "Usage: simple_verilog_compiler <input.v> [-o output.il]\n";
    return 1;
  }

  fs::path inputPath = fs::u8path(argv[1]);
  fs::path outputPath;

  if (argc == 2)
  {
    outputPath = fs::path(inputPath.string() + ".il");
  }
  else
  {
    if (std::string(argv[2]) != "-o")
    {
      std::cerr << "Error: only -o is supported\n";
      return 1;
    }
    if (std::string(argv[3]).empty())
    {
      std::cerr << "Error: missing output file after -o\n";
      return 1;
    }
    outputPath = fs::u8path(argv[3]);
  }

  std::error_code ec;
  inputPath = fs::absolute(inputPath, ec);
  if (ec)
  {
    std::cerr << "Error: invalid input path: " << argv[1] << "\n";
    return 1;
  }

  outputPath = fs::absolute(outputPath, ec);
  if (ec)
  {
    std::cerr << "Error: invalid output path\n";
    return 1;
  }

  std::string verilog = readFileText(inputPath);
  if (verilog.empty())
  {
    std::cerr << "Error: cannot read input file: " << inputPath << "\n";
    return 1;
  }

  try
  {
    NodePool pool;
    ModuleIR ir = parseModule(verilog, pool);
    NetlistResult netlist = generateNetlist(ir);

    fs::path parent = outputPath.parent_path();
    if (!parent.empty())
    {
      fs::create_directories(parent, ec);
      if (ec)
      {
        std::cerr << "Error: cannot create output directory: " << parent << "\n";
        return 1;
      }
    }

    std::ostringstream output;
    writeRtlil(output, ir, netlist);
    if (!writeFileText(outputPath, output.str()))
    {
      std::cerr << "Error: cannot write output file: " << outputPath << "\n";
      return 1;
    }
    std::cout << "Compile succeeded: " << inputPath << " -> " << outputPath << "\n";
    return 0;
  }
  catch (const CompileError &e)
  {
    std::cerr << "Compile failed: " << e.what() << "\n";
    return 2;
  }
  catch (const std::exception &e)
  {
    std::cerr << "Unknown error: " << e.what() << "\n";
    return 3;
  }
}
