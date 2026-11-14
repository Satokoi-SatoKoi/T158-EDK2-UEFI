# 校验 T158Pkg 的 DSC/FDF/DEC/INF 里引用的 edk2 文件路径是否真实存在
$ErrorActionPreference = 'SilentlyContinue'

$edk2 = 'D:\t158-uefi\edk2-edk2-stable202405'
$prj  = 'D:\t158-uefi\edk2'

if (-not (Test-Path $edk2)) { Write-Host "!! EDK2 源码不存在: $edk2"; exit 1 }
if (-not (Test-Path $prj))  { Write-Host "!! 工程不存在: $prj"; exit 1 }

$files = Get-ChildItem -Path $prj -Recurse -Include *.dsc,*.fdf,*.dec,*.inf -File
$missing = New-Object System.Collections.Generic.List[string]

foreach ($f in $files) {
    $txt = Get-Content -LiteralPath $f.FullName -Raw
    if (-not $txt) { continue }
    foreach ($m in [regex]::Matches($txt, '([A-Za-z0-9_]+)Pkg/[A-Za-z0-9_./\-]+\.(inf|dec|dsc|fdf)')) {
        $rel = $m.Value -replace '/', '\'
        $p1 = Join-Path $edk2 $rel
        $p2 = Join-Path $prj  $rel
        if (-not ((Test-Path -LiteralPath $p1) -or (Test-Path -LiteralPath $p2))) {
            $missing.Add((Split-Path $f.FullName -Leaf) + '  =>  ' + $m.Value)
        }
    }
}

Write-Host ("===== 检查文件数: " + $files.Count + " =====")
Write-Host ("===== 缺失引用: " + $missing.Count + " =====")
$missing | Sort-Object -Unique | ForEach-Object { Write-Host $_ }
