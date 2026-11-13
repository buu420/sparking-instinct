function Get-SharedReloadedPointer {
    $roaming = [Environment]::GetFolderPath([Environment+SpecialFolder]::ApplicationData)
    if (-not $roaming) { throw 'Windows did not return an AppData folder for the current account.' }
    return Get-SafeReloadedPath $roaming 'Reloaded-Mod-Loader-II\ReloadedII.json'
}

function Complete-PreviousReloadedSession([string]$Manager,[string]$GameBin) {
    $session = Get-SafeReloadedPath $Manager $SparkingSessionPath
    if (-not [IO.File]::Exists($session)) { return }
    $saved = Get-ReloadedJson $session
    if ($saved.schema -ne 1 -or $saved.manager -ine $Manager) {
        throw 'The previous launch receipt could not be verified. Preserve it for manual review.'
    }
    $pointer = Get-SharedReloadedPointer
    $steamMarker = Get-SafeReloadedPath $GameBin 'steam_appid.txt'
    Assert-SparkingGameClosed
    Restore-OwnedManagerPointer $pointer $Manager $saved.pointer
    Restore-OwnedSteamMarker $steamMarker $saved.steamMarker
    [IO.File]::Delete($session)
}
