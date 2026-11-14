# Shared package and migration helpers for Windows PowerShell 5.1.
# The parent shell can put its own built-in modules first. Resolve the
# host's own built-ins first before any package cmdlet triggers auto-loading.
$moduleRoot = [IO.Path]::Combine($PSHOME,'Modules')
$env:PSModulePath = $moduleRoot + [IO.Path]::PathSeparator + $env:PSModulePath
foreach ($moduleName in @('Microsoft.PowerShell.Management','Microsoft.PowerShell.Utility')) {
    $modulePath = [IO.Path]::Combine($moduleRoot,$moduleName,($moduleName + '.psd1'))
    if (-not (Get-Module -Name $moduleName | Where-Object Path -EQ $modulePath)) {
        Import-Module -Name $modulePath -Force -ErrorAction Stop
    }
}
$SparkingReloadedModId = 'sparkingzero.accessibility.reloaded'
$SparkingReloadedFolder = 'SparkingZeroAccess-Reloaded'
$SparkingShipping = 'SparkingZERO\Binaries\Win64\SparkingZERO-Win64-Shipping.exe'
$SparkingSupportedHash = '6DB7002A8FA66A1309B01037D8A9A8C678A8ECD35B72D4C6AFAC550A74371657'
$SparkingLegacyStartupHash = 'BB8767F918C52A2AD055D2DE9BAFFD2478598643B9894F09ABD20D1F1FFD170C'
$SparkingUtf8 = [Text.UTF8Encoding]::new($false, $true)

function Get-SafeReloadedPath([string]$Root,[string]$Relative) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\','/')
    $parts = $Relative -split '[\\/]'
    if ([IO.Path]::IsPathRooted($Relative) -or $parts.Count -eq 0) { throw 'A relative package path is required.' }
    # Reject links in the existing ancestors as well as the package contents.
    $ancestor = $rootPath
    while ($ancestor -and $ancestor -ne [IO.Path]::GetPathRoot($ancestor)) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Linked package or game path requires manual review. $ancestor"
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    $full = $rootPath
    foreach ($part in $parts) {
        if (-not $part -or $part.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
            $part.EndsWith('.') -or $part.EndsWith(' ') -or
            $part -match '^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\.|$)') { throw "Invalid package path. $Relative" }
        $full = Join-Path $full $part
        if ((Test-Path -LiteralPath $full) -and ((Get-Item -LiteralPath $full -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Linked package or game path requires manual review. $full"
        }
    }
    return $full
}

function Get-ReloadedHash([string]$Path) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $file = [IO.File]::OpenRead($Path)
        try { return [BitConverter]::ToString($hasher.ComputeHash($file)).Replace('-','') }
        finally { $file.Dispose() }
    } finally { $hasher.Dispose() }
}

function Get-ReloadedSnapshot([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Container) { throw "A directory occupies a required file path. $Path" }
    if (-not [IO.File]::Exists($Path)) { return $null }
    if ([IO.FileInfo]::new($Path).Length -gt 1048576) { throw "The configuration file is too large to back up safely. $Path" }
    return [Convert]::ToBase64String([IO.File]::ReadAllBytes($Path))
}

function Set-ReloadedBytes([string]$Path,[byte[]]$Bytes) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    $staged = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    $backup = $staged + '.replace'
    try {
        [IO.File]::WriteAllBytes($staged,$Bytes)
        if ([IO.File]::Exists($Path)) { [IO.File]::Replace($staged,$Path,$backup) }
        else { [IO.File]::Move($staged,$Path) }
    } finally {
        if ([IO.File]::Exists($staged)) { [IO.File]::Delete($staged) }
        if ([IO.File]::Exists($backup)) { [IO.File]::Delete($backup) }
    }
}

function Set-ReloadedJson([string]$Path,$Value) {
    Set-ReloadedBytes $Path ($SparkingUtf8.GetBytes(($Value | ConvertTo-Json -Depth 12)))
}

function Get-ReloadedJson([string]$Path) {
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Get-LegacyRegistryText([byte[]]$Bytes) {
    $offset = 0
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 239 -and $Bytes[1] -eq 187 -and $Bytes[2] -eq 191) { $offset = 3 }
    return [pscustomobject]@{ Text=$SparkingUtf8.GetString($Bytes,$offset,$Bytes.Length-$offset); Bom=($offset -eq 3) }
}

function ConvertTo-LegacyRegistryBytes([string]$Text,[bool]$Bom) {
    [byte[]]$bytes = $SparkingUtf8.GetBytes($Text)
    if ($Bom) { return ,([byte[]]@(239,187,191) + $bytes) }
    return ,$bytes
}

