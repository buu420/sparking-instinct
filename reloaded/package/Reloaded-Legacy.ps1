# Only the installed legacy copy is suspended. The rollback receipt is outside Mods.
$SparkingLegacyFiles = [ordered]@{
    'dwmapi.dll' = 'CF440B9EB8643BB7C434ACFDA696AEE57FD981D185DCA5E57FB8DBB18F8FC1CD'
    'dsound.dll' = $SparkingLegacyStartupHash
    'plugins\DBSparkingZeroUTOCBypass.asi' = 'A851CC9E4E49DE3C5BC9A3A4487F62FD37183D44F40708093C422A68FE988F63'
}
$SparkingLegacyUe4ss = '680A026890ABB4D0DF2211251F8DEFC1681A584275F1521DCC0FE30AF480006F'
$SparkingLegacySettings = '63D72989AC5E34C904F17D8AD585F3F9F243FD270E098E99543532B2BF1E66B3'
$SparkingLegacyMain = 'FB3B749EBF1D55BC395259642E859A7F83447088E4AD125B42CCF52C681DD9AB'

function Get-LegacySuspendPlan([string]$GameBin) {
    $registry = 'ue4ss\Mods\mods.txt'
    $modsRoot = Get-SafeReloadedPath $GameBin 'ue4ss\Mods'
    $modsPath = Get-SafeReloadedPath $GameBin $registry
    $legacy = [ordered]@{ registry=$null; moved=@() }
    $dwm = Get-SafeReloadedPath $GameBin 'dwmapi.dll'
    $dsound = Get-SafeReloadedPath $GameBin 'dsound.dll'
    $bypass = Get-SafeReloadedPath $GameBin 'plugins\DBSparkingZeroUTOCBypass.asi'
    if ([IO.File]::Exists((Get-SafeReloadedPath $GameBin 'UE4SS.dll'))) {
        throw 'A separate root-level UE4SS install is present. Use a game session with its loader disabled.'
    }
    if (-not [IO.File]::Exists($dwm) -and -not [IO.File]::Exists($dsound) -and
        -not [IO.File]::Exists($bypass) -and -not [IO.File]::Exists($modsPath)) { return $legacy }

    $settings = Get-SafeReloadedPath $GameBin 'ue4ss\UE4SS-settings.ini'
    $ue4ss = Get-SafeReloadedPath $GameBin 'ue4ss\UE4SS.dll'
    $main = Get-SafeReloadedPath $GameBin 'ue4ss\Mods\SparkingZeroAccess\Scripts\main.lua'
    $known = [IO.File]::Exists($main) -and (Get-ReloadedHash $main) -eq $SparkingLegacyMain
    if ([IO.File]::Exists($dwm)) {
        if (-not $known -or -not [IO.File]::Exists($settings) -or (Get-ReloadedHash $settings) -ne $SparkingLegacySettings -or
            -not [IO.File]::Exists($ue4ss) -or (Get-ReloadedHash $ue4ss) -ne $SparkingLegacyUe4ss -or
            (Get-ReloadedHash $dwm) -ne $SparkingLegacyFiles['dwmapi.dll']) {
            throw 'The existing dwmapi.dll or UE4SS installation differs from the verified legacy repair. Leave it intact and use a Reloaded-only session.'
        }
        $otherMods = @(Get-ChildItem -LiteralPath $modsRoot -Directory -Force | Where-Object { $_.Name -ine 'SparkingZeroAccess' })
        $rootMods = Get-SafeReloadedPath $GameBin 'Mods'
        $ue4ssOverrides = [Environment]::GetEnvironmentVariable('UE4SS_MODS_PATHS')
        if ($otherMods.Count -or [IO.Directory]::Exists($rootMods) -or $ue4ssOverrides) {
            throw 'Other UE4SS mods or overrides are present. Leave their loader intact and use a Reloaded-only session.'
        }
    }
    if ([IO.File]::Exists($bypass) -or [IO.File]::Exists($dsound)) {
        $plugins = Get-SafeReloadedPath $GameBin 'plugins'
        $otherAsi = @(Get-ChildItem -LiteralPath $GameBin -File -Filter '*.asi' -Force)
        if ([IO.Directory]::Exists($plugins)) {
            $otherAsi += @(Get-ChildItem -LiteralPath $plugins -Force | Where-Object { $_.Name -ine 'DBSparkingZeroUTOCBypass.asi' })
        }
        $otherPaks = Get-SafeReloadedPath (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $GameBin))) 'SparkingZERO\Content\Paks\~mods'
        if (-not $known -or $otherAsi.Count -or [IO.Directory]::Exists($otherPaks)) {
            throw 'A shared ASI loader or other game mods are present. Keep those files and use a Reloaded-only session.'
        }
    }

    foreach ($relative in $SparkingLegacyFiles.Keys) {
        $path = Get-SafeReloadedPath $GameBin $relative
        if (-not [IO.File]::Exists($path)) { continue }
        $hash = Get-ReloadedHash $path
        if ($hash -ne $SparkingLegacyFiles[$relative]) { throw "Legacy file is not owned by the verified repair. $relative" }
        $legacy.moved += [ordered]@{ path=$relative; sha256=$hash }
    }

    if ([IO.File]::Exists($modsPath) -and $known) {
        $before = Get-ReloadedSnapshot $modsPath
        [byte[]]$original = [Convert]::FromBase64String($before)
        [byte[]]$after = Get-DisabledLegacyRegistration $original
        $afterString = [Convert]::ToBase64String($after)
        if ($before -cne $afterString) {
            $line = [regex]::Matches((Get-LegacyRegistryText $original).Text,'(?im)^[ \t]*SparkingZeroAccess[ \t]*:[ \t]*1[ \t]*(?=\r?$)')
            if ($line.Count -ne 1) { throw 'The legacy mod has multiple active registration lines. Leave the registry intact and use a Reloaded-only session.' }
            $legacy.registry = [ordered]@{
                before=$before; after=$afterString; path=$registry
                originalLine=$line[0].Value
                disabledLine=[regex]::Replace($line[0].Value,'1(?=[ \t]*$)','0')
            }
        }
    }
    return $legacy
}

