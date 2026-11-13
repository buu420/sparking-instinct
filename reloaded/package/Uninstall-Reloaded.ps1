param([string]$GameDirectory,[switch]$WhatIf)
$ErrorActionPreference = 'Stop'
. ([IO.Path]::Combine($PSScriptRoot,'Reloaded-InstallCommon.ps1'))
. (Join-Path $PSScriptRoot 'Game-Install-Location.ps1')
. (Join-Path $PSScriptRoot 'Reloaded-Legacy.ps1')
. (Join-Path $PSScriptRoot 'Reloaded-Payload.ps1')
. (Join-Path $PSScriptRoot 'Reloaded-LaunchSession.ps1')

$lock = Enter-SparkingReloadedLock
try {
    Assert-SparkingGameClosed
    if (-not $GameDirectory -and (Split-Path -Leaf $PSScriptRoot) -ieq $SparkingReloadedFolder -and
        (Test-SparkingZeroFolder (Split-Path -Parent $PSScriptRoot))) {
        $GameDirectory = Split-Path -Parent $PSScriptRoot
    }
    $gameRoot = Resolve-ReloadedGameRoot $GameDirectory
    $gameBin = Split-Path -Parent (Get-SafeReloadedPath $gameRoot $SparkingShipping)
    $manager = Get-SafeReloadedPath $gameRoot $SparkingReloadedFolder
    $receiptPath = Get-SafeReloadedPath $manager $SparkingReceiptPath
    if (-not [IO.File]::Exists($receiptPath)) { Write-Output 'No Sparking Instinct installation receipt found.'; return }
    $receipt = Get-ReloadedJson $receiptPath
    if ($receipt.installOwner -ceq 'Amethyst') {
        throw 'This copy is managed by Amethyst. Use Uninstall in the manager.'
    }
    Assert-ReloadedInstallationReceipt $receipt
    if ($WhatIf) {
        Write-Output ('Would remove matching Reloaded files and restore backed-up legacy files in: ' + $gameRoot)
        return
    }
    $backup = Get-SafeReloadedPath $manager $SparkingMigrationBackupPath
    if ($receipt.schema -eq 1) { Complete-PreviousReloadedSession $manager $gameBin }
    else {
        Assert-SparkingGameClosed
        Remove-ReloadedAutoStart $gameBin $receipt.autoStart $receipt.legacy
    }
    Remove-ReloadedPackageFiles $manager $receipt.files -KeepTools
    $appConfig = Get-SafeReloadedPath $manager $SparkingAppConfigPath
    $modConfig = Get-SafeReloadedPath $manager "$SparkingModFolder\ModConfig.json"
    $modDll = Get-SafeReloadedPath $manager "$SparkingModFolder\SparkingZeroAccess.Native.dll"
    if ([IO.File]::Exists($appConfig) -and [IO.File]::Exists($modConfig) -and [IO.File]::Exists($modDll)) {
        throw 'The modified Reloaded profile and native mod were preserved. Disable the profile before restoring the legacy loader.'
    }
    Assert-SparkingGameClosed
    if (-not (Restore-LegacyRepair $gameBin $backup $receipt.legacy)) {
        throw 'Legacy rollback needs manual review. The receipt and backups remain beside the installed uninstaller.'
    }
    if ([IO.Directory]::Exists($backup)) {
        $archive = Get-SafeReloadedPath $manager ('LegacyRollback-' + [guid]::NewGuid().ToString('N'))
        [IO.Directory]::Move($backup,$archive)
    }
    Remove-ReloadedPackageFiles $manager $receipt.files
    [IO.File]::Delete($receiptPath)
    Write-Output 'Sparking Instinct was removed. Matching legacy files were restored.'
} finally { Exit-SparkingReloadedLock $lock }
