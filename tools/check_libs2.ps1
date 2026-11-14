# 按"模块类型作用域"检查: 每个模块需要的库类, 是否在其 MODULE_TYPE 段或 common 段有实例
$ErrorActionPreference = 'SilentlyContinue'

$edk2 = 'D:\t158-uefi\edk2-edk2-stable202405'
$prj  = 'D:\t158-uefi\edk2'
$dsc  = Join-Path $prj 'T158Pkg\T158Pkg.dsc'

# ---------- DSC ----------
$map = @{}                 # "类型::类" -> INF
$components = New-Object System.Collections.Generic.List[string]
$sec = ''
foreach ($l in (Get-Content -LiteralPath $dsc)) {
    $t = $l.Trim()
    if ($t.StartsWith('[')) { $sec = $t; continue }
    if ($sec.StartsWith('[LibraryClasses')) {
        $type = 'common'
        if ($sec -match '\[LibraryClasses\.common\.([A-Za-z0-9_]+)\]') { $type = $Matches[1] }
        if ($t -match '^([A-Za-z0-9_]+)\s*\|\s*(\S+)$') { $map["$type::$($Matches[1])"] = $Matches[2] }
    }
    if ($sec.StartsWith('[Components')) {
        if ($t -match '^([A-Za-z0-9_]+)Pkg/[A-Za-z0-9_./\-]+\.inf') { $components.Add($Matches[0]) }
    }
}

function Resolve-Inf([string]$rel) {
    $p = Join-Path $edk2 ($rel -replace '/', '\')
    if (Test-Path -LiteralPath $p) { return $p }
    $p = Join-Path $prj ($rel -replace '/', '\')
    if (Test-Path -LiteralPath $p) { return $p }
    return $null
}
function Get-InfField([string]$path, [string]$field) {
    foreach ($l in (Get-Content -LiteralPath $path)) {
        $t = $l.Trim()
        if ($t.StartsWith('[')) { if ($t -notlike "$field*") { if ($seen) { break } } }
        if ($t -match "^$field\s*=\s*(\S+)") { return $Matches[1] }
    }
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

$bad = New-Object System.Collections.Generic.List[string]

foreach ($c in $components) {
    $full = Resolve-Inf $c
    if (-not $full) { continue }
    $mtype = Get-InfField $full 'MODULE_TYPE'
    if (-not $mtype) { $mtype = 'UNKNOWN' }

    $need = New-Object System.Collections.Generic.List[string]
    $q = New-Object System.Collections.Generic.Queue[string]
    foreach ($lib in (Get-InfLibs $full)) { $q.Enqueue($lib) }
    $seenCls = @{}
    while ($q.Count -gt 0) {
        $lib = $q.Dequeue()
        if ($seenCls.ContainsKey($lib)) { continue }
        $seenCls[$lib] = $true
        $inst = $null
        if ($map.ContainsKey("$mtype::$lib")) { $inst = $map["$mtype::$lib"] }
        elseif ($map.ContainsKey("common::$lib")) { $inst = $map["common::$lib"] }
        if ($inst) {
            $p = Resolve-Inf $inst
            if ($p) { foreach ($x in (Get-InfLibs $p)) { $q.Enqueue($x) } }
        } else {
            $need.Add($lib)
        }
    }
    if ($need.Count -gt 0) {
        $bad.Add(("[" + $mtype + "] " + (Split-Path $full -Leaf) + "  =>  " + ($need -join ', ')))
    }
}

Write-Host ("===== 模块数: " + $components.Count + " =====")
Write-Host ("===== 缺实例的模块: " + $bad.Count + " =====")
foreach ($b in $bad) { Write-Host ("  " + $b) }
