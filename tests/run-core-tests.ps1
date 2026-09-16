param([string]$Compiler = $env:CXX)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Compiler) {
    $command = Get-Command g++.exe -ErrorAction SilentlyContinue
    if (-not $command) { throw 'Install MinGW-w64 and add g++.exe to PATH, or pass -Compiler.' }
    $Compiler = $command.Source
}
$output = Join-Path $root 'build\tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$test = Join-Path $output 'core_unit_test.exe'
& $Compiler -std=c++17 -static-libgcc -static-libstdc++ `
    (Join-Path $PSScriptRoot 'core_unit_test.cpp') `
    (Join-Path $root 'src\selection.cpp') (Join-Path $root 'src\util.cpp') `
    -o $test -luser32 -lole32 -luuid -lshell32
if ($LASTEXITCODE -ne 0) { throw "Test compilation failed: $LASTEXITCODE" }
& $test
if ($LASTEXITCODE -ne 0) { throw "Core tests failed: $LASTEXITCODE" }
