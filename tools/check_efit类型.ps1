# 找出使用了 EFI_ 类型/全局量 但没有引入 Uefi.h / PiPei.h / UefiBaseType.h 的 .c 文件
$ErrorActionPreference = 'SilentlyContinue'
$prj = 'D:\t158-uefi\edk2\T158Pkg'

$useEfi   = 'EFI_STATUS|EFI_SUCCESS|EFI_GUID|EFI_HANDLE|EFI_PHYSICAL_ADDRESS|EFI_BOOT_SERVICES|EFI_SYSTEM_TABLE|gBS|gST|gRT'
$goodInc  = '<Uefi\.h>|<PiPei\.h>|<PiDxe\.h>|<UefiBaseType\.h>|<Uefi/UefiBaseType\.h>|<Pi/PiHob\.h>|<Library/UefiBootServicesTableLib\.h>|<Library/UefiRuntimeServicesTableLib\.h>'

$bad = New-Object System.Collections.Generic.List[string]
foreach ($f in (Get-ChildItem -Path $prj -Recurse -Filter *.c -File)) {
    $txt = Get-Content -LiteralPath $f.FullName -Raw
    if ($txt -notmatch $useEfi) { continue }
    if ($txt -match $goodInc) { continue }
    $first = ($txt -split "`n" | Where-Object { $_ -match 'EFI_|gBS|gST|gRT' } | Select-Object -First 3)
    $bad.Add((Split-Path $f.FullName -Leaf) + '  <<< ' + ($first -join ' | '))
}

Write-Host ("===== 可疑文件: " + $bad.Count + " =====")
$bad | ForEach-Object { Write-Host ("  " + $_) }
