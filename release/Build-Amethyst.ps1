param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$GameDefinition,
    [Parameter(Mandatory=$true)][string]$StageDirectory,
    [Parameter(Mandatory=$true)][string]$ArchivePath
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PackageDirectory 'Reloaded-InstallCommon.ps1')
. (Join-Path $PackageDirectory 'Reloaded-Payload.ps1')

if (Test-Path -LiteralPath $StageDirectory) { throw 'Choose a new Amethyst staging directory.' }
if (Test-Path -LiteralPath $ArchivePath) { throw 'Choose a new Amethyst ZIP path.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$game = Get-ReloadedJson $GameDefinition
$preInstall = $game.defaultPreInstall
$knownScript = 'setup/Prepare-AmethystInstall.ps1'
if ($game.gameId -ne 'dragon-ball-sparking-zero' -or $preInstall.executable -cne $knownScript) {
    throw 'The Amethyst game definition must use the verified Sparking Instinct pre-install hook.'
}
$StageDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($StageDirectory)
$ArchivePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ArchivePath)
if ($ArchivePath.StartsWith($StageDirectory.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Place the Amethyst ZIP outside the staging directory.'
}
$source = Get-SafeReloadedPath $PackageDirectory 'Payload'
$package = Read-ReloadedPackageFiles $source (Join-Path $PackageDirectory 'Payload.json')
$files = Join-Path $StageDirectory 'files'
$manager = Get-SafeReloadedPath $files $SparkingReloadedFolder
$win64 = Get-SafeReloadedPath $files 'SparkingZERO\Binaries\Win64'
$setup = Get-SafeReloadedPath $StageDirectory 'setup'
[IO.Directory]::CreateDirectory($setup) | Out-Null
foreach ($item in @($package.files)) {
    # Amethyst owns removal of every file installed through its manifest.
    if ($item.path -iin @('Uninstall.cmd','Uninstall-Reloaded.ps1','Reloaded-InstallCommon.ps1',
        'Reloaded-LaunchSession.ps1','Reloaded-Legacy.ps1','Reloaded-Payload.ps1','Game-Install-Location.ps1')) { continue }
    Copy-ReloadedPackageFile (Get-SafeReloadedPath $source $item.path) (Get-SafeReloadedPath $manager $item.path) $item.sha256
}
Copy-ReloadedPackageFile (Get-SafeReloadedPath $source $package.autoStart.path) (Get-SafeReloadedPath $win64 'dsound.dll') $package.autoStart.sha256
$startupReceipt = [ordered]@{ installOwner='Amethyst'; modId=$SparkingReloadedModId; version=$SparkingPackageVersion }
Set-ReloadedJson (Get-SafeReloadedPath $manager $SparkingReceiptPath) $startupReceipt
$ammReadme = @'
Sparking Instinct 0.1.2 beta for DRAGON BALL: Sparking! ZERO.

This copy is installed and removed by Amethyst. To play, launch the game as
usual from Steam or the Steam desktop shortcut. Use Amethyst to uninstall.

F6 reads the screen. F7 and F8 review the previous and next item. F9 toggles
automatic subtitle speech. See the release page for controls and coverage.

Original Sparking Zero Access reader by Jessica Tegner and contributors.
The Reloaded II and Prism port is maintained by Buu420.
'@
[IO.File]::WriteAllText((Get-SafeReloadedPath $manager 'README.txt'),$ammReadme,$SparkingUtf8)
foreach ($name in @('Reloaded-InstallCommon.ps1','Reloaded-Payload.ps1','Reloaded-Legacy.ps1','Prepare-Reader.ps1','ReaderPlan.json')) {
    [IO.File]::Copy((Get-SafeReloadedPath $PackageDirectory $name),(Get-SafeReloadedPath $setup $name))
}
[IO.File]::Copy((Join-Path $PSScriptRoot 'Prepare-AmethystInstall.ps1'),(Get-SafeReloadedPath $StageDirectory $knownScript))

$verification = @(
    [ordered]@{ type='hashEquals'; path='SparkingZERO/Binaries/Win64/dsound.dll'; sha256=$package.autoStart.sha256 }
)
foreach ($item in (Get-ReloadedJson (Join-Path $setup 'ReaderPlan.json')).files) {
    $verification += [ordered]@{
        type='hashEquals'; path="$SparkingReloadedFolder/$($SparkingModFolder.Replace('\','/'))/Scripts/$($item.path)"
        sha256=$item.patched
    }
}
$manifest = [ordered]@{
    gameId=$game.gameId; pluginId='buu420'; modVersion=$SparkingPackageVersion; targetPlatform='windows'
    preInstall=$preInstall; dependencies=@($game.dependencies); verify=$verification
    installActions=@(
        [ordered]@{ type='copyFolder'; sourceDir=$SparkingReloadedFolder; targetDir=$SparkingReloadedFolder },
        [ordered]@{ type='copyFile'; source='SparkingZERO/Binaries/Win64/dsound.dll'; target='SparkingZERO/Binaries/Win64/dsound.dll' }
    )
}
Set-ReloadedJson (Join-Path $StageDirectory 'manifest.json') $manifest
[IO.Compression.ZipFile]::CreateFromDirectory($StageDirectory,$ArchivePath,[IO.Compression.CompressionLevel]::Optimal,$false)
Write-Output ('Amethyst ZIP SHA-256: ' + (Get-ReloadedHash $ArchivePath))