function Get-DisabledLegacyRegistration([byte[]]$Bytes) {
    $document = Get-LegacyRegistryText $Bytes
    $activePattern = '(?im)^(?<prefix>[ \t]*SparkingZeroAccess[ \t]*:[ \t]*)1(?<suffix>[ \t]*)(?=\r?$)'
    $result = [regex]::Replace($document.Text,$activePattern,'${prefix}0${suffix}')
    return ,(ConvertTo-LegacyRegistryBytes $result $document.Bom)
}

function Restore-LegacyRegistration([byte[]]$Bytes,[string]$DisabledLine,[string]$OriginalLine) {
    $document = Get-LegacyRegistryText $Bytes
    $pattern = '(?m)^' + [regex]::Escape($DisabledLine) + '(?=\r?$)'
    if ([regex]::Matches($document.Text,$pattern).Count -ne 1) { return $null }
    $replacement = [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $OriginalLine }
    return ,(ConvertTo-LegacyRegistryBytes ([regex]::Replace($document.Text,$pattern,$replacement)) $document.Bom)
}

function Test-OwnManagerPointer([string]$Path,[string]$Manager) {
    if (-not [IO.File]::Exists($Path)) { return $false }
    try {
        $config = Get-ReloadedJson $Path
        return $config.LauncherPath -ieq (Join-Path $Manager 'Reloaded-II.exe') -and
            $config.LoaderPath64 -ieq (Join-Path $Manager 'Loader\x64\Reloaded.Mod.Loader.dll')
    } catch { return $false }
}

function Restore-OwnedManagerPointer([string]$Path,[string]$Manager,$Snapshot) {
    if (-not (Test-OwnManagerPointer $Path $Manager)) { return }
    if ($null -eq $Snapshot) { [IO.File]::Delete($Path) }
    else { Set-ReloadedBytes $Path ([Convert]::FromBase64String([string]$Snapshot)) }
}

function Restore-OwnedSteamMarker([string]$Path,$Snapshot) {
    if (-not [IO.File]::Exists($Path) -or [IO.FileInfo]::new($Path).Length -ne 7 -or
        [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Path)) -cne '1790600') { return }
    if ($null -eq $Snapshot) { [IO.File]::Delete($Path) }
    else { Set-ReloadedBytes $Path ([Convert]::FromBase64String([string]$Snapshot)) }
}

function Assert-SparkingGameClosed {
    if (Get-Process -Name 'SparkingZERO','SparkingZERO-Win64-Shipping' -ErrorAction SilentlyContinue) {
        throw 'Close Sparking! ZERO before continuing.'
    }
}

function Resolve-ReloadedGameRoot([string]$GameDirectory) {
    if (-not $GameDirectory) { $GameDirectory = Resolve-SparkingZeroInstallation -Candidates @(Get-SparkingZeroInstallations) }
    if (-not (Test-SparkingZeroFolder $GameDirectory)) { throw 'Choose the Sparking! ZERO game folder.' }
    return (Resolve-Path -LiteralPath $GameDirectory).ProviderPath
}

function Assert-SparkingExecutable([string]$GameRoot) {
    $exe = Get-SafeReloadedPath $GameRoot $SparkingShipping
    if ((Get-ReloadedHash $exe) -ne $SparkingSupportedHash) {
        throw 'The installed Steam game build differs from build 24953175 supported by this preview.'
    }
    return $exe
}

function Enter-SparkingReloadedLock {
    $mutex = [Threading.Mutex]::new($false,'Local\SparkingZeroAccess.Reloaded.Package')
    try {
        try { $owned = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $owned = $true }
        if (-not $owned) { throw 'A SparkingZeroAccess install, launch or cleanup is already in progress.' }
        return $mutex
    } catch {
        $mutex.Dispose()
        throw
    }
}

function Exit-SparkingReloadedLock($Mutex) {
    if ($Mutex) { $Mutex.ReleaseMutex(); $Mutex.Dispose() }
}

function Test-ReloadedDesktopRuntime {
    $programFiles = if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }
    $dotnet = Join-Path $programFiles 'dotnet\dotnet.exe'
    if (-not [IO.File]::Exists($dotnet)) { return $false }
    $runtimes = @(& $dotnet --list-runtimes)
    if ($LASTEXITCODE -ne 0) { return $false }
    $hasCore = $false
    $hasDesktop = $false
    foreach ($line in $runtimes) {
        if ($line -match '^Microsoft\.NETCore\.App (9\.0\.\d+) ' -and [version]$Matches[1] -ge [version]'9.0.8') { $hasCore = $true }
        if ($line -match '^Microsoft\.WindowsDesktop\.App (9\.0\.\d+) ' -and [version]$Matches[1] -ge [version]'9.0.8') { $hasDesktop = $true }
    }
    return ($hasCore -and $hasDesktop)
}

function Assert-ReloadedDesktopRuntime {
    if (-not (Test-ReloadedDesktopRuntime)) {
        throw 'Install the x64 Microsoft .NET 9 Desktop Runtime 9.0.8 or later, then try again. https://dotnet.microsoft.com/download/dotnet/9.0'
    }
}
