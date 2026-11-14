# 检查: 每个模块用到的库实例, 是否声明支持该模块的 MODULE_TYPE
$ErrorActionPreference = 'SilentlyContinue'

$edk2 = 'D:\t158-uefi\edk2-edk2-stable202405'
$prj  = 'D:\t158-uefi\edk2'
$dsc  = Join-Path $prj 'T158Pkg\T158Pkg.dsc'

$map = @{}
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
    $p = Join-Path $edk2 ($rel -replace '/', '\'); if (Test-Path -LiteralPath $p) { return $p }
    $p = Join-Path $prj ($rel -replace '/', '\');  if (Test-Path -LiteralPath $p) { return $p }
    return $null
}
# 返回 INF 的 LIBRARY_CLASS 支持的模块类型列表; 无 "|" 表示不限
function Get-Supported([string]$path) {
    foreach ($l in (Get-Content -LiteralPath $path)) {
        $t = $l.Trim()
        if ($t -match '^LIBRARY_CLASS\s*=\s*(.+)$') {
            $v = $Matches[1].Trim()
            if ($v -like '*|*') { return ($v -split '\|')[1].Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries) }
            return @()
        }
    }
    return @()
}
function Get-Field([string]$path, [string]$field) {
    foreach ($l in (Get-Content -LiteralPath $path)) {
        if ($l.Trim() -match "^$field\s*=\s*(\S+)") { return $Matches[1] }
    }
    return $null
}
function Get-InfLibs([string]$path) {
    $out = New-Object System.Collections.Generic.List[string]
    $in = $false
    foreach ($l in (Get-Content -LiteralPath $path)) {
        $t = $l.Trim()
        if ($t.StartsWith('[')) { $in = $t.StartsWith('[LibraryClasses'); continue }
        if (-not $in -or $t -eq '' -or $t.StartsWith('#')) { continue }
        $t = ($t -split '#')[0].Trim()
        if ($t -ne '') { $out.Add($t) }
    }
    return $out
}

$bad = New-Object System.Collections.Generic.List[string]

foreach ($c in $components) {
    $full = Resolve-Inf $c
    if (-not $full) { continue }
    $mtype = Get-Field $full 'MODULE_TYPE'; if (-not $mtype) { $mtype = 'UNKNOWN' }

    $seen = @{}
    $q = New-Object System.Collections.Generic.Queue[string]
    foreach ($lib in (Get-InfLibs $full)) { $q.Enqueue($lib) }

    while ($q.Count -gt 0) {
        $lib = $q.Dequeue()
        if ($seen.ContainsKey($lib)) { continue }
        $seen[$lib] = $true
        $inst = $null
        if ($map.ContainsKey("$mtype::$lib")) { $inst = $map["$mtype::$lib"] }
        elseif ($map.ContainsKey("common::$lib")) { $inst = $map["common::$lib"] }
        if (-not $inst) { continue }

        $ip = Resolve-Inf $inst
        if (-not $ip) { continue }
        $sup = Get-Supported $ip
        if ($sup.Count -gt 0 -and ($sup -notcontains $mtype)) {
            $bad.Add(("[" + $mtype + "] " + (Split-Path $full -Leaf) + " : " + $lib + " -> " + (Split-Path $inst -Leaf)))
        }
        foreach ($x in (Get-InfLibs $ip)) { $q.Enqueue($x) }
    }
}

Write-Host ("===== 模块数: " + $components.Count + " =====")
Write-Host ("===== 实例不支持该模块类型: " + $bad.Count + " =====")
foreach ($b in $bad) { Write-Host ("  " + $b) }
