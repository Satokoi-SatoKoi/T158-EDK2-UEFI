# 对比 T158Pkg 的 INF/DSC/FDF 中引用的 GUID / PCD / Protocol / Ppi, 是否在被引用包的 DEC 中声明
$ErrorActionPreference = 'SilentlyContinue'

$edk2 = 'D:\t158-uefi\edk2-edk2-stable202405'
$prj  = 'D:\t158-uefi\edk2'

# 读入每个 DEC 的 [Guids]/[Protocols]/[Ppis] 段内容
$decNames = @('MdePkg.dec', 'MdeModulePkg.dec', 'ArmPkg.dec', 'ShellPkg.dec', 'EmbeddedPkg.dec', 'T158Pkg.dec')
$decText = @{}
foreach ($n in $decNames) {
    $txt = ''
    foreach ($f in (Get-ChildItem -Path $edk2, $prj -Recurse -Filter $n -File)) {
        $txt += (Get-Content -LiteralPath $f.FullName -Raw)
    }
    $decText[$n] = $txt
}
$allDec = ($decText.Values -join "`n")

# 扫描工程里 INF 的 [Guids]/[Protocols]/[Ppis] 条目
$bad = New-Object System.Collections.Generic.List[string]
foreach ($f in (Get-ChildItem -Path $prj -Recurse -Filter *.inf -File)) {
    $sec = ''
    foreach ($l in (Get-Content -LiteralPath $f.FullName)) {
        $t = $l.Trim()
        if ($t.StartsWith('[')) { $sec = $t; continue }
        if ($sec -notmatch '^\[(Guids|Protocols|Ppis)') { continue }
        if ($t -eq '' -or $t.StartsWith('#')) { continue }
        $name = ($t -split '#')[0].Trim()
        if ($name -eq '') { continue }
        if ($allDec -notmatch ([regex]::Escape($name))) {
            $bad.Add((Split-Path $f.FullName -Leaf) + ' : ' + $name)
        }
    }
}

Write-Host ("===== 工程 INF 数: " + (Get-ChildItem -Path $prj -Recurse -Filter *.inf -File).Count + " =====")
Write-Host ("===== DEC 中查不到的 GUID/Protocol: " + $bad.Count + " =====")
$bad | Sort-Object -Unique | ForEach-Object { Write-Host ("  " + $_) }
