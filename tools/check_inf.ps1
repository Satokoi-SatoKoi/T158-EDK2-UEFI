# 静态检查 T158Pkg 每个 INF: 源码里用到的 PCD 是否在 [FixedPcd]/[Pcd]/[PatchPcd] 声明,
# 用到的库 API 对应库类是否在 [LibraryClasses] 声明
$ErrorActionPreference = 'SilentlyContinue'
$prj = 'D:\t158-uefi\edk2\T158Pkg'

$apiMap = @{
  'MemoryAllocationLib'        = 'FreePool|AllocatePool|AllocateZeroPool|AllocatePages|FreePages|ReallocatePool|AllocateCopyPool'
  'BaseMemoryLib'              = 'CopyMem|SetMem|ZeroMem|CompareMem|ScanMem|CompareGuid|CopyGuid|ScanMem8'
  'DebugLib'                   = 'DEBUG\s*\(|ASSERT\s*\(|DebugPrint|DebugAssert'
  'PrintLib'                   = 'UnicodeSPrint|AsciiSPrint|Print\s*\(|AsciiPrint\s*\('
  'UefiBootServicesTableLib'   = 'gBS->|gST->'
  'UefiRuntimeServicesTableLib'= 'gRT->'
  'IoLib'                      = 'MmioRead|MmioWrite|IoRead|IoWrite|MmioOr|MmioAnd'
  'UefiBootManagerLib'         = 'EfiBootManager'
  'DevicePathLib'              = 'AppendDevicePath|DuplicateDevicePath|DevicePathFromHandle|LocateDevicePath|IsDevicePathEnd'
  'SerialPortLib'              = 'SerialPort'
  'ArmMmuLib'                  = 'ArmMmu|ArmSetMemoryAttributes|ArmConfigureMmu'
  'CacheMaintenanceLib'        = 'InvalidateDataCache|WriteBackDataCache|InvalidateInstructionCache'
  'PeCoffLib'                  = 'PeCoffLoader|PeCoffGetEntryPoint'
  'PcdLib'                     = 'PcdGet|FixedPcdGet|PatchPcdGet|FeaturePcdGet'
  'UefiLib'                    = 'EfiCreateEvent|Print\s*\(|gST->ConOut|EfiLib'
  'HobLib'                     = 'GetFirstHob|GetNextHob|BuildHob|GetHobList|HobBuilder'
  'ArmLib'                     = 'ArmRead|ArmWrite|ArmGet|ArmSet|ArmDataSynchronizationBarrier'
  'TimerLib'                   = 'GetPerformanceCounter|MicroSecondDelay|NanoSecondDelay'
}

$report = New-Object System.Collections.Generic.List[string]

foreach ($inf in (Get-ChildItem -Path $prj -Recurse -Filter *.inf -File)) {
  $lines = Get-Content -LiteralPath $inf.FullName
  $sec = ''
  $src = @(); $libs = @(); $fpcd = @(); $pcd = @(); $ppcd = @(); $fepcd = @()
  foreach ($l in $lines) {
    $t = $l.Trim()
    if ($t.StartsWith('[')) { $sec = $t; continue }
    if ($t -eq '' -or $t.StartsWith('#')) { continue }
    $name = ($t -split '\s+#')[0].Trim()
    switch -Regex ($sec) {
      '^\[Sources'        { if ($name -match '\.c$') { $src += $name } }
      '^\[LibraryClasses' { $libs += ($name -split '\|')[0].Trim() }
      '^\[FixedPcd'       { $fpcd += (($name -split '\.')[-1]) }
      '^\[PatchPcd'       { $ppcd += (($name -split '\.')[-1]) }
      '^\[FeaturePcd'     { $fepcd += (($name -split '\.')[-1]) }
      '^\[Pcd'            { $pcd += (($name -split '\.')[-1]) }
    }
  }

  $dir = Split-Path $inf.FullName -Parent
  $code = ''
  foreach ($c in $src) { $p = Join-Path $dir $c; if (Test-Path -LiteralPath $p) { $code += (Get-Content -LiteralPath $p -Raw) } }
  if ($code -eq '') { continue }

  # PCD 检查
  foreach ($m in [regex]::Matches($code, '(FixedPcd|PatchPcd|FeaturePcd|Pcd)Get\w*\s*\(\s*(\w+)')) {
    $kind = $m.Groups[1].Value; $nm = $m.Groups[2].Value
    $pool = switch ($kind) { 'FixedPcd' { $fpcd } 'PatchPcd' { $ppcd } 'FeaturePcd' { $fepcd } default { $pcd } }
    if ($pool -notcontains $nm) {
      $report.Add(($inf.Name) + " : 用了 " + $kind + "Get(" + $nm + ") 但 INF 的 [" + $kind + "Pcd] 未声明")
    }
  }
  # 库类检查
  foreach ($lib in $apiMap.Keys) {
    if ($code -match $apiMap[$lib]) {
      if ($libs -notcontains $lib) {
        $report.Add(($inf.Name) + " : 代码用到 " + $lib + " 的 API 但 [LibraryClasses] 未声明")
      }
    }
  }
}

Write-Host ("===== 疑似缺失: " + $report.Count + " =====")
$report | Sort-Object -Unique | ForEach-Object { Write-Host ("  " + $_) }
