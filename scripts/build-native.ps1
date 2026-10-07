param([string]$CMake = '', [string]$Gcc = '', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (!$CMake) {
    $taskCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($taskCommand) { $CMake = $taskCommand.Source }
    else { $CMake = Get-ChildItem "$env:LOCALAPPDATA/Microsoft/WinGet/Packages" -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName }
}
if (!$CMake) { throw 'CMake 3.16+ is required.' }
if (!$Gcc) {
    $taskCommand = Get-Command gcc -ErrorAction SilentlyContinue
    if ($taskCommand) { $Gcc = $taskCommand.Source }
    elseif (Test-Path 'D:/mingw64/mingw64/bin/gcc.exe') { $Gcc = 'D:/mingw64/mingw64/bin/gcc.exe' }
}
$taskBuild = Join-Path $taskRoot 'build/native'
$taskOptions = @('-S', $taskRoot, '-B', $taskBuild, '-DDEMO_BUILD_GAME=OFF')
if ($Gcc) {
    $taskCompilerDir = Split-Path $Gcc
    $taskOptions += @('-G', 'MinGW Makefiles', "-DCMAKE_C_COMPILER=$Gcc", "-DCMAKE_CXX_COMPILER=$(Join-Path $taskCompilerDir 'g++.exe')", "-DCMAKE_MAKE_PROGRAM=$(Join-Path $taskCompilerDir 'mingw32-make.exe')")
}
& $CMake @taskOptions
if ($LASTEXITCODE) { throw 'Native configure failed.' }
& $CMake --build $taskBuild --config Release --parallel 4
if ($LASTEXITCODE) { throw 'Native build failed.' }
if (!$SkipTests) {
    & (Join-Path (Split-Path $CMake) 'ctest.exe') --test-dir $taskBuild -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Native tests failed.' }
}
