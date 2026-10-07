param([string]$OutputRoot = '')
$ErrorActionPreference = 'Stop'
function Get-TaskSha256([string]$Path) {
    $taskHasher = [Security.Cryptography.SHA256]::Create()
    $taskStream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($taskHasher.ComputeHash($taskStream)).Replace('-', '') }
    finally { $taskStream.Dispose(); $taskHasher.Dispose() }
}
$taskRoot = Split-Path -Parent $PSScriptRoot
if (!$OutputRoot) { $OutputRoot = Join-Path $taskRoot 'build/release' }
$taskOutput = [IO.Path]::GetFullPath($OutputRoot)
$taskPackage = Join-Path $taskOutput 'ustc-danmaku-endless-v6'
$taskZip = "$taskPackage.zip"
if ((Test-Path -LiteralPath $taskPackage) -or (Test-Path -LiteralPath $taskZip)) {
    throw 'Output already exists. Choose another OutputRoot to preserve the previous package.'
}
if (!(Test-Path -LiteralPath (Join-Path $taskRoot 'apps/web/dist/wasm/demo-core.wasm'))) {
    throw 'Build the Wasm core and web production output first.'
}
$taskCoreRoot = Join-Path $taskRoot 'packages/core'
$taskCoreSources = @(Get-Content -LiteralPath (Join-Path $taskCoreRoot 'sources.txt') -Encoding utf8 |
    ForEach-Object { $_.Trim() } | Where-Object { $_ -and !$_.StartsWith('#') })
$taskCoreSourceCount = $taskCoreSources.Count
if ($taskCoreSourceCount -ne 16 -or @($taskCoreSources | Select-Object -Unique).Count -ne $taskCoreSourceCount) {
    throw 'Expected the v6 shared 16-file C/AI source list.'
}
foreach ($taskSource in $taskCoreSources) {
    if (!(Test-Path -LiteralPath (Join-Path $taskCoreRoot $taskSource) -PathType Leaf)) {
        throw "Missing shared core source: $taskSource"
    }
}
New-Item -ItemType Directory -Path (Join-Path $taskPackage 'web'), (Join-Path $taskPackage 'scripts'), (Join-Path $taskPackage 'docs') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'apps/web/dist') -Destination (Join-Path $taskPackage 'web') -Recurse
Copy-Item -LiteralPath (Join-Path $taskRoot 'Start-Web-Demo.bat') -Destination $taskPackage
& node (Join-Path $taskRoot 'scripts/build-standalone.mjs') (Join-Path $taskPackage 'ustc-danmaku.html')
if ($LASTEXITCODE) { throw 'Standalone HTML build failed.' }
Copy-Item -LiteralPath (Join-Path $taskRoot 'scripts/serve-web.mjs') -Destination (Join-Path $taskPackage 'scripts')
foreach ($taskName in @('monorepo.md','monorepo-validation.md','web-demo-guide.md','web-demo-validation.md','web-demo-playtest.md','web-playability-findings.md','web-balance-findings.md','web-balance-findings-v5.md','demo-rules.md','web-abi.md','ustc-emblem-source.md')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot "docs/$taskName") -Destination (Join-Path $taskPackage 'docs')
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'docs/validation') -Destination (Join-Path $taskPackage 'docs') -Recurse
foreach ($taskHistoricalFolder in @('archive','assets')) {
    $taskHistoricalPath = Join-Path $taskRoot "docs/$taskHistoricalFolder"
    if (Test-Path -LiteralPath $taskHistoricalPath) {
        Copy-Item -LiteralPath $taskHistoricalPath -Destination (Join-Path $taskPackage 'docs') -Recurse
    }
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'docs/web-package-readme.md') -Destination (Join-Path $taskPackage 'README.md')

# Preserve the licenses of the production dependency tree distributed in the bundle.
$taskLicenseReader = @'
const data = require(process.argv[2]);
const packages = Object.entries(data.packages)
    .filter(([name, value]) => name.split('/').includes('node_modules') && !value.dev && !value.link)
    .map(([name, value]) => ({ name, version: value.version, license: value.license }));
process.stdout.write(JSON.stringify(packages));
'@
$taskList = $taskLicenseReader | & node - (Join-Path $taskRoot 'package-lock.json')
if ($LASTEXITCODE) { throw 'Cannot read production dependency manifest.' }
$taskPackages = $taskList | ConvertFrom-Json
$taskNotices = [Collections.Generic.List[string]]::new()
$taskNotices.Add('Third-party notices for the production dependency tree')
foreach ($taskEntry in $taskPackages) {
    $taskDependency = Join-Path $taskRoot $taskEntry.Name
    $taskLicense = Get-ChildItem -LiteralPath $taskDependency -File | Where-Object { $_.Name -match '^(LICENSE|LICENCE|COPYING)(\.|$)' } | Select-Object -First 1
    if (!$taskLicense -and $taskEntry.Name -eq 'node_modules/@pixi/colord') {
        $taskLicense = Get-Item -LiteralPath (Join-Path $taskRoot 'references/licenses/colord-LICENSE.md')
    }
    $taskNotices.Add("`n=== $($taskEntry.Name) $($taskEntry.version) ($($taskEntry.license)) ===`n")
    if ($taskLicense) { $taskNotices.Add((Get-Content -LiteralPath $taskLicense.FullName -Raw -Encoding utf8)) }
    else { throw "Missing production license text: $($taskEntry.Name)" }
}
$taskNotices -join "`n" | Set-Content -LiteralPath (Join-Path $taskPackage 'THIRD-PARTY-NOTICES.txt') -Encoding utf8

$taskManifest = @(Get-ChildItem -LiteralPath $taskPackage -Recurse -File | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($taskPackage.Length + 1).Replace('\','/'); bytes = $_.Length; sha256 = (Get-TaskSha256 $_.FullName) }
})
[ordered]@{ configVersion = 6; abiVersion = 5; coreSourceCount = $taskCoreSourceCount; files = $taskManifest } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskPackage 'manifest.json') -Encoding utf8
Compress-Archive -LiteralPath $taskPackage -DestinationPath $taskZip -CompressionLevel Optimal
Write-Host "Package: $taskPackage"
Write-Host "ZIP: $taskZip"
[pscustomobject]@{ Algorithm = 'SHA256'; Hash = (Get-TaskSha256 $taskZip); Path = $taskZip }
