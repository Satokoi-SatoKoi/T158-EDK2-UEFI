# 建立 edk2 中 库类名 -> 实现 INF 的映射, 并输出指定类的候选
$ErrorActionPreference = 'SilentlyContinue'
$edk2 = 'D:\t158-uefi\edk2-edk2-stable202405'

$map = @{}
$infs = Get-ChildItem -Path $edk2 -Recurse -Filter *.inf -File
foreach ($f in $infs) {
    $txt = Get-Content -LiteralPath $f.FullName -Raw
    if (-not $txt) { continue }
    if ($txt -match 'LIBRARY_CLASS\s*=\s*([A-Za-z0-9_]+)') {
        $cls = $Matches[1]
        if (-not $map.ContainsKey($cls)) { $map[$cls] = New-Object System.Collections.Generic.List[string] }
        $map[$cls].Add($f.FullName.Substring($edk2.Length + 1))
    }
}

$want = @(
 'AuthVariableLib','CapsuleLib','CpuExceptionHandlerLib','CpuLib','DebugAgentLib',
 'DefaultExceptionHandlerLib','DxeCoreEntryPoint','ImagePropertiesRecordLib','OrderedCollectionLib',
 'PeCoffExtraActionLib','RegisterFilterLib','ReportStatusCodeLib','ResetSystemLib','SafeIntLib',
 'SecurityManagementLib','SynchronizationLib','TimerLib','TpmMeasurementLib','UefiDecompressLib',
 'VarCheckLib','VariableFlashInfoLib','VariablePolicyHelperLib','VariablePolicyLib'
)

foreach ($w in $want) {
    Write-Host ("== " + $w)
    if ($map.ContainsKey($w)) {
        foreach ($p in $map[$w]) { Write-Host ("   " + ($p -replace '\\', '/')) }
    } else { Write-Host "   (无实现)" }
}
