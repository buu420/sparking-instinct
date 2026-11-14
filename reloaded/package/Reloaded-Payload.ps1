$SparkingAppConfigPath = 'Apps\sparkingzero-win64-shipping.exe\AppConfig.json'
$SparkingModFolder = 'Mods\sparkingzero.accessibility.reloaded'
$SparkingHooksModId = 'reloaded.sharedlib.hooks'
$SparkingHooksConfigPath = 'Mods\reloaded.sharedlib.hooks\ModConfig.json'
$SparkingHooksOriginalConfigHash = 'A0204B7EAEA3A0B1FAA086AB38F349A014782D41F2608C9989CD0075FE74F4A1'
$SparkingHooksNormalizedConfigHash = 'ED086FA604A40472596638F549A541ED02B254F4B62AFB33E88C371DEE719CFD'
$SparkingReceiptPath = 'Installation.json'
$SparkingMigrationBackupPath = 'LegacyRollback'
$SparkingSessionPath = 'LaunchSession.json'
$SparkingAutoStartSource = 'AutoStart/dsound.dll'
$SparkingAutoStartGamePath = 'dsound.dll'
$SparkingLoaderFolder = 'Loader\x64'
$SparkingLocalLoaderFiles = @('Reloaded.Mod.Loader.dll','Reloaded.Mod.Loader.deps.json','Reloaded.Mod.Loader.runtimeconfig.json')
$SparkingPackageVersion = '0.2.2'
$SparkingPrismHash = '7C7D09C8C7306E8E1A0C46D603CCB2A391C194C77843400C11B746E7DD56A467'
$SparkingNarrationFiles = @('cue-sheet.json') +
    @(1..18 | ForEach-Object { 'op2-{0:00}.wav' -f $_ }) +
    @((1..17 + 19..20) | ForEach-Object { 'op3-{0:00}.wav' -f $_ })

function Get-ReloadedAppConfig {
    return [ordered]@{
        AppId='sparkingzero-win64-shipping.exe'
        AppName='DRAGON BALL Sparking! ZERO'
        AppLocation='..\..\..\SparkingZERO\Binaries\Win64\SparkingZERO-Win64-Shipping.exe'
        AppArguments=''
        AppIcon=''
        AutoInject=$false
        EnabledMods=@($SparkingReloadedModId)
        SortedMods=@($SparkingReloadedModId)
        PluginData=@{}
        PreserveDisabledModOrder=$true
        DontInject=$false
        IsMsStore=$false
    }
}

function Test-ReloadedFileRecords($Files) {
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($record in @($Files)) {
        $normalized = $record.path -replace '/','\'
        $parts = $normalized.Split('\')
        $reserved = @($SparkingReceiptPath,$SparkingSessionPath,'Logs','User','Plugins','AutoStart')
        $otherApp = $parts[0] -ieq 'Apps' -and ($parts.Count -lt 2 -or $parts[1] -ine 'sparkingzero-win64-shipping.exe')
        $otherMod = $parts[0] -ieq 'Mods' -and ($parts.Count -lt 2 -or $parts[1] -notin @($SparkingReloadedModId,$SparkingHooksModId))
        if ($record.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or -not $seen.Add($normalized) -or
            $parts[0] -in $reserved -or $parts[0] -like 'LegacyRollback*' -or $otherApp -or $otherMod) {
            throw 'The package file list is invalid.'
        }
    }
    if ($seen.Count -eq 0) { throw 'The package file list is empty.' }
}

function Read-ReloadedPackageFiles([string]$Payload,[string]$Manifest) {
    $document = Get-ReloadedJson $Manifest
    if ($document.schema -ne 2 -or $document.modId -ne $SparkingReloadedModId -or $document.version -ne $SparkingPackageVersion -or
        $document.autoStart.path -cne $SparkingAutoStartSource -or $document.autoStart.sha256 -eq $SparkingLegacyStartupHash -or
        $document.autoStart.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'This is not a supported Sparking Instinct package manifest.'
    }
    Test-ReloadedFileRecords $document.files
    $required = @('portable.txt',$SparkingAppConfigPath,
        "$SparkingModFolder\ModConfig.json","$SparkingModFolder\SparkingZeroAccess.Native.dll",
        "$SparkingModFolder\prism.dll",
        'Uninstall.cmd','LICENSES\Reloaded-LocalLoader-Source.zip','LICENSES\DotNet\LICENSE.TXT','LICENSES\DotNet\THIRD-PARTY-NOTICES.TXT')
    $required += @($SparkingNarrationFiles | ForEach-Object { "$SparkingModFolder\AudioDescription\$_" })
    $required += @($SparkingLocalLoaderFiles | ForEach-Object { "$SparkingLoaderFolder\$_" })
    foreach ($record in @($document.files) + @($document.autoStart)) {
        $path = Get-SafeReloadedPath $Payload $record.path
        if (-not [IO.File]::Exists($path) -or (Get-ReloadedHash $path) -ne $record.sha256) { throw "Package integrity check failed. $($record.path)" }
    }
    foreach ($path in $required) {
        $matches = @($document.files | Where-Object { ($_.path -replace '/','\') -ieq $path })
        if ($matches.Count -ne 1) { throw "Package entry missing. $path" }
    }
    return $document
}

function Assert-ReloadedInstallationReceipt($Receipt) {
    if ($Receipt.schema -notin @(1,2) -or $Receipt.modId -ne $SparkingReloadedModId -or
        $Receipt.phase -notin @('installing','installed') -or -not $Receipt.legacy -or
        -not $Receipt.legacy.PSObject.Properties['registry'] -or $Receipt.legacy.moved -isnot [array]) {
        throw 'The installation receipt could not be verified.'
    }
    Test-ReloadedFileRecords $Receipt.files
    if ($Receipt.schema -eq 1 -and ($Receipt.version -ne '0.1.0' -or $Receipt.PSObject.Properties['autoStart'])) {
        throw 'The installation receipt has inconsistent startup ownership.'
    }
    if ($Receipt.schema -eq 2 -and ($Receipt.version -notin @('0.1.1','0.1.2','0.1.3','0.2.0','0.2.1',$SparkingPackageVersion) -or
        $Receipt.autoStart.path -cne $SparkingAutoStartGamePath -or
        $Receipt.autoStart.sha256 -eq $SparkingLegacyStartupHash -or
        $Receipt.autoStart.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or $Receipt.autoStart.armed -isnot [bool] -or
        ($Receipt.phase -eq 'installed' -and -not $Receipt.autoStart.armed))) {
        throw 'The automatic startup entry in the installation receipt could not be verified.'
    }
}

function Remove-ReloadedAutoStart([string]$GameBin,$AutoStart,$Legacy) {
    if (-not $AutoStart.armed) { return }
    $path = Get-SafeReloadedPath $GameBin $SparkingAutoStartGamePath
    if (-not (Test-Path -LiteralPath $path)) { return }
    $oldFile = $Legacy.moved | Where-Object { $_.path -ieq $SparkingAutoStartGamePath } | Select-Object -First 1
    if ([IO.File]::Exists($path)) {
        $hash = Get-ReloadedHash $path
        if ($hash -eq $AutoStart.sha256) {
            [IO.File]::Delete($path)
            return
        }
        if ($oldFile -and $hash -eq $oldFile.sha256) { return }
    }
    if ($oldFile) {
        throw 'The current dsound.dll is not owned by SparkingZeroAccess. The file and legacy rollback receipt were preserved.'
    }
    Write-Warning 'A file or directory outside this installation now occupies the dsound.dll path and was preserved.'
}

function Copy-ReloadedPackageFile([string]$Source,[string]$Destination,[string]$Hash) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination)) | Out-Null
    $staging = $Destination + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::Copy($Source,$staging)
        if ((Get-ReloadedHash $staging) -ne $Hash) { throw "Staged package file failed verification. $Destination" }
        [IO.File]::Move($staging,$Destination)
    } finally {
        if ([IO.File]::Exists($staging)) { [IO.File]::Delete($staging) }
    }
}

