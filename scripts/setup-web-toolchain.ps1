[CmdletBinding()]
param(
    [string]$SdkRoot = (Join-Path $env:USERPROFILE '.dsh/toolchains/emsdk'),
    [string]$Version = '6.0.11'
)
$ErrorActionPreference = 'Stop'
$env:EMSDK_QUIET = '1'
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Use a concrete stable release, such as 6.0.11.' }
if (-not (Test-Path -LiteralPath $SdkRoot)) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $SdkRoot) | Out-Null
    & git clone https://github.com/emscripten-core/emsdk.git $SdkRoot
    if ($LASTEXITCODE -ne 0) { throw 'Official emsdk clone failed.' }
}
$Sdk = Join-Path $SdkRoot 'emsdk.bat'
if (-not (Test-Path -LiteralPath $Sdk)) { throw "Not an emsdk checkout: $SdkRoot" }
$Origin = & git -C $SdkRoot remote get-url origin
if ($LASTEXITCODE -ne 0 -or $Origin -notmatch 'github\.com[:/]emscripten-core/emsdk(?:\.git)?$') {
    throw 'The existing checkout must use the official emscripten-core/emsdk origin.'
}
& $Sdk install $Version
if ($LASTEXITCODE -ne 0) { throw "emsdk install $Version failed." }
& $Sdk activate $Version
if ($LASTEXITCODE -ne 0) { throw "emsdk activate $Version failed." }
. (Join-Path $SdkRoot 'emsdk_env.ps1') | Out-Null
& (Join-Path $SdkRoot 'upstream/emscripten/emcc.exe') --version
if ($LASTEXITCODE -ne 0) { throw 'emcc version verification failed.' }
Write-Host "SDK ready at $SdkRoot. Build with scripts/build-wasm.ps1."
