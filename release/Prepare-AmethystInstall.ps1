param(
    [Parameter(Mandatory=$true)][string]$gameFolder,
    [Parameter(Mandatory=$true)][string]$modFolder
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Reloaded-InstallCommon.ps1')
. (Join-Path $PSScriptRoot 'Reloaded-Payload.ps1')
. (Join-Path $PSScriptRoot 'Reloaded-Legacy.ps1')

Assert-SparkingGameClosed
$gameRoot = (Resolve-Path -LiteralPath $gameFolder).ProviderPath
$exe = Assert-SparkingExecutable $gameRoot
$gameBin = [IO.Path]::GetDirectoryName($exe)
Assert-ReloadedDesktopRuntime
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
