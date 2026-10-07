# 导出 monorepo 源码与已有原生产物；已有目录不覆盖。
param([string]$OutDir = '')
$ErrorActionPreference = 'Stop'
function Get-TaskSha256([string]$Path) {
    $taskHasher = [Security.Cryptography.SHA256]::Create()
    $taskStream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($taskHasher.ComputeHash($taskStream)).Replace('-', '') }
    finally { $taskStream.Dispose(); $taskHasher.Dispose() }
}
$taskRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$taskOutput = if ($OutDir) { [IO.Path]::GetFullPath($OutDir) } else { Join-Path $taskRoot 'build/release-monorepo-source' }
if (Test-Path -LiteralPath $taskOutput) { throw 'Output already exists. Choose a new OutDir.' }
$taskFiles = @(& git -C $taskRoot ls-files --cached --others --exclude-standard)
if ($LASTEXITCODE) { throw 'Cannot enumerate repository source files.' }
New-Item -ItemType Directory -Force -Path (Join-Path $taskOutput 'src') | Out-Null
$taskCount = 0
$taskSourceManifest = [Collections.Generic.List[object]]::new()
foreach ($taskFile in ($taskFiles | Sort-Object -Unique)) {
    $taskSource = [IO.Path]::GetFullPath((Join-Path $taskRoot $taskFile))
    if (-not $taskSource.StartsWith($taskRoot.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Source escaped repository.' }
    if (-not (Test-Path -LiteralPath $taskSource -PathType Leaf)) { continue }
    $taskTarget = Join-Path (Join-Path $taskOutput 'src') $taskFile
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $taskTarget) | Out-Null
    Copy-Item -LiteralPath $taskSource -Destination $taskTarget
    $taskSourceManifest.Add([ordered]@{ path = $taskFile; bytes = (Get-Item -LiteralPath $taskTarget).Length; sha256 = (Get-TaskSha256 $taskTarget) })
    $taskCount++
}
New-Item -ItemType Directory -Force -Path (Join-Path $taskOutput 'bin') | Out-Null
$taskBinaryManifest = [Collections.Generic.List[object]]::new()
foreach ($taskBinary in @('build/native/sim.exe','build/msvc/ustc_danmaku.exe','build/native/ustc_danmaku.exe')) {
    $taskSource = Join-Path $taskRoot $taskBinary
    $taskTarget = Join-Path (Join-Path $taskOutput 'bin') (Split-Path -Leaf $taskBinary)
    if ((Test-Path -LiteralPath $taskSource) -and -not (Test-Path -LiteralPath $taskTarget)) {
        Copy-Item -LiteralPath $taskSource -Destination $taskTarget
        $taskBinaryManifest.Add([ordered]@{ path = 'bin/' + (Split-Path -Leaf $taskBinary); sourcePath = $taskBinary;
            bytes = (Get-Item -LiteralPath $taskTarget).Length; sha256 = (Get-TaskSha256 $taskTarget);
            sourceCommit = $null; provenance = 'existing-unverified' })
    }
}
$taskCommit = & git -C $taskRoot rev-parse HEAD
$taskDirty = @(& git -C $taskRoot status --porcelain)
[ordered]@{ commit = $taskCommit; dirty = ($taskDirty.Count -gt 0); sourceFiles = $taskCount;
    layout = 'src/apps, src/packages, src/docs; root npm workspace and CMake entries';
    gameConfig = 6; webAbi = 5; sources = $taskSourceManifest.ToArray(); binaries = $taskBinaryManifest.ToArray() } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskOutput 'manifest.json') -Encoding utf8
@(
 '科大弹幕录 monorepo 源码包',
 'src/ 是完整源码；在 src/ 根执行 npm ci、npm run build、npm test。',
 'apps/web 为当前网页，apps/sim 为无界面仿真，apps/desktop 为历史 Windows 入口。',
 'packages/core 是唯一 C/AI 实现，packages/wasm 为桥接。',
 '直接试玩 src/demo/ustc-danmaku.html；操作/构建见 src/docs/monorepo.md。',
 'bin/ 可包含已有原生产物；打包过程不会重新构建或证明其对应当前源码，manifest 标为 existing-unverified。'
) | Set-Content -LiteralPath (Join-Path $taskOutput 'README.txt') -Encoding utf8
Write-Host "Source package: $taskOutput ($taskCount files)"
