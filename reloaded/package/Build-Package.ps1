param(
    [Parameter(Mandatory=$true)][string]$NativeDll,
    [Parameter(Mandatory=$true)][string]$PrismDll,
    [Parameter(Mandatory=$true)][string]$AutoStartDll,
    [Parameter(Mandatory=$true)][string]$LocalLoaderDirectory,
    [Parameter(Mandatory=$true)][string]$LocalLoaderSourceArchive,
    [Parameter(Mandatory=$true)][string]$DotnetHostNoticesDirectory,
    [Parameter(Mandatory=$true)][string]$ReloadedMemorySourceArchive,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$ArchivePath
)
$ErrorActionPreference = 'Stop'
. ([IO.Path]::Combine($PSScriptRoot,'Reloaded-InstallCommon.ps1'))
. (Join-Path $PSScriptRoot 'Reloaded-Payload.ps1')
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$prismLicenses = Join-Path $project 'reloaded\speech\licenses'
if ((Get-ReloadedHash $PrismDll) -ne $SparkingPrismHash) { throw 'Prism DLL does not match the pinned x64 release.' }

function Assert-X64Pe([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) { throw "Invalid PE file. $Path" }
        $stream.Position = 60
        $offset = $reader.ReadUInt32()
        if ($offset -gt $stream.Length - 6) { throw "Invalid PE header. $Path" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw "The payload must be x64. $Path" }
    } finally { $reader.Dispose() }
}
foreach ($dll in @($NativeDll,$PrismDll,$AutoStartDll)) { Assert-X64Pe $dll }
if ((Get-ReloadedHash $AutoStartDll) -eq $SparkingLegacyStartupHash) {
    throw 'The automatic startup DLL matches the retired Candidate 22 proxy.'
}
$OutputDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new package output directory.' }
if ($ArchivePath) {
    $ArchivePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ArchivePath)
    if (Test-Path -LiteralPath $ArchivePath) { throw 'Choose a new archive file.' }
    if ($ArchivePath.StartsWith($OutputDirectory.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Place the output ZIP beside the package directory.'
    }
}
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$payload = Join-Path $OutputDirectory 'Payload'
[IO.Directory]::CreateDirectory($payload) | Out-Null
$loaderSource = (Resolve-Path -LiteralPath $LocalLoaderDirectory).ProviderPath
$loaderTarget = Get-SafeReloadedPath $payload $SparkingLoaderFolder
foreach ($file in $SparkingLocalLoaderFiles) {
    if (-not [IO.File]::Exists((Get-SafeReloadedPath $loaderSource $file))) {
        throw "The app-local loader input is incomplete. $file"
    }
}
foreach ($file in @(Get-ChildItem -LiteralPath $loaderSource -Recurse -File -Force)) {
    $relative = $file.FullName.Substring($loaderSource.TrimEnd('\').Length + 1)
    if ($file.Extension -in @('.pdb','.dmp','.log','.cmd','.ps1') -or $file.Name -ieq 'steam_appid.txt') {
        throw "Unexpected private or executable script file in app-local loader input. $relative"
    }
    $source = Get-SafeReloadedPath $loaderSource $relative
    $destination = Get-SafeReloadedPath $loaderTarget $relative
    Copy-ReloadedPackageFile $source $destination (Get-ReloadedHash $source)
}
$autoStart = Get-SafeReloadedPath $payload $SparkingAutoStartSource
Copy-ReloadedPackageFile (Resolve-Path -LiteralPath $AutoStartDll).ProviderPath $autoStart (Get-ReloadedHash $AutoStartDll)
$mod = Get-SafeReloadedPath $payload $SparkingModFolder
[IO.Directory]::CreateDirectory($mod) | Out-Null
[IO.File]::Copy((Resolve-Path -LiteralPath $NativeDll).ProviderPath,(Join-Path $mod 'SparkingZeroAccess.Native.dll'))
[IO.File]::Copy((Resolve-Path -LiteralPath $PrismDll).ProviderPath,(Join-Path $mod 'prism.dll'))
$narration = Get-SafeReloadedPath $project 'reloaded\narration'
$audio = Get-SafeReloadedPath $mod 'AudioDescription'
$cueSheet = Get-SafeReloadedPath $narration 'cue-sheet.json'
$description = Get-ReloadedJson $cueSheet
$clips = @{}
foreach ($film in $description.films) {
    foreach ($cue in $film.cues) {
        if ($cue.file -cnotmatch '^AudioDescription/op[23]-[0-9]{2}\.wav$' -or
            $cue.sha256 -cnotmatch '^[0-9a-fA-F]{64}$') {
            throw 'The movie description cue sheet has an invalid clip entry.'
        }
        $name = $cue.file.Substring('AudioDescription/'.Length)
        if ($clips.ContainsKey($name)) { throw "The movie description lists $name twice." }
        $clips[$name] = $cue.sha256
    }
}
if (Compare-Object @($SparkingNarrationFiles | Where-Object { $_ -like '*.wav' }) @($clips.Keys)) {
    throw 'The movie description cue sheet does not match the bundled clips.'
}
Copy-ReloadedPackageFile $cueSheet (Get-SafeReloadedPath $audio 'cue-sheet.json') (Get-ReloadedHash $cueSheet)
foreach ($clip in $SparkingNarrationFiles | Where-Object { $_ -like '*.wav' }) {
    $source = Get-SafeReloadedPath $narration $clip
    Copy-ReloadedPackageFile $source (Get-SafeReloadedPath $audio $clip) $clips[$clip]
}
[IO.File]::Copy((Join-Path $PSScriptRoot 'ModConfig.json'),(Join-Path $mod 'ModConfig.json'))
[IO.File]::WriteAllText((Join-Path $payload 'portable.txt'),'')
Set-ReloadedJson (Get-SafeReloadedPath $payload $SparkingAppConfigPath) (Get-ReloadedAppConfig)
$common = @('Reloaded-InstallCommon.ps1','Reloaded-Legacy.ps1','Reloaded-Payload.ps1','Reloaded-LaunchSession.ps1',
    'Uninstall-Reloaded.ps1','Uninstall.cmd','README.txt')
foreach ($name in $common) {
    [IO.File]::Copy((Join-Path $PSScriptRoot $name),(Join-Path $payload $name))
}
foreach ($name in $common + @('Install-Reloaded.ps1','Install.cmd')) {
    [IO.File]::Copy((Join-Path $PSScriptRoot $name),(Join-Path $OutputDirectory $name))
}
$discovery = Join-Path $project 'helpers\Game-Install-Location.ps1'
[IO.File]::Copy($discovery,(Join-Path $OutputDirectory 'Game-Install-Location.ps1'))
[IO.File]::Copy($discovery,(Join-Path $payload 'Game-Install-Location.ps1'))
$notices = Join-Path $payload 'LICENSES'
[IO.Directory]::CreateDirectory($notices) | Out-Null
foreach ($name in @('THIRD-PARTY-NOTICES.md','Reloaded-II-GPL-3.0.txt','MinHook-LICENSE.txt')) {
    [IO.File]::Copy((Join-Path $PSScriptRoot $name),(Join-Path $notices $name))
}
$loaderSourceZip = (Resolve-Path -LiteralPath $LocalLoaderSourceArchive).ProviderPath
$loaderZip = [IO.Compression.ZipFile]::OpenRead($loaderSourceZip)
try {
    if (-not $loaderZip.Entries.Count) { throw 'The corresponding app-local loader source ZIP is empty.' }
} finally { $loaderZip.Dispose() }
[IO.File]::Copy($loaderSourceZip,(Join-Path $notices 'Reloaded-LocalLoader-Source.zip'))
$memorySource = (Resolve-Path -LiteralPath $ReloadedMemorySourceArchive).ProviderPath
[IO.File]::Copy($memorySource,(Join-Path $notices 'Reloaded.Memory-9.4.3-Source.zip'))
$dotnetSource = (Resolve-Path -LiteralPath $DotnetHostNoticesDirectory).ProviderPath
$dotnetNotices = Get-SafeReloadedPath $notices 'DotNet'
[IO.Directory]::CreateDirectory($dotnetNotices) | Out-Null
foreach ($name in @('LICENSE.TXT','THIRD-PARTY-NOTICES.TXT')) {
    [IO.File]::Copy((Get-SafeReloadedPath $dotnetSource $name),(Join-Path $dotnetNotices $name))
}
$prismDest = Join-Path $notices 'Prism'
[IO.Directory]::CreateDirectory($prismDest) | Out-Null
Copy-Item -LiteralPath (Join-Path $prismLicenses 'Prism-NOTICE.txt'),(Join-Path $prismLicenses 'LICENSES') -Destination $prismDest -Recurse
$files = @(Get-ChildItem -LiteralPath $payload -File -Recurse -Force | Sort-Object FullName | Where-Object { $_.FullName -ine $autoStart } | ForEach-Object {
    [ordered]@{
        path=$_.FullName.Substring($payload.Length + 1).Replace('\','/')
        sha256=(Get-ReloadedHash $_.FullName)
    }
})
$manifest = Join-Path $OutputDirectory 'Payload.json'
Set-ReloadedJson $manifest ([ordered]@{
    schema=2; modId=$SparkingReloadedModId; version=$SparkingPackageVersion; files=$files
    autoStart=[ordered]@{ path=$SparkingAutoStartSource; sha256=(Get-ReloadedHash $autoStart) }
})
Read-ReloadedPackageFiles $payload $manifest | Out-Null
if ($ArchivePath) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($ArchivePath)) | Out-Null
    [IO.Compression.ZipFile]::CreateFromDirectory($OutputDirectory,$ArchivePath,[IO.Compression.CompressionLevel]::Optimal,$false)
    $zip = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $archiveEntries = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $zip.Entries) {
            if (-not $entry.Name) { continue }
            if (-not $archiveEntries.Add($entry.FullName)) { throw 'Duplicate file in the completed ZIP.' }
            $expectedFile = Get-SafeReloadedPath $OutputDirectory $entry.FullName
            $data = $entry.Open()
            try { $actualHash = [BitConverter]::ToString($hasher.ComputeHash($data)).Replace('-','') }
            finally { $data.Dispose() }
            if ((Get-ReloadedHash $expectedFile) -ne $actualHash) { throw "Completed ZIP verification failed. $($entry.FullName)" }
        }
        if ($archiveEntries.Count -ne @(Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File -Force).Count) {
            throw 'The completed ZIP is missing a staged file.'
        }
    } finally {
        $hasher.Dispose()
        $zip.Dispose()
    }
    Write-Output ('Package ZIP: ' + $ArchivePath)
    Write-Output ('Package SHA-256: ' + (Get-ReloadedHash $ArchivePath))
}
Write-Output ("Verified {0} payload files. The native reader and movie narration are bundled." -f $files.Count)
