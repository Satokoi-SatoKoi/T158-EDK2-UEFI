# 检查 INF 中 [FixedPcd]/[FeaturePcd]/[PatchPcd] 段引用的 PCD, 其 DEC 声明类型是否匹配
$ErrorActionPreference = 'SilentlyContinue'

$prj = 'D:\t158-uefi\edk2'
$decPath = Join-Path $prj 'T158Pkg\T158Pkg.dec'

# 1) 解析 DEC: PCD 名 -> 声明类型
$decType = @{}
$sec = ''
foreach ($l in (Get-Content -LiteralPath $decPath)) {
    $t = $l.Trim()
    if ($t.StartsWith('[')) { $sec = $t; continue }
    if ($sec -notmatch '^\[Pcds') { continue }
    if ($t -eq '' -or $t.StartsWith('#')) { continue }
    if ($t -match '^([A-Za-z0-9_]+)\.([A-Za-z0-9_]+)\s*\|') {
        $decType[$Matches[2]] = ($sec -replace '\[Pcds|\]', '')
    }
}

# INF 段 -> 要求的 DEC 类型
$want = @{ 'FixedPcd' = 'FixedAtBuild'; 'FeaturePcd' = 'FeatureFlag'; 'PatchPcd' = 'PatchableInModule' }

$bad = New-Object System.Collections.Generic.List[string]
foreach ($f in (Get-ChildItem -Path $prj -Recurse -Filter *.inf -File)) {
    $sec = ''
    foreach ($l in (Get-Content -LiteralPath $f.FullName)) {
        $t = $l.Trim()
        if ($t.StartsWith('[')) { $sec = $t; continue }
        $seg = $sec -replace '\[|\]', ''
        if (-not $want.ContainsKey($seg)) { continue }
        if ($t -eq '' -or $t.StartsWith('#')) { continue }
        if ($t -match '^([A-Za-z0-9_]+)\.([A-Za-z0-9_]+)') {
            $name = $Matches[2]
            if ($decType.ContainsKey($name)) {
                if ($decType[$name] -ne $want[$seg]) {
                    $bad.Add((Split-Path $f.FullName -Leaf) + " [$seg] " + $name + " : DEC 中是 " + $decType[$name] + ", 应为 " + $want[$seg])
                }
            }
        }
    }
}

Write-Host ("===== DEC 中 PCD 数: " + $decType.Count + " =====")
Write-Host ("===== 类型不匹配: " + $bad.Count + " =====")
$bad | Sort-Object -Unique | ForEach-Object { Write-Host ("  " + $_) }
