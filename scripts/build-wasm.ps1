[CmdletBinding()]
param(
    [string]$SdkRoot = (Join-Path $env:USERPROFILE '.dsh/toolchains/emsdk'),
    [string]$OutDir = ''
)
$ErrorActionPreference = 'Stop'
$env:EMSDK_QUIET = '1'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
$Emcc = Join-Path $SdkRoot 'upstream/emscripten/emcc.exe'
if (-not (Test-Path -LiteralPath $Emcc)) { throw 'Run scripts/setup-web-toolchain.ps1 first.' }
. (Join-Path $SdkRoot 'emsdk_env.ps1') | Out-Null
if (-not $OutDir) { $OutDir = Join-Path $RepoRoot 'web/public/wasm' }
$OutDir = [IO.Path]::GetFullPath($OutDir)
$RepoPrefix = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $OutDir.StartsWith($RepoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutDir must be a child directory of this repository.'
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$RootCmake = Get-Content -LiteralPath (Join-Path $RepoRoot 'CMakeLists.txt') -Raw
$Block = [regex]::Match($RootCmake, 'set\(DEMO_CORE_SOURCES[^)]*\)').Value
$Core = @([regex]::Matches($Block, '(core|ai)/[A-Za-z0-9_]+\.c') | ForEach-Object { Join-Path $RepoRoot $_.Value })
if ($Core.Count -ne 15) { throw 'Expected the frozen 15-file core/ai source list.' }
$Bridge = Join-Path $RepoRoot 'wasm/demo_bridge.c'
$Output = Join-Path $OutDir 'demo-core.mjs'
$Arguments = @('-std=c11', '-O2', '-Wall', '-Wextra', '--no-entry', '-I', (Join-Path $RepoRoot 'core'),
    '-I', (Join-Path $RepoRoot 'ai')) + $Core + @($Bridge, '-lm', '-o', $Output,
    '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sENVIRONMENT=web,node', '-sFILESYSTEM=0',
    '-sALLOW_MEMORY_GROWTH=1', '-sSTACK_SIZE=1048576', '-sINITIAL_MEMORY=16777216',
    '-sMAXIMUM_MEMORY=67108864',
    "-sEXPORTED_FUNCTIONS=['_demo_reset','_demo_reset_sized','_demo_step','_demo_snapshot','_demo_snapshot_size','_demo_dispose']",
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']")
& $Emcc @Arguments
if ($LASTEXITCODE -ne 0) { throw 'Wasm core compilation failed.' }
if (-not (Test-Path -LiteralPath $Output) -or -not (Test-Path -LiteralPath (Join-Path $OutDir 'demo-core.wasm'))) {
    throw 'The expected mjs/wasm pair was not produced.'
}
& $Emcc --version
Write-Host "Built $Output (same core/ai sources as native)."
