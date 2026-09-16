param(
    [string]$Compiler = $env:CXX,
    [string]$OutputRoot = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $Compiler) {
    $command = Get-Command g++.exe -ErrorAction SilentlyContinue
    if (-not $command) { throw 'Install MinGW-w64 and add g++.exe to PATH, or pass -Compiler.' }
    $Compiler = $command.Source
}
if (-not (Get-Command $Compiler -ErrorAction SilentlyContinue)) { throw 'C++ compiler was not found.' }
$build = Join-Path $OutputRoot 'build'
$release = Join-Path $OutputRoot 'release'
New-Item -ItemType Directory -Force -Path $build, $release | Out-Null
& $Compiler -std=c++17 -Os -s -static-libgcc -static-libstdc++ -municode -mwindows `
    (Join-Path $root 'src\main.cpp') (Join-Path $root 'src\selection.cpp') `
    (Join-Path $root 'src\translation.cpp') (Join-Path $root 'src\util.cpp') `
    -o (Join-Path $build 'quick-translate.exe') `
    -lwinhttp -lshell32 -lole32 -luuid -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $LASTEXITCODE" }
Copy-Item -LiteralPath (Join-Path $build 'quick-translate.exe') -Destination $release -Force
foreach ($destination in @($build, $release)) {
    Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination (Join-Path $destination '使用说明.md') -Force
    foreach ($name in @('enable-startup.ps1', 'disable-startup.ps1', '一键启用开机启动.bat', '一键取消开机启动.bat')) {
        Copy-Item -LiteralPath (Join-Path $root "packaging\$name") -Destination $destination -Force
    }
}
Write-Output (Join-Path $build 'quick-translate.exe')
