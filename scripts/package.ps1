# 打包脚本：把本次 demo 的源码、可运行程序、报告与验证证据汇总成一个自包含文件夹
# 用法: powershell -File scripts/package.ps1 [-OutDir <dir>]
param(
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$OutRoot = if ($OutDir) { $OutDir } else { Join-Path (Split-Path -Parent $RepoRoot) 'ustc-danmaku-demo-package' }
$OutRoot = [System.IO.Path]::GetFullPath($OutRoot)
if (Test-Path $OutRoot) { Remove-Item -Recurse -Force $OutRoot }
New-Item -ItemType Directory -Force -Path $OutRoot | Out-Null

function Copy-Tree([string]$Src, [string]$Dst, [string[]]$ExcludeNames) {
    if (-not (Test-Path $Src)) { return }
    New-Item -ItemType Directory -Force -Path $Dst | Out-Null
    Get-ChildItem $Src -Recurse -File | ForEach-Object {
        $skip = $false
        foreach ($n in $ExcludeNames) {
            if ($_.Name -like $n) { $skip = $true; break }
        }
        if ($skip) { return }
        $rel = $_.FullName.Substring((Resolve-Path $Src).Path.Length + 1)
        $target = Join-Path $Dst $rel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item $_.FullName $target -Force
    }
}

# 排除的中间产物：编译产物、代理构建目录、临时探针
$ExObj = @('*.obj', '*.o', '*.pdb', '*.ilk', '*.lib', '*.exp', '*.idb', '*.res')

Write-Host "[1/6] 源码"
foreach ($d in @('core', 'ai', 'platform', 'render', 'sim', 'tests', 'configs', 'assets', 'scripts', 'runs')) {
    Copy-Tree (Join-Path $RepoRoot $d) (Join-Path $OutRoot "src\$d") $ExObj
}
foreach ($f in @('game_main.cpp', 'CMakeLists.txt', '.gitignore', 'AGENTS.md')) {
    $p = Join-Path $RepoRoot $f
    if (Test-Path $p) { Copy-Item $p (Join-Path $OutRoot "src\$f") -Force }
}

Write-Host "[2/6] 文档（规则 / 接口 / 任务板 / 手册 / 验收 / 问题清单 / 试玩模板）"
Copy-Tree (Join-Path $RepoRoot 'docs') (Join-Path $OutRoot 'docs') $ExObj
# 参考规范与配图归入 references
$refDocs = @('project-spec-v3.1.md')
New-Item -ItemType Directory -Force -Path (Join-Path $OutRoot 'references\assets') | Out-Null
foreach ($f in $refDocs) {
    $p = Join-Path $RepoRoot "docs\$f"
    if (Test-Path $p) { Copy-Item $p (Join-Path $OutRoot "references\$f") -Force }
}
Copy-Tree (Join-Path $RepoRoot 'docs\assets') (Join-Path $OutRoot 'references\assets') $ExObj
Copy-Tree (Join-Path $RepoRoot 'references') (Join-Path $OutRoot 'references') $ExObj

Write-Host "[3/6] 可运行程序"
New-Item -ItemType Directory -Force -Path (Join-Path $OutRoot 'bin') | Out-Null
foreach ($f in @('build\game\ustc_danmaku.exe', 'build\sim\sim.exe')) {
    $p = Join-Path $RepoRoot $f
    if (Test-Path $p) { Copy-Item $p (Join-Path $OutRoot "bin\$(Split-Path -Leaf $f)") -Force }
}

Write-Host "[4/6] 子代理交付报告与验证证据"
$rptDst = Join-Path $OutRoot 'reports\agents'
foreach ($d in (Get-ChildItem (Join-Path $RepoRoot 'work\agents') -Directory -ErrorAction SilentlyContinue)) {
    $md = Get-ChildItem $d.FullName -Filter '*-report.md' -File -ErrorAction SilentlyContinue
    if ($md) {
        $dst = Join-Path $rptDst $d.Name
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        $md | ForEach-Object { Copy-Item $_.FullName (Join-Path $dst $_.Name) -Force }
    }
    # 日志与截图证据（排除 build 与超大中间产物）
    $logs = Join-Path $d.FullName 'logs'
    if (Test-Path $logs) {
        Copy-Tree $logs (Join-Path $rptDst "$($d.Name)\logs") ($ExObj + @('*.bmp.tmp'))
    }
    $snap = Join-Path $d.FullName 'snap'
    if (Test-Path $snap) { Copy-Tree $snap (Join-Path $rptDst "$($d.Name)\snap") $ExObj }
}

Write-Host "[5/6] 本次运行的实测日志"
New-Item -ItemType Directory -Force -Path (Join-Path $OutRoot 'reports\run-logs') | Out-Null
Get-ChildItem (Join-Path $RepoRoot 'build') -Recurse -File -Include '*.log', '*.txt' -ErrorAction SilentlyContinue |
    Where-Object { $_.Length -lt 5MB } |
    ForEach-Object {
        $rel = $_.FullName.Substring((Join-Path $RepoRoot 'build').Length + 1) -replace '[\\/]', '_'
        Copy-Item $_.FullName (Join-Path $OutRoot "reports\run-logs\$rel") -Force
    }

Write-Host "[6/6] 清单文件"
$commit = (& git -C $RepoRoot rev-parse HEAD) 2>$null
$branch = (& git -C $RepoRoot rev-parse --abbrev-ref HEAD) 2>$null
$dirty = (& git -C $RepoRoot status --porcelain) 2>$null
@(
    "ustc-danmaku 最小可玩 Boss demo 交付包",
    "生成时间: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
    "仓库分支: $branch",
    "提交: $commit",
    "未提交改动: $(if ($dirty) { ($dirty -join '; ') } else { '无' })",
    "",
    "目录说明:",
    "  src/        源码（core 纯 C 核心、ai 脚本学生、platform 输入、render 绘制、sim headless、tests）",
    "  bin/        可运行程序（ustc_danmaku.exe 图形 / sim.exe headless）",
    "  docs/       规则、接口、操作手册、独立验收、问题清单、试玩模板",
    "  references/ 原规范（Markdown + PDF）与设计配图",
    "  reports/    子代理交付报告、验证证据与本次运行的实测日志",
    "",
    "快速开始见 docs/demo-guide.md"
) | Set-Content (Join-Path $OutRoot 'PACKAGE-README.txt') -Encoding UTF8

# 统计
$all = Get-ChildItem $OutRoot -Recurse -File
Write-Host ""
Write-Host "打包完成: $OutRoot"
Write-Host ("文件数={0}  总大小={1:N0} 字节 ({2:N1} MB)" -f $all.Count,
    ($all | Measure-Object -Property Length -Sum).Sum,
    (($all | Measure-Object -Property Length -Sum).Sum / 1MB))
