# build.ps1 - 最小可玩 Boss demo 构建入口（母代理维护）
#
# 用法:
#   powershell -File scripts/build.ps1 -Target all
#   powershell -File scripts/build.ps1 -Target tests -Toolchain msvc
#   powershell -File scripts/build.ps1 -Target game  -Toolchain msvc -EasyXRoot <dir>
#   powershell -File scripts/build.ps1 -Target all   -Toolchain cmake-msvc
#
# 产物目录默认 build/<toolchain>/，可用 -OutDir 覆盖（须位于仓库内）。该目录不提交 Git。
# 说明: 本文件必须保存为 UTF-8 with BOM，否则 Windows PowerShell 5.1 会按 CP936 误读中文。

[CmdletBinding()]
param(
    [ValidateSet('all', 'core', 'sim', 'tests', 'game')]
    [string]$Target = 'all',
    [ValidateSet('mingw', 'msvc', 'cmake-mingw', 'cmake-msvc', 'auto')]
    [string]$Toolchain = 'auto',
    [string]$OutDir = '',
    [string]$EasyXRoot = '',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

# EasyX 位置：母代理用 7z 从官方 SFX 解包（见 runs/easyx-install.md）。
# 可用 -EasyXRoot 覆盖，或设置环境变量 EASYX_ROOT。
$DefaultEasyX = 'C:\Users\jhsly\.dsh\toolchains\easyx-26.9.25'
if (-not $EasyXRoot) {
    if ($env:EASYX_ROOT) { $EasyXRoot = $env:EASYX_ROOT } else { $EasyXRoot = $DefaultEasyX }
}

$MinGWBin = 'D:\mingw64\mingw64\bin'
$Gcc = Join-Path $MinGWBin 'gcc.exe'

function Get-MsvcEnv {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { return $null }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { return $null }
    return $vcvars
}

function Get-CMakePath {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $found = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if ($found) { return $found }
    return $null
}

if ($Toolchain -eq 'auto') {
    if (Get-MsvcEnv) { $Toolchain = 'msvc' }
    elseif (Test-Path $Gcc) { $Toolchain = 'mingw' }
    else { throw 'no C/C++ toolchain found' }
}

if (-not $OutDir) { $OutDir = Join-Path $RepoRoot "build\$Toolchain" }
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
if (-not $OutDir.StartsWith([System.IO.Path]::GetFullPath($RepoRoot))) {
    throw "OutDir must be inside the repository: $OutDir"
}
if ($Clean -and (Test-Path $OutDir)) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$CoreSources = @(
    'core/demo_config.c', 'core/field_config.c', 'core/rng.c', 'core/collision.c', 'core/actors.c',
    'core/projectiles.c', 'core/attack.c', 'core/patterns.c',
    'core/pattern_ring.c', 'core/pattern_course.c', 'core/pattern_mine.c',
    'core/pattern_shower.c', 'core/student_fire.c', 'core/world.c'
)
$AiSources = @('ai/student_bot.c')
$SimSources = @('sim/main.c', 'sim/log.c')
$GameSources = @('game_main.cpp', 'platform/input_win.cpp', 'render/scene.cpp', 'render/hud.cpp')

function Get-MissingSources([string[]]$Files) {
    $missing = @()
    foreach ($f in $Files) {
        if (-not (Test-Path (Join-Path $RepoRoot $f))) { $missing += $f }
    }
    return $missing
}

# ---------------------------------------------------------------- MinGW

function Build-MinGW {
    if (-not (Test-Path $Gcc)) { throw "gcc not found: $Gcc" }
    $present = @(($CoreSources + $AiSources) | Where-Object { Test-Path (Join-Path $RepoRoot $_) } | Select-Object -Unique)
    $missing = Get-MissingSources $CoreSources
    if ($missing.Count -gt 0) {
        Write-Host "[mingw] missing core sources (other tasks still running): $($missing -join ', ')" -ForegroundColor Yellow
    }
    $coreObjs = @()
    foreach ($src in $present) {
        $obj = Join-Path $OutDir (($src -replace '[\\/]', '_') -replace '\.c$', '.o')
        $coreObjs += $obj
        Write-Host "[mingw] cc $src"
        & $Gcc -std=c11 -O2 -Wall -Wextra -I "$RepoRoot\core" -I "$RepoRoot\ai" -c (Join-Path $RepoRoot $src) -o $obj
        if ($LASTEXITCODE -ne 0) { throw "compile failed: $src" }
    }

    if ($Target -in @('all', 'sim') -and (Get-MissingSources $SimSources).Count -eq 0) {
        $simObjs = @()
        foreach ($src in $SimSources) {
            $obj = Join-Path $OutDir (($src -replace '[\\/]', '_') -replace '\.c$', '.o')
            $simObjs += $obj
            Write-Host "[mingw] cc $src"
            & $Gcc -std=c11 -O2 -Wall -Wextra -I "$RepoRoot\core" -I "$RepoRoot\ai" -I "$RepoRoot\sim" -c (Join-Path $RepoRoot $src) -o $obj
            if ($LASTEXITCODE -ne 0) { throw "compile failed: $src" }
        }
        Write-Host "[mingw] link sim.exe"
        & $Gcc -o (Join-Path $OutDir 'sim.exe') ($coreObjs + $simObjs)
        if ($LASTEXITCODE -ne 0) { throw 'link sim.exe failed' }
    }

    if ($Target -in @('all', 'tests')) {
        $tests = Get-ChildItem (Join-Path $RepoRoot 'tests') -Filter 'test_*.c' -ErrorAction SilentlyContinue
        foreach ($t in $tests) {
            Write-Host "[mingw] test $($t.Name)"
            & $Gcc -std=c11 -O2 -Wall -Wextra -I "$RepoRoot\core" -I "$RepoRoot\ai" -I "$RepoRoot\tests" `
                $t.FullName $coreObjs -o (Join-Path $OutDir ($t.BaseName + '.exe'))
            if ($LASTEXITCODE -ne 0) { throw "test compile failed: $($t.Name)" }
        }
    }

    if ($Target -in @('all', 'game')) {
        Write-Host "[mingw] EasyX needs MSVC; use -Toolchain msvc -Target game" -ForegroundColor Yellow
    }
    return 0
}

# ---------------------------------------------------------------- MSVC + EasyX

function Build-Msvc {
    $vcvars = Get-MsvcEnv
    if (-not $vcvars) { throw 'MSVC vcvars64.bat not found' }
    $easyxInclude = Join-Path $EasyXRoot 'include'
    $easyxLibDir = Join-Path $EasyXRoot 'lib_vc2015\x64'
    $haveEasyX = (Test-Path (Join-Path $easyxInclude 'easyx.h')) -and (Test-Path (Join-Path $easyxLibDir 'EasyXw.lib'))

    $missing = Get-MissingSources $CoreSources
    if ($missing.Count -gt 0) {
        Write-Host "[msvc] missing core sources: $($missing -join ', ')" -ForegroundColor Yellow
    }
    $allSources = @(($CoreSources + $AiSources) | Where-Object { Test-Path (Join-Path $RepoRoot $_) })
    $inc = "/I`"$RepoRoot\core`" /I`"$RepoRoot\ai`""

    $lines = @()
    $lines += "call `"$vcvars`" >nul"
    $lines += "cd /d `"$RepoRoot`""
    $lines += "if exist `"$OutDir\*.obj`" del /q `"$OutDir\*.obj`""
    $lines += "set FAILED=0"

    # 1) 核心按 C 编译。逐文件调用 cl：/Fo 只接受单一输出路径，多源文件会报 D8036。
    foreach ($src in $allSources) {
        $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
        $lines += "cl /nologo /TC /std:c11 /O2 /W4 /utf-8 $inc /Fo`"$OutDir\core_$base.obj`" /c `"$($src.Replace('/','\'))`" || set FAILED=1"
    }

    # 2) sim
    if ($Target -in @('all', 'sim') -and (Get-MissingSources $SimSources).Count -eq 0) {
        foreach ($src in $SimSources) {
            $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
            $lines += "cl /nologo /TC /std:c11 /O2 /W4 /utf-8 $inc /I`"$RepoRoot\sim`" /Fo`"$OutDir\sim_$base.obj`" /c `"$($src.Replace('/','\'))`" || set FAILED=1"
        }
        $lines += "link /nologo /OUT:`"$OutDir\sim.exe`" `"$OutDir\core_*.obj`" `"$OutDir\sim_*.obj`" || set FAILED=1"
    }

    # 3) 测试：每个 test_*.c 自带 main()，必须各自链接为独立可执行文件。
    if ($Target -in @('all', 'tests')) {
        $tests = Get-ChildItem (Join-Path $RepoRoot 'tests') -Filter 'test_*.c' -ErrorAction SilentlyContinue
        foreach ($t in $tests) {
            $lines += "cl /nologo /TC /std:c11 /O2 /W4 /utf-8 $inc /I`"$RepoRoot\tests`" /Fo`"$OutDir\tobj_$($t.BaseName).obj`" /c `"tests\\$($t.Name)`" || set FAILED=1"
            $lines += "link /nologo /OUT:`"$OutDir\$($t.BaseName).exe`" `"$OutDir\core_*.obj`" `"$OutDir\tobj_$($t.BaseName).obj`" || set FAILED=1"
        }
    }

    # 4) 图形游戏（C++ + EasyX）
    if ($Target -in @('all', 'game')) {
        $missingGame = Get-MissingSources $GameSources
        if (-not $haveEasyX) {
            Write-Host "[msvc] EasyX not found ($easyxInclude / $easyxLibDir); skipping game" -ForegroundColor Yellow
        } elseif ($missingGame.Count -gt 0) {
            Write-Host "[msvc] missing game sources: $($missingGame -join ', ')" -ForegroundColor Yellow
        } else {
            $lines += "cl /nologo /std:c++17 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /utf-8 " +
                "/I`"$easyxInclude`" /I`"$RepoRoot\core`" /I`"$RepoRoot\ai`" /I`"$RepoRoot\platform`" /I`"$RepoRoot\render`" /c " +
                (($GameSources | ForEach-Object { "`"$($_.Replace('/','\'))`"" }) -join ' ') + " /Fo`"$OutDir\game_`" || set FAILED=1"
            # /SUBSYSTEM:CONSOLE 保留控制台便于诊断；EasyX 仍创建图形窗口
            $lines += "link /nologo /SUBSYSTEM:CONSOLE /OUT:`"$OutDir\ustc_danmaku.exe`" `"$OutDir\core_*.obj`" `"$OutDir\game_*.obj`" " +
                "/LIBPATH:`"$easyxLibDir`" user32.lib gdi32.lib winmm.lib shell32.lib || set FAILED=1"
        }
    }

    $lines += "if %FAILED%==1 (echo BUILD_FAILED & exit /b 1)"
    $lines += "echo BUILD_OK"
    $bat = Join-Path $OutDir '_build.bat'
    $lines -join "`r`n" | Set-Content -Path $bat -Encoding ASCII
    Write-Host "[msvc] run $bat"
    & cmd.exe /c "`"$bat`""
    if ($LASTEXITCODE -ne 0) { throw 'MSVC build failed' }
    return 0
}

# ---------------------------------------------------------------- CMake

function Build-CMake([string]$Generator) {
    $cmake = Get-CMakePath
    if (-not $cmake) { throw 'cmake.exe not found' }
    $cmakeArgs = @('-S', $RepoRoot, '-B', $OutDir)
    if ($Generator) { $cmakeArgs += @('-G', $Generator, '-A', 'x64') }
    if ($EasyXRoot) { $cmakeArgs += "-DEASYX_ROOT=$EasyXRoot" }
    Write-Host "[cmake] $cmake $($cmakeArgs -join ' ')"
    & $cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
    & $cmake --build $OutDir --config Release
    if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }
    return 0
}

# ---------------------------------------------------------------- 入口

switch ($Toolchain) {
    'mingw' { $rc = Build-MinGW }
    'msvc' { $rc = Build-Msvc }
    'cmake-mingw' { $rc = Build-CMake 'MinGW Makefiles' }
    'cmake-msvc' { $rc = Build-CMake 'Visual Studio 17 2022' }
    default { throw "unknown toolchain $Toolchain" }
}
Write-Host "build finished: $OutDir (exit=$rc)"
exit $rc
