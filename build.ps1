param(
  [string]$OutputPath = "bin/verilog_rtlil_compiler.exe"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$compilerDir = Join-Path $root "compiler"
$fullOutputPath = Join-Path $root $OutputPath
$outputDir = Split-Path -Parent $fullOutputPath

if (-not (Test-Path -LiteralPath $outputDir)) {
  New-Item -ItemType Directory -Path $outputDir | Out-Null
}

$sources = @(
  "compile_error.cpp"
  "frontend_analyzer.cpp"
  "intermediate_representation.cpp"
  "netlist_generator.cpp"
  "rtlil_writer.cpp"
  "simple_verilog_compiler.cpp"
) | ForEach-Object { Join-Path $compilerDir $_ }

$args = @(
  "-std=c++17"
  "-O2"
  "-Wall"
  "-Wextra"
  "-I", $compilerDir
  "-o", $fullOutputPath
) + $sources

Write-Host "Building compiler -> $fullOutputPath"
& g++ @args

if ($LASTEXITCODE -ne 0) {
  throw "g++ build failed."
}

Write-Host "Build completed."
