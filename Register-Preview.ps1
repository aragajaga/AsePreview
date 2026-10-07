[CmdletBinding()]
param(
    [string]$DllPath = (Join-Path $PSScriptRoot 'x64\Release\AsePreview.dll'),
    [switch]$BackupOnly,
    [string]$RestoreBackup
)
$ErrorActionPreference = 'Stop'
$clsid = '{5D8C0E7A-4C0A-4C4E-9B6D-7F479E5F6D11}'
$classKey = "SOFTWARE\Classes\CLSID\$clsid"
$associationKey = 'SOFTWARE\Classes\Photoshop.ExchangeableSwatchFile.200\shellex\{8895b1c6-b41f-4c1c-a562-0d564250836f}'
$listKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\PreviewHandlers'
$values = @(
    @{ Key=$classKey; Name=''; Data='Aragajaga ASE Preview Handler' },
    @{ Key=$classKey; Name='AppID'; Data='{6D2B5079-2F0B-48DD-AB7F-97CEC514D30B}' },
    @{ Key="$classKey\InprocServer32"; Name=''; Data=$DllPath },
    @{ Key="$classKey\InprocServer32"; Name='ThreadingModel'; Data='Apartment' },
    @{ Key=$associationKey; Name=''; Data=$clsid },
    @{ Key=$listKey; Name=$clsid; Data='Aragajaga ASE Preview Handler' }
)
$machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
try {
    $restore = $null
    if ($RestoreBackup) {
        $restore = @(Get-Content -LiteralPath $RestoreBackup -Raw | ConvertFrom-Json)
        # A backup may restore only the precise values this script owns.
        if ($restore.Count -ne $values.Count) { throw 'Invalid backup value count.' }
        for ($i=0; $i -lt $values.Count; ++$i) {
            if ($restore[$i].Key -ne $values[$i].Key -or $restore[$i].Name -ne $values[$i].Name) { throw 'Backup contains an unexpected registry target.' }
        }
    } else {
        $DllPath = (Resolve-Path -LiteralPath $DllPath).Path
        $values[2].Data = $DllPath
        $bytes = [IO.File]::ReadAllBytes($DllPath)
        if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw 'Not a PE DLL.' }
        $pe = [BitConverter]::ToInt32($bytes,60)
        if ($pe -lt 0 -or $pe + 24 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or
            [BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664 -or ([BitConverter]::ToUInt16($bytes,$pe+22) -band 0x2000) -eq 0) {
            throw 'Registration requires an x64 DLL.'
        }
    }
    $snapshots = foreach ($value in $values) {
        $key = $machine.OpenSubKey($value.Key)
        try {
            $exists = $null -ne $key -and $key.GetValueNames() -contains $value.Name
            [pscustomobject]@{ Key=$value.Key; Name=$value.Name; Exists=$exists;
                Kind=$(if ($exists) { $key.GetValueKind($value.Name).ToString() } else { 'String' });
                Data=$(if ($exists) { $key.GetValue($value.Name,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) } else { $null }) }
        } finally { if ($key) { $key.Dispose() } }
    }
    $backupDir = Join-Path $PSScriptRoot ('_maintenance\backups\registry-' + (Get-Date -Format 'yyyyMMdd-HHmmss-ffff'))
    New-Item -ItemType Directory -Path $backupDir | Out-Null
    $backupPath = Join-Path $backupDir 'values.json'
    $snapshots | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $backupPath -Encoding utf8
    $exportKeys = @($classKey,$associationKey,$listKey)
    for ($i=0; $i -lt $exportKeys.Count; ++$i) {
        $existing = $machine.OpenSubKey($exportKeys[$i])
        if ($existing) {
            $existing.Dispose()
            & reg.exe export ('HKLM\' + $exportKeys[$i]) (Join-Path $backupDir "key-$i.reg") /y /reg:64 | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Registry export failed; no changes applied.' }
        }
    }
    Write-Output "Registry backup: $backupPath"
    if ($BackupOnly) { return }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'HKLM requires an administrator PowerShell session. Backup created; registration unchanged.'
    }
    $target = if ($restore) { $restore } else { $values }
    try {
        foreach ($value in $target) {
            $key = $machine.CreateSubKey($value.Key)
            try {
                if ($restore -and -not $value.Exists) { $key.DeleteValue($value.Name,$false) }
                else {
                    $kind = if ($restore) { [Microsoft.Win32.RegistryValueKind]::$($value.Kind) } else { [Microsoft.Win32.RegistryValueKind]::String }
                    $key.SetValue($value.Name,$value.Data,$kind)
                }
            } finally { $key.Dispose() }
        }
        foreach ($value in $target) {
            $key = $machine.OpenSubKey($value.Key)
            try {
                $exists = $null -ne $key -and $key.GetValueNames() -contains $value.Name
                if ($restore -and -not $value.Exists) {
                    if ($exists) { throw 'Registry restore verification failed.' }
                } elseif (-not $exists -or $key.GetValue($value.Name,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) -ne $value.Data) {
                    throw 'Registry write verification failed.'
                }
            } finally { if ($key) { $key.Dispose() } }
        }
    } catch {
        # Restore only our values; leave other handlers and values untouched.
        foreach ($value in $snapshots) {
            $key = $machine.CreateSubKey($value.Key)
            try {
                if ($value.Exists) { $key.SetValue($value.Name,$value.Data,[Microsoft.Win32.RegistryValueKind]::$($value.Kind)) }
                else { $key.DeleteValue($value.Name,$false) }
            } finally { $key.Dispose() }
        }
        throw
    }
    if (-not ('AseAssociationRefresh' -as [type])) { Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class AseAssociationRefresh {
    [DllImport("shell32.dll")] public static extern void SHChangeNotify(uint eventId, uint flags, System.IntPtr item1, System.IntPtr item2);
}
'@
    }
    [AseAssociationRefresh]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero)
    Write-Output $(if ($restore) { 'Previous registration restored.' } else { "Registered x64 preview handler: $DllPath" })
} finally { $machine.Dispose() }
