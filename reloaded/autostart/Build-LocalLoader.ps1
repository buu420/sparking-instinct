param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$SourceArchive
)
$ErrorActionPreference = 'Stop'
$tag = '1.31.0'
$sourceHash = 'C2D6A6F7C9244ABFFD5FB7EE0837AD8F00D392B5DF906AD3C91BB1981CBC18AA'
$archiveUri = "https://codeload.github.com/Reloaded-Project/Reloaded-II/zip/refs/tags/$tag"

foreach ($dir in @($BuildDirectory,$OutputDirectory,$SourceArchive)) {
    if (Test-Path -LiteralPath $dir) { throw "Choose new build, publish, and source ZIP paths. $dir" }
}
$BuildDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
$OutputDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$SourceArchive = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SourceArchive)
[IO.Directory]::CreateDirectory($BuildDirectory) | Out-Null
$download = Join-Path $BuildDirectory "Reloaded-II-$tag-upstream.zip"
Invoke-WebRequest -UseBasicParsing -Uri $archiveUri -OutFile $download
if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $sourceHash) {
    throw 'Reloaded II source did not match the pinned upstream tag archive.'
}
Expand-Archive -LiteralPath $download -DestinationPath $BuildDirectory
$upstream = Join-Path $BuildDirectory "Reloaded-II-$tag"
$patch = Join-Path $PSScriptRoot 'reloaded-loader.patch'
& git -C $upstream apply --check $patch
if ($LASTEXITCODE -ne 0) { throw 'The local loader patch did not match the verified upstream files.' }
& git -C $upstream apply $patch
if ($LASTEXITCODE -ne 0) { throw 'The local loader patch did not apply.' }
$upstreamSource = Join-Path $upstream 'source'
[IO.File]::Copy((Join-Path $PSScriptRoot 'LocalEntryPoint.cs'),
    (Join-Path $upstreamSource 'Reloaded.Mod.Loader\LocalEntryPoint.cs'))

# Make the corresponding source archive before MSBuild creates obj/bin trees.
$corresponding = Join-Path $BuildDirectory 'corresponding-loader-source'
$correspondingSource = Join-Path $corresponding 'source'
[IO.Directory]::CreateDirectory($correspondingSource) | Out-Null
foreach ($name in @('Reloaded.Mod.Loader','Reloaded.Mod.Loader.IO','Reloaded.Mod.Interfaces',
    'Reloaded.Mod.Shared','Reloaded.Mod.Loader.IPC','Directory.Build.props','Robust.Trimming.targets',
    'NuGet.Config','NuGet-Icon.png')) {
    Copy-Item -LiteralPath (Join-Path $upstreamSource $name) -Destination $correspondingSource -Recurse
}
$packages = Join-Path $correspondingSource 'Packages'
[IO.Directory]::CreateDirectory($packages) | Out-Null
foreach ($name in @('Colorful.Console.1.2.2-reloaded.nupkg','Indieteur.SteamAppsManAndVDFAPI.1.0.5.nupkg')) {
    [IO.File]::Copy((Join-Path $upstreamSource "Packages\$name"),(Join-Path $packages $name))
}
[IO.File]::Copy((Join-Path $upstream 'LICENSE.md'),(Join-Path $corresponding 'LICENSE.md'))
[IO.File]::Copy((Join-Path $PSScriptRoot 'BUILD-SparkingZeroAccess.txt'),
    (Join-Path $corresponding 'BUILD-SparkingZeroAccess.txt'))

if ($OutputDirectory.StartsWith($corresponding + '\',[StringComparison]::OrdinalIgnoreCase) -or
    $SourceArchive.StartsWith($corresponding + '\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Place the binary and ZIP outputs outside the corresponding source folder.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($SourceArchive)) | Out-Null
[IO.Compression.ZipFile]::CreateFromDirectory($corresponding,$SourceArchive,[IO.Compression.CompressionLevel]::Optimal,$false)

# Build the same source archive a tester can use, leaving it free of build output.
$buildSource = Join-Path $BuildDirectory 'local-loader-build-input'
[IO.Compression.ZipFile]::ExtractToDirectory($SourceArchive,$buildSource)
$loaderProject = Join-Path $buildSource 'source\Reloaded.Mod.Loader\Reloaded.Mod.Loader.csproj'
& dotnet publish $loaderProject -c Release -r win-x64 --self-contained false -o $OutputDirectory `
    -p:Platform=AnyCPU -p:PlatformTarget=x64 -p:RobustILLink=false -p:GenerateDepsJson=true `
    -p:DebugSymbols=false -p:DebugType=none "-p:PathMap=$buildSource=/src/Reloaded-II"
if ($LASTEXITCODE -ne 0) { throw 'Building the game-local Reloaded II loader failed.' }
Write-Output ('Local loader source: ' + $SourceArchive)
