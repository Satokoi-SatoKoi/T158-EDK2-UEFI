# 列出 DSC [Components] 中所有模块的 MODULE_TYPE, 对照 FDF 中的 [Rule...] 是否齐全
$ErrorActionPreference = 'SilentlyContinue'

$edk2 = 'D:\t158-uefi\edk2-edk2-stable202405'
$prj  = 'D:\t158-uefi\edk2'
$dsc  = Join-Path $prj 'T158Pkg\T158Pkg.dsc'
$fdf  = Join-Path $prj 'T158Pkg\T158Pkg.fdf'

# FDF 中已有的 rule (取 [Rule.XXX.YYY] 的末段)
$rules = @{}
foreach ($l in (Get-Content -LiteralPath $fdf)) {
    $t = $l.Trim()
    if ($t -match '^\[Rule\.[A-Za-z0-9_.]+\]') {
        $full = $Matches[0]
        $last = ($full.Trim('[', ']') -split '\.')[-1]
        $rules[$last] = $true
    }
}

# DSC Components 中的 INF -> MODULE_TYPE
$types = @{}
$sec = ''
foreach ($l in (Get-Content -LiteralPath $dsc)) {
    $t = $l.Trim()
    if ($t.StartsWith('[')) { $sec = $t; continue }
    if (-not $sec.StartsWith('[Components')) { continue }
    if ($t -eq '' -or $t.StartsWith('#')) { continue }
    $m = $t -split '\s+' | Select-Object -First 1
    if ($m -notmatch '\.inf$') { continue }
    $found = $null
    foreach ($base in @($prj, $edk2)) {
        $p = Join-Path $base $m
        if (Test-Path -LiteralPath $p) { $found = $p; break }
    }
    if (-not $found) { continue }
    $mt = 'UNKNOWN'
    foreach ($il in (Get-Content -LiteralPath $found)) {
        if ($il -match '^\s*MODULE_TYPE\s*=\s*(\S+)') { $mt = $Matches[1]; break }
    }
    $types[$mt] = $true
}

Write-Host "===== FDF 中已有的 Rule ====="
($rules.Keys | Sort-Object) -join ', '
Write-Host ""
Write-Host "===== DSC 中出现的模块类型 ====="
$miss = @()
foreach ($k in ($types.Keys | Sort-Object)) {
    $ok = $rules.ContainsKey($k)
    Write-Host ("  " + $k + "  -> FDF rule: " + $(if ($ok) { '有' } else { '缺失 !!!' }))
    if (-not $ok) { $miss += $k }
}
Write-Host ""
Write-Host ("===== 缺 rule 的类型数: " + $miss.Count + " =====")
