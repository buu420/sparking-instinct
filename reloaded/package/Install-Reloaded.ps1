param([string]$GameDirectory,[switch]$DetectOnly)
$ErrorActionPreference = 'Stop'
. ([IO.Path]::Combine($PSScriptRoot,'Reloaded-InstallCommon.ps1'))
. (Join-Path $PSScriptRoot 'Game-Install-Location.ps1')
. (Join-Path $PSScriptRoot 'Reloaded-Legacy.ps1')
. (Join-Path $PSScriptRoot 'Reloaded-Payload.ps1')

if ($DetectOnly) { Get-SparkingZeroInstallations; return }
$lock = Enter-SparkingReloadedLock
try {
    Assert-SparkingGameClosed
    $gameRoot = Resolve-ReloadedGameRoot $GameDirectory
    $gameExe = Assert-SparkingExecutable $gameRoot
    $gameBin = [IO.Path]::GetDirectoryName($gameExe)
    Assert-ReloadedDesktopRuntime
    $payload = Get-SafeReloadedPath $PSScriptRoot 'Payload'
    $manifest = Get-SafeReloadedPath $PSScriptRoot 'Payload.json'
    $package = Read-ReloadedPackageFiles $payload $manifest
    $manager = Get-SafeReloadedPath $gameRoot $SparkingReloadedFolder
    $backup = Get-SafeReloadedPath $manager $SparkingMigrationBackupPath
    $receiptPath = Get-SafeReloadedPath $manager $SparkingReceiptPath
    if (Test-Path -LiteralPath $receiptPath) { throw 'A Reloaded install or its rollback receipt already exists. Run Uninstall.cmd before reinstalling.' }
    if (Test-Path -LiteralPath $backup) {
        # A completed uninstall keeps its prior rollback bytes in a uniquely named archive.
        throw 'A legacy rollback is still pending. Run Uninstall.cmd before reinstalling.'
    }

    foreach ($record in @($package.files)) {
        $destination = Get-SafeReloadedPath $manager $record.path
        if (Test-Path -LiteralPath $destination) { throw "A file already exists at an install destination. $($record.path)" }
    }
    $autoStartTarget = Get-SafeReloadedPath $gameBin $SparkingAutoStartGamePath
    if (Test-Path -LiteralPath $autoStartTarget -PathType Container) { throw 'A directory already occupies the dsound.dll startup path.' }
    $autoStartSource = Get-SafeReloadedPath $payload $SparkingAutoStartSource
    $legacy = Get-LegacySuspendPlan $gameBin
    if ([IO.File]::Exists($autoStartTarget) -and
        ($legacy.moved.path -notcontains $SparkingAutoStartGamePath)) {
        throw 'The dsound.dll startup path belongs to another loader or mod and was preserved.'
    }
    $receipt = [ordered]@{
        schema=2; modId=$SparkingReloadedModId; version=$package.version
        phase='installing'; files=@($package.files); legacy=$legacy
        autoStart=[ordered]@{ path=$SparkingAutoStartGamePath; sha256=$package.autoStart.sha256; armed=$false }
    }
    Assert-SparkingGameClosed
    Set-ReloadedJson $receiptPath $receipt
    $copied = [Collections.Generic.List[object]]::new()
    try {
        foreach ($record in $package.files) {
            $source = Get-SafeReloadedPath $payload $record.path
            $destination = Get-SafeReloadedPath $manager $record.path
            Copy-ReloadedPackageFile $source $destination $record.sha256
            $copied.Add($record)
        }
        Assert-ReloadedModConfig $manager
        Assert-SparkingGameClosed
        Suspend-LegacyRepair $gameBin $backup $legacy
        Assert-LegacyBootstrapSuspended $gameBin
        Assert-SparkingGameClosed
        $receipt.autoStart.armed = $true
        Set-ReloadedJson $receiptPath $receipt
        Copy-ReloadedPackageFile $autoStartSource $autoStartTarget $receipt.autoStart.sha256
        $receipt.phase = 'installed'
        Set-ReloadedJson $receiptPath $receipt
    } catch {
        $installFailure = $_
        try {
            Assert-SparkingGameClosed
            Remove-ReloadedAutoStart $gameBin $receipt.autoStart $legacy
            if (Restore-LegacyRepair $gameBin $backup $legacy) {
                if ($copied.Count) { Remove-ReloadedPackageFiles $manager $copied.ToArray() }
                if ([IO.Directory]::Exists($backup)) {
                    $savedBackup = Get-SafeReloadedPath $manager ('LegacyRollback-' + [guid]::NewGuid().ToString('N'))
                    [IO.Directory]::Move($backup,$savedBackup)
                }
                [IO.File]::Delete($receiptPath)
            }
        } catch { Write-Warning ('Automatic rollback is incomplete. Run Uninstall.cmd after closing the game. ' + $_.Exception.Message) }
        throw $installFailure
    }
    Write-Output 'Sparking Instinct is installed.'
    Write-Output 'Start Sparking! ZERO from Steam or your usual desktop shortcut.'
    Write-Output ('To remove the port and restore the previous install: ' + (Join-Path $manager 'Uninstall.cmd'))
} finally { Exit-SparkingReloadedLock $lock }
