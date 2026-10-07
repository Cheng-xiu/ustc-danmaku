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
$RepoPrefix = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $OutDir.StartsWith($RepoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutDir must be inside the repository: $OutDir"
}
if ($Clean -and (Test-Path -LiteralPath $OutDir)) {
    $ResolvedOutput = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $OutDir).Path)
    if (-not $ResolvedOutput.StartsWith($RepoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove an output directory outside the repository.'
    }
    Remove-Item -LiteralPath $ResolvedOutput -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$CoreRoot = Join-Path $RepoRoot 'packages/core'
$CoreRelative = @(Get-Content -LiteralPath (Join-Path $CoreRoot 'sources.txt') -Encoding utf8)
if ($CoreRelative.Count -ne 16 -or @($CoreRelative | Select-Object -Unique).Count -ne 16) {
    throw 'Expected the shared 16-file C/AI source list.'
}
$CoreSources = @($CoreRelative | ForEach-Object {
    if ($_ -notmatch '^(core|ai)/[A-Za-z0-9_]+\.c$') { throw "Invalid core source: $_" }
    "packages/core/$_"
})
$AiSources = @() # AI 已包含在唯一清单中，不保留第二份构建清单。
$SimSources = @('apps/sim/main.c', 'apps/sim/log.c')
$GameSources = @('apps/desktop/game_main.cpp', 'apps/desktop/platform/input_win.cpp',
    'apps/desktop/render/scene.cpp', 'apps/desktop/render/hud.cpp')
$AttackUnitSources = @('core/attack.c', 'core/pattern_aim.c', 'core/demo_config.c',
    'core/rng.c', 'core/projectiles.c') | ForEach-Object { "packages/core/$_" }

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
        & $Gcc -std=c11 -O2 -Wall -Wextra -I "$CoreRoot\core" -I "$CoreRoot\ai" -c (Join-Path $RepoRoot $src) -o $obj
        if ($LASTEXITCODE -ne 0) { throw "compile failed: $src" }
    }

    if ($Target -in @('all', 'sim') -and (Get-MissingSources $SimSources).Count -eq 0) {
        $simObjs = @()
        foreach ($src in $SimSources) {
            $obj = Join-Path $OutDir (($src -replace '[\\/]', '_') -replace '\.c$', '.o')
            $simObjs += $obj
            Write-Host "[mingw] cc $src"
            & $Gcc -std=c11 -O2 -Wall -Wextra -I "$CoreRoot\core" -I "$CoreRoot\ai" -I "$RepoRoot\apps\sim" -c (Join-Path $RepoRoot $src) -o $obj
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
            $testObjects = $coreObjs
            if ($t.BaseName -eq 'test_attack') {
                $testObjects = @($AttackUnitSources | ForEach-Object {
                    Join-Path $OutDir (($_ -replace '[\\/]', '_') -replace '\.c$', '.o')
                })
            }
            & $Gcc -std=c11 -O2 -Wall -Wextra -I "$CoreRoot\core" -I "$CoreRoot\ai" -I "$RepoRoot\tests" `
                $t.FullName $testObjects -lm -o (Join-Path $OutDir ($t.BaseName + '.exe'))
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
    $inc = "/I`"$CoreRoot\core`" /I`"$CoreRoot\ai`""

    $lines = @()
    $lines += "call `"$vcvars`" >nul"
    $lines += "cd /d `"$RepoRoot`""
    Get-ChildItem -LiteralPath $OutDir -Filter '*.obj' -File | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Force
    }
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
            $lines += "cl /nologo /TC /std:c11 /O2 /W4 /utf-8 $inc /I`"$RepoRoot\apps\sim`" /Fo`"$OutDir\sim_$base.obj`" /c `"$($src.Replace('/','\'))`" || set FAILED=1"
        }
        $lines += "link /nologo /OUT:`"$OutDir\sim.exe`" `"$OutDir\core_*.obj`" `"$OutDir\sim_*.obj`" || set FAILED=1"
    }

    # 3) 测试：每个 test_*.c 自带 main()，必须各自链接为独立可执行文件。
    if ($Target -in @('all', 'tests')) {
        $tests = Get-ChildItem (Join-Path $RepoRoot 'tests') -Filter 'test_*.c' -ErrorAction SilentlyContinue
        foreach ($t in $tests) {
            $lines += "cl /nologo /TC /std:c11 /O2 /W4 /utf-8 $inc /I`"$RepoRoot\tests`" /Fo`"$OutDir\tobj_$($t.BaseName).obj`" /c `"tests\\$($t.Name)`" || set FAILED=1"
            $testCoreObjects = "`"$OutDir\core_*.obj`""
            if ($t.BaseName -eq 'test_attack') {
                $testCoreObjects = ($AttackUnitSources | ForEach-Object {
                    $base = [System.IO.Path]::GetFileNameWithoutExtension($_)
                    "`"$OutDir\core_$base.obj`""
                }) -join ' '
            }
            $lines += "link /nologo /OUT:`"$OutDir\$($t.BaseName).exe`" $testCoreObjects `"$OutDir\tobj_$($t.BaseName).obj`" || set FAILED=1"
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
            foreach ($src in $GameSources) {
                $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
                $lines += "cl /nologo /std:c++17 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /utf-8 " +
                    "/I`"$easyxInclude`" /I`"$CoreRoot\core`" /I`"$CoreRoot\ai`" /I`"$RepoRoot\apps\desktop\platform`" /I`"$RepoRoot\apps\desktop\render`" " +
                    "/Fo`"$OutDir\game_$base.obj`" /c `"$($src.Replace('/','\'))`" || set FAILED=1"
            }
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
    & cmd.exe /c "`"$bat`"" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'MSVC build failed' }
    return 0
}

# ---------------------------------------------------------------- CMake

function Build-CMake([string]$Generator) {
    $cmake = Get-CMakePath
    if (-not $cmake) { throw 'cmake.exe not found' }
    $cmakeArgs = @('-S', $RepoRoot, '-B', $OutDir)
    if ($Generator) { $cmakeArgs += @('-G', $Generator) }
    if ($Generator -like 'Visual Studio*') { $cmakeArgs += @('-A', 'x64') }
    if ($Generator -eq 'MinGW Makefiles') {
        $cmakeArgs += @("-DCMAKE_C_COMPILER=$Gcc", "-DCMAKE_CXX_COMPILER=$(Join-Path $MinGWBin 'g++.exe')",
            "-DCMAKE_MAKE_PROGRAM=$(Join-Path $MinGWBin 'mingw32-make.exe')", '-DDEMO_BUILD_GAME=OFF')
    }
    if ($EasyXRoot) { $cmakeArgs += "-DEASYX_ROOT=$EasyXRoot" }
    Write-Host "[cmake] $cmake $($cmakeArgs -join ' ')"
    & $cmake @cmakeArgs | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
    & $cmake --build $OutDir --config Release | Out-Host
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