function Test-ReloadedOwnedHash([string]$Relative,[string]$Recorded,[string]$Current) {
    if ($Current -eq $Recorded) { return $true }
    # Older receipts recorded the release's shorter config. The official
    # Reloaded II 1.31 serializer rewrote this exact config on first load.
    return ($Relative -replace '/','\') -ieq $SparkingHooksConfigPath -and
        $Recorded -eq $SparkingHooksOriginalConfigHash -and $Current -eq $SparkingHooksNormalizedConfigHash
}

function Remove-ReloadedPackageFiles([string]$Manager,$Files,[switch]$KeepTools) {
    Test-ReloadedFileRecords $Files
    $tools = @('Uninstall.cmd','Uninstall-Reloaded.ps1','Reloaded-InstallCommon.ps1','Reloaded-Legacy.ps1',
        'Reloaded-Payload.ps1','Reloaded-LaunchSession.ps1','Game-Install-Location.ps1')
    foreach ($record in @($Files)) {
        $relative = $record.path -replace '/','\'
        if ($KeepTools -and $relative -in $tools) { continue }
        $path = Get-SafeReloadedPath $Manager $relative
        if (-not [IO.File]::Exists($path)) { continue }
        if (-not (Test-ReloadedOwnedHash $relative $record.sha256 (Get-ReloadedHash $path))) {
            Write-Warning "Modified file preserved. $relative"
            continue
        }
        [IO.File]::Delete($path)
    }
}

function Assert-ReloadedModConfig([string]$Manager) {
    $app = Get-ReloadedJson (Get-SafeReloadedPath $Manager $SparkingAppConfigPath)
    $expected = Get-ReloadedAppConfig
    if ($app.AppId -ine $expected.AppId -or $app.AppLocation -ine $expected.AppLocation -or
        $app.DontInject -or $app.AutoInject -or $app.IsMsStore -or $app.AppArguments -or $app.WorkingDirectory -or
        @($app.EnabledMods).Count -ne 1 -or $app.EnabledMods[0] -ine $SparkingReloadedModId) {
        throw 'The portable game profile was changed. Restore its original settings before using this start shortcut.'
    }
    $mod = Get-ReloadedJson (Get-SafeReloadedPath $Manager "$SparkingModFolder\ModConfig.json")
    if ($mod.ModId -ine $SparkingReloadedModId -or $mod.ModNativeDll64 -ine 'SparkingZeroAccess.Native.dll' -or
        $mod.ModDll -or $mod.ModNativeDll32 -or $mod.ModR2RManagedDll64 -or $mod.ModR2RManagedDll32 -or
        @($mod.ModDependencies).Count -gt 0) { throw 'The installed native mod profile was changed.' }
}
