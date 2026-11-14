param(
    [Parameter(Mandatory=$true)][string]$gameFolder,
    [Parameter(Mandatory=$true)][string]$modFolder
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Reloaded-InstallCommon.ps1')
. (Join-Path $PSScriptRoot 'Reloaded-Payload.ps1')
. (Join-Path $PSScriptRoot 'Reloaded-Legacy.ps1')

function Install-AmethystDesktopRuntime {
    $url = 'https://builds.dotnet.microsoft.com/dotnet/WindowsDesktop/9.0.23/windowsdesktop-runtime-9.0.23-win-x64.exe'
    $sha256 = 'EEFB60BD0F3FB33C0787074CEBC0467FFBA32BCB01088613121649FC3BDB4A27'
    $fileName = 'SparkingInstinct-dotnet-' + [guid]::NewGuid().ToString('N') + '.exe'
    $installer = Join-Path ([IO.Path]::GetTempPath()) $fileName
    $ProgressPreference = 'SilentlyContinue'
    try {
        Write-Output 'Downloading the x64 Microsoft .NET 9 Desktop Runtime.'
        Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing -TimeoutSec 180
        if ((Get-ReloadedHash $installer) -ne $sha256) {
            throw 'The Microsoft .NET Desktop Runtime download failed its SHA-256 check.'
        }
        if (Test-ReloadedDesktopRuntime) { return }

        Write-Output 'Approve the Windows prompt to install the x64 Microsoft .NET 9 Desktop Runtime.'
        $process = Start-Process -FilePath $installer -ArgumentList @('/install','/quiet','/norestart') -Verb RunAs -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -eq 3010) {
            throw 'The Microsoft .NET Desktop Runtime needs a Windows restart. Restart Windows and retry the mod installation.'
        }
        if ($process.ExitCode -ne 0) {
            throw "The Microsoft .NET Desktop Runtime installer exited with code $($process.ExitCode)."
        }
        Assert-ReloadedDesktopRuntime
    } finally {
        try { [IO.File]::Delete($installer) } catch { Write-Warning 'Could not remove the temporary .NET Runtime installer.' }
    }
}

Assert-SparkingGameClosed
$gameRoot = (Resolve-Path -LiteralPath $gameFolder).ProviderPath
$exe = Assert-SparkingExecutable $gameRoot
$gameBin = [IO.Path]::GetDirectoryName($exe)
$manager = Get-SafeReloadedPath $gameRoot $SparkingReloadedFolder
if (Test-Path -LiteralPath (Get-SafeReloadedPath $manager $SparkingReceiptPath)) {
    throw 'A standalone Sparking Instinct or Reloaded preview is already installed. Remove it with its Uninstall.cmd, or use the new standalone ZIP to remove it, before installing through Amethyst.'
}
Assert-LegacyBootstrapSuspended $gameBin

$stagedFiles = Get-SafeReloadedPath $modFolder 'files'
$stagedManager = Get-SafeReloadedPath $stagedFiles $SparkingReloadedFolder
foreach ($root in @($gameBin,$manager)) {
    $stagedRoot = if ($root -eq $gameBin) {
        Get-SafeReloadedPath $stagedFiles 'SparkingZERO\Binaries\Win64'
    } else { $stagedManager }
    foreach ($item in (Get-ChildItem -LiteralPath $stagedRoot -File -Recurse -Force)) {
        $relative = $item.FullName.Substring($stagedRoot.Length + 1)
        if (Test-Path -LiteralPath (Get-SafeReloadedPath $root $relative)) {
            throw "A file or directory already occupies the Sparking Instinct installation path. $relative"
        }
    }
}

Assert-SparkingGameClosed
# Amethyst's registry check cannot require a specific .NET major version and minimum patch.
if (-not (Test-ReloadedDesktopRuntime)) { Install-AmethystDesktopRuntime }
Assert-SparkingGameClosed