function Suspend-LegacyRepair([string]$GameBin,[string]$Backup,$Legacy) {
    foreach ($item in @($Legacy.moved)) {
        $source = Get-SafeReloadedPath $GameBin $item.path
        $backupFile = Get-SafeReloadedPath $Backup $item.path
        if ((Get-ReloadedHash $source) -ne $item.sha256) { throw "Legacy file changed during installation. $($item.path)" }
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backupFile)) | Out-Null
        [IO.File]::Move($source,$backupFile)
    }
    if ($Legacy.registry) {
        $path = Get-SafeReloadedPath $GameBin 'ue4ss\Mods\mods.txt'
        if ((Get-ReloadedSnapshot $path) -cne $Legacy.registry.before) { throw 'The legacy mod registry changed during installation.' }
        Set-ReloadedBytes $path ([Convert]::FromBase64String($Legacy.registry.after))
    }
}

function Restore-LegacyRepair([string]$GameBin,[string]$Backup,$Legacy) {
    $complete = $true
    $pending = [Collections.Generic.List[object]]::new()
    foreach ($item in @($Legacy.moved)) {
        if (-not $item) { continue }
        if ($item.path -notin @($SparkingLegacyFiles.Keys) -or $item.sha256 -ne $SparkingLegacyFiles[$item.path]) {
            throw 'The legacy restoration receipt lists an unexpected file.'
        }
        $path = Get-SafeReloadedPath $GameBin $item.path
        $backupFile = Get-SafeReloadedPath $Backup $item.path
        if ([IO.File]::Exists($path) -and (Get-ReloadedHash $path) -eq $item.sha256) { continue }
        if ((Test-Path -LiteralPath $path) -or -not [IO.File]::Exists($backupFile) -or
            (Get-ReloadedHash $backupFile) -ne $item.sha256) {
            Write-Warning "Legacy restore needs manual review. $($item.path)"
            return $false
        }
        $pending.Add([pscustomobject]@{ Destination=$path; Source=$backupFile })
    }
    foreach ($item in $pending) {
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($item.Destination)) | Out-Null
        [IO.File]::Copy($item.Source,$item.Destination)
    }
    if ($Legacy.registry) {
        $path = Get-SafeReloadedPath $GameBin 'ue4ss\Mods\mods.txt'
        $current = Get-ReloadedSnapshot $path
        if ($current -ceq $Legacy.registry.before) { return $complete }
        if ($current -ceq $Legacy.registry.after) { Set-ReloadedBytes $path ([Convert]::FromBase64String($Legacy.registry.before)) }
        elseif ($null -ne $current) {
            $recovered = Restore-LegacyRegistration ([Convert]::FromBase64String($current)) $Legacy.registry.disabledLine $Legacy.registry.originalLine
            if ($null -ne $recovered) { Set-ReloadedBytes $path $recovered }
            else {
                Write-Warning 'The legacy mod registry entry was edited independently and was preserved.'
                $complete = $false
            }
        } else {
            Write-Warning 'The original mod registry was removed independently. Its original bytes are in the migration receipt.'
            $complete = $false
        }
    }
    return $complete
}

function Assert-LegacyBootstrapSuspended([string]$GameBin) {
    foreach ($path in @('dwmapi.dll','dsound.dll','plugins\DBSparkingZeroUTOCBypass.asi','UE4SS.dll')) {
        if (Test-Path -LiteralPath (Get-SafeReloadedPath $GameBin $path)) {
            throw "Close the game and disable the previous loader before using this Reloaded II profile. $path"
        }
    }
}
