# 检查 DSC 里定义的 PCD, 是否能在某个 DEC 中查到同名声明
$ErrorActionPreference = 'SilentlyContinue'

$edk2 = 'D:\t158-uefi\edk2-edk2-stable202405'
$prj  = 'D:\t158-uefi\edk2'
$dsc  = Join-Path $prj 'T158Pkg\T158Pkg.dsc'

# 汇总所有 DEC 文本
$decAll = ''
foreach ($f in (Get-ChildItem -Path $edk2, $prj -Recurse -Filter *.dec -File)) {
    $decAll += (Get-Content -LiteralPath $f.FullName -Raw)
}

$missing = @{}
$total = 0
$sec = ''
foreach ($l in (Get-Content -LiteralPath $dsc)) {
    $t = $l.Trim()
    if ($t.StartsWith('[')) { $sec = $t; continue }
    if ($sec -notmatch '^\[Pcds') { continue }
    if ($t -eq '' -or $t.StartsWith('#')) { continue }
    if ($t -match '^([A-Za-z0-9_]+)\.([A-Za-z0-9_]+)\s*\|') {
        $total++
        $guid = $Matches[1]
        $name = $Matches[2]
        if ($decAll -notmatch ([regex]::Escape($name))) { $missing[$name] = $guid }
    }
}

Write-Host ("===== DSC 中 PCD 条目: " + $total + " =====")
Write-Host ("===== DEC 中查不到的 PCD: " + $missing.Count + " =====")
foreach ($k in ($missing.Keys | Sort-Object)) {
    Write-Host ("  " + $missing[$k] + "." + $k)
}
