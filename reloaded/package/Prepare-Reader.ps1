param(
    [Parameter(Mandatory=$true)][string]$PlanPath,
    [Parameter(Mandatory=$true)][string]$Destination,
    [string]$UpstreamArchive
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$utf8 = [Text.UTF8Encoding]::new($false,$true)
$commit = '527125583d5d7ef36890c1c006ed187923e7e923'
$archiveHash = '81320E984AB4F1BBDCB301D296DA829A2FE0705EA7DD99221EAB848331FAA21F'
$plan = Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($plan.commit -cne $commit -or @($plan.files).Count -ne 62) {
    throw 'The Sparking Instinct reader plan is invalid.'
}

function Hash-ReaderBytes([byte[]]$Bytes) {
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($Bytes)).Replace('-','') }
    finally { $hash.Dispose() }
}

function Read-ReaderEntry($Zip,[string]$Path) {
    $entry = $Zip.GetEntry("SparkingZeroAccess-$commit/SparkingZeroAccess/$Path")
    if ($null -eq $entry -or $entry.Length -le 0 -or $entry.Length -gt 4194304) {
        throw "AccessForge archive entry is missing or unexpected. $Path"
    }
    $inputStream = $entry.Open()
    $buffer = [IO.MemoryStream]::new()
    try { $inputStream.CopyTo($buffer); return ,$buffer.ToArray() }
    finally { $inputStream.Dispose(); $buffer.Dispose() }
}

function Get-ReaderLines([byte[]]$Bytes) {
    $source = $utf8.GetString($Bytes).Replace("`r`n","`n")
    if (-not $source.EndsWith("`n") -or $source.Contains("`r")) {
        throw 'Unexpected line endings in the upstream reader.'
    }
    return ,$source.Substring(0,$source.Length-1).Split([char]10)
}

$temporaryArchive = $null
try {
    if (-not $UpstreamArchive) {
        $temporaryArchive = Join-Path ([IO.Path]::GetTempPath()) ('SparkingReader-' + [guid]::NewGuid().ToString('N') + '.zip')
        $UpstreamArchive = $temporaryArchive
        Write-Output 'Downloading Sparking Zero Access by Jessica Tegner from AccessForge.'
        try {
            Invoke-WebRequest -UseBasicParsing -Uri "https://codeload.github.com/AccessForge/SparkingZeroAccess/zip/$commit" `
                -OutFile $UpstreamArchive -TimeoutSec 90
        } catch {
            throw "Could not download Sparking Zero Access from GitHub. Check your connection and retry. $($_.Exception.Message)"
        }
    }
    if ((Get-FileHash -LiteralPath $UpstreamArchive -Algorithm SHA256).Hash -cne $archiveHash) {
        throw 'The AccessForge archive differs from the verified source. Setup stopped.'
    }
    $archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $UpstreamArchive).ProviderPath)
    $original = @{}
    $lines = @{}
    $ready = @{}
    try {
        foreach ($file in $plan.files) {
            $relative = $file.path
            if ($relative -cnotmatch '^(?:[a-z_]+\.lua|AudioDescription/(?:op[23]-[0-9]{2}\.wav|cue-sheet\.json))$' -or
                $original.ContainsKey($relative)) {
                throw 'The reader plan contains an unexpected file.'
            }
            [byte[]]$original[$relative] = Read-ReaderEntry $archive $relative
            if ((Hash-ReaderBytes $original[$relative]) -cne $file.original) {
                throw "The AccessForge reader did not match the expected source. $relative"
            }
            if ($relative -match '\.(lua|json)$') { $lines[$relative] = Get-ReaderLines $original[$relative] }
        }
        foreach ($file in $plan.files) {
            $relative = $file.path
            [byte[]]$result = $original[$relative]
            if ($file.PSObject.Properties['edits']) {
                if (-not $lines.ContainsKey($relative)) { throw 'Only reader text files can have edits.' }
                $base = $lines[$relative]
                $output = [Collections.Generic.List[string]]::new()
                $cursor = 0
                foreach ($edit in $file.edits) {
                    if ($edit.at -lt $cursor -or $edit.remove -lt 0 -or $edit.at + $edit.remove -gt $base.Count) {
                        throw "Invalid reader edit. $relative"
                    }
                    while ($cursor -lt $edit.at) { $output.Add($base[$cursor]); $cursor++ }
                    foreach ($addition in $edit.add) {
                        if ($addition -is [string]) {
                            if ($addition.Contains("`n") -or $addition.Contains("`r")) { throw 'Invalid reader edit line.' }
                            $output.Add($addition)
                        } else {
                            $other = $lines[$addition.from]
                            if ($null -eq $other -or $addition.line -lt 0 -or $addition.line -ge $other.Count) {
                                throw 'Invalid reference to an upstream reader line.'
                            }
                            $line = $other[$addition.line]
                            if ($addition.indent -is [string]) { $line = $addition.indent + $line.TrimStart() }
                            $output.Add($line)
                        }
                    }
                    $cursor += $edit.remove
                }
                while ($cursor -lt $base.Count) { $output.Add($base[$cursor]); $cursor++ }
                $crlf = [Collections.Generic.HashSet[int]]::new()
                foreach ($range in $file.crlf) {
                    if ($range.start -lt 0 -or $range.count -le 0 -or $range.start + $range.count -gt $output.Count) {
                        throw 'Invalid reader line endings.'
                    }
                    for ($i = $range.start; $i -lt $range.start + $range.count; $i++) { $null = $crlf.Add($i) }
                }
                $builder = [Text.StringBuilder]::new()
                for ($i = 0; $i -lt $output.Count; $i++) {
                    $null = $builder.Append($output[$i])
                    if ($crlf.Contains($i)) { $null = $builder.Append("`r") }
                    $null = $builder.Append("`n")
                }
                $result = $utf8.GetBytes($builder.ToString())
            }
            if ((Hash-ReaderBytes $result) -cne $file.patched) {
                throw "The reconstructed reader did not match the tested build. $relative"
            }
            $ready[$relative] = $result
        }
    } finally { $archive.Dispose() }
    foreach ($file in $plan.files) {
        $target = Get-SafeReloadedPath $Destination $file.path
        Set-ReloadedBytes $target $ready[$file.path]
    }
    Write-Output 'Verified 24 reader modules and 37 narration clips from the AccessForge source.'
} finally {
    if ($temporaryArchive -and [IO.File]::Exists($temporaryArchive)) { [IO.File]::Delete($temporaryArchive) }
}
