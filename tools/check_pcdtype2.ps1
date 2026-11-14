# 检查 DSC 中 PCD 所处的 [PcdsXXX] 段类型, 是否与其 DEC 声明类型一致 (跨所有包)
$ErrorActionPreference = 'SilentlyContinue'

$edk2 = 'D:\t158-uefi\edk2-edk2-stable202405'
$prj  = 'D:\t158-uefi\edk2'
$dsc  = Join-Path $prj 'T158Pkg\T158Pkg.dsc'

# 1) 所有 DEC 中的 PCD -> 类型
$decType = @{}
$decNames = @('MdePkg.dec', 'MdeModulePkg.dec', 'ArmPkg.dec', 'ArmPlatformPkg.dec', 'EmbeddedPkg.dec', 'ShellPkg.dec', 'T158Pkg.dec')
foreach ($n in $decNames) {
    foreach ($f in (Get-ChildItem -Path $edk2, $prj -Recurse -Filter $n -File)) {
        $sec = ''
        foreach ($l in (Get-Content -LiteralPath $f.FullName)) {
            $t = $l.Trim()
            if ($t.StartsWith('[')) { $sec = $t; continue }
            if ($sec -notmatch '^\[Pcds') { continue }
            if ($t -eq '' -or $t.StartsWith('#')) { continue }
            if ($t -match '^([A-Za-z0-9_]+)\.([A-Za-z0-9_]+)\s*\|') {
                $type = $sec -replace '\[Pcds|\].*', ''
                if ($type -like 'Dynamic*') { $type = 'Dynamic' }
                $decType[$Matches[2]] = $type
            }
        }
    }
}

# 2) DSC 中的 PCD -> 段类型
$bad = New-Object System.Collections.Generic.List[string]
$sec = ''
foreach ($l in (Get-Content -LiteralPath $dsc)) {
    $t = $l.Trim()
    if ($t.StartsWith('[')) { $sec = $t; continue }
    if ($sec -notmatch '^\[Pcds') { continue }
    if ($t -eq '' -or $t.StartsWith('#')) { continue }
    if ($t -match '^([A-Za-z0-9_]+)\.([A-Za-z0-9_]+)\s*\|') {
        $name = $Matches[2]
        $dst = $sec -replace '\[Pcds|\].*', ''
        if ($dst -like 'Dynamic*') { $dst = 'Dynamic' }
        if ($decType.ContainsKey($name)) {
            if ($decType[$name] -ne $dst) {
                $bad.Add($name + " : DSC=" + $dst + "  DEC=" + $decType[$name])
            }
        }
    }
}

Write-Host ("===== DEC 中 PCD 数: " + $decType.Count + " =====")
Write-Host ("===== DSC/DEC 类型冲突: " + $bad.Count + " =====")
$bad | Sort-Object -Unique | ForEach-Object { Write-Host ("  " + $_) }
