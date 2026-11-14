# 分析 T158Pkg.dsc: 沿模块依赖做传递闭包, 列出未在 DSC 中声明的库类
$ErrorActionPreference = 'SilentlyContinue'

$edk2 = 'D:\t158-uefi\edk2-edk2-stable202405'
$prj  = 'D:\t158-uefi\edk2'
$dsc  = Join-Path $prj 'T158Pkg\T158Pkg.dsc'

if (-not (Test-Path $dsc)) { Write-Host "!! 找不到 $dsc"; exit 1 }

# ---------- 解析 DSC ----------
$lines = Get-Content -LiteralPath $dsc
$map = @{}          # 库类名 -> INF 相对路径
$components = New-Object System.Collections.Generic.List[string]
$sec = ''
foreach ($l in $lines) {
    $t = $l.Trim()
    if ($t.StartsWith('[')) { $sec = $t; continue }
    if ($sec.StartsWith('[LibraryClasses')) {
        if ($t -match '^([A-Za-z0-9_]+)\s*\|\s*(\S+)$') { $map[$Matches[1]] = $Matches[2] }
    }
    if ($sec.StartsWith('[Components')) {
        if ($t -match '^([A-Za-z0-9_]+)Pkg/[A-Za-z0-9_./\-]+\.inf') { $components.Add($Matches[0]) }
    }
}

# ---------- 工具 ----------
function Resolve-Inf([string]$rel) {
    $p = Join-Path $edk2 ($rel -replace '/', '\')
    if (Test-Path -LiteralPath $p) { return $p }
    $p = Join-Path $prj ($rel -replace '/', '\')
    if (Test-Path -LiteralPath $p) { return $p }
    return $null
}
function Get-InfLibs([string]$path) {
    $out = New-Object System.Collections.Generic.List[string]
    $in = $false
    foreach ($l in (Get-Content -LiteralPath $path)) {
        $t = $l.Trim()
        if ($t.StartsWith('[')) { $in = $t.StartsWith('[LibraryClasses'); continue }
        if (-not $in) { continue }
        if ($t -eq '' -or $t.StartsWith('#')) { continue }
        $t = ($t -split '#')[0].Trim()
        if ($t -ne '') { $out.Add($t) }
    }
    return $out
}

# ---------- 传递闭包 ----------
$missing = @{}
$visited = @{}
$queue = New-Object System.Collections.Generic.Queue[string]
foreach ($c in $components) { $queue.Enqueue($c) }

while ($queue.Count -gt 0) {
    $rel = $queue.Dequeue()
    if ($visited.ContainsKey($rel)) { continue }
    $visited[$rel] = $true
    $full = Resolve-Inf $rel
    if (-not $full) { continue }
    foreach ($lib in (Get-InfLibs $full)) {
        if ($map.ContainsKey($lib)) {
            $queue.Enqueue($map[$lib])
        } elseif (-not $missing.ContainsKey($lib)) {
            $missing[$lib] = (Split-Path $full -Leaf)
        }
    }
}

Write-Host ("===== 模块数: " + $components.Count + "  已声明库类: " + $map.Count + " =====")
Write-Host ("===== 未声明库类: " + $missing.Count + " =====")
foreach ($k in ($missing.Keys | Sort-Object)) {
    Write-Host ("  " + $k + "   (被 " + $missing[$k] + " 需要)")
}
