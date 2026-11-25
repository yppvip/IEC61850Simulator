param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$ScdPath,
    [Parameter(Mandatory = $true)][string]$IcdPath,
    [ValidateRange(1, 30)][int]$Rounds = 10,
    [switch]$CaptureScreenshots,
    [switch]$EvidenceDetails,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$OutputName = 't100-local',
    [string]$BaselineDirectory
)

# Explicit local, read-only engineering acceptance. Logs/JSON/images stay in build/.
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$executable = Join-Path $build 'bin/MainWindowBaselineTest.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build MainWindowBaselineTest first.' }
$samples = @(
    @{ Label = 'Scd'; Path = (Resolve-Path -LiteralPath $ScdPath).Path; Ied = 'PL2201A' },
    @{ Label = 'Icd'; Path = (Resolve-Path -LiteralPath $IcdPath).Path; Ied = 'TEMPLATE' }
)
$outputDirectory = Join-Path $build $OutputName
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$programs = @(@{ Label = 'current'; Directory = $build; Output = $outputDirectory })
if ($BaselineDirectory) {
    $baseline = (Resolve-Path -LiteralPath $BaselineDirectory).Path
    if (-not (Test-Path -LiteralPath (Join-Path $baseline 'bin/MainWindowBaselineTest.exe'))) { throw 'Frozen baseline executable unavailable.' }
    $programs[0].Output = Join-Path $outputDirectory 'current'
    $programs += @{ Label = 'baseline'; Directory = $baseline; Output = (Join-Path $outputDirectory 'baseline') }
    foreach ($program in $programs) { New-Item -ItemType Directory -Path $program.Output -Force | Out-Null }
}
$variables = @('PATH','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','IEC61850_LOCAL_SCL',
    'IEC61850_T100_IED','IEC61850_T100_OUTPUT','IEC61850_T100_SCREENSHOT','IEC61850_T101_EVIDENCE')
$original = @{}
foreach ($name in $variables) { $original[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:PATH = (Join-Path $build 'bin') + ';C:\Windows\System32;C:\Windows'
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $build 'bin/platforms'
    $env:IEC61850_T101_EVIDENCE = if ($EvidenceDetails) { '1' } else { '' }
    foreach ($sample in $samples) { $sample.Before = (Get-FileHash -LiteralPath $sample.Path -Algorithm SHA256).Hash }
    foreach ($program in $programs) {
        $program.Executable = Join-Path $program.Directory 'bin/MainWindowBaselineTest.exe'
        $program.Before = (Get-FileHash -LiteralPath $program.Executable -Algorithm SHA256).Hash
    }
    for ($round = 1; $round -le $Rounds; $round++) {
        foreach ($sample in $samples) {
          $orderedPrograms = if ($round % 2 -eq 0) { @($programs | Sort-Object Label) } else { $programs }
          foreach ($program in $orderedPrograms) {
            $env:PATH = (Join-Path $program.Directory 'bin') + ';C:\Windows\System32;C:\Windows'
            $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $program.Directory 'bin/platforms'
            $prefix = Join-Path $program.Output ('{0}-{1:d2}' -f $sample.Label, $round)
            $env:IEC61850_LOCAL_SCL = $sample.Path
            $env:IEC61850_T100_IED = $sample.Ied
            $env:IEC61850_T100_OUTPUT = $prefix + '.json'
            $env:IEC61850_T100_SCREENSHOT = if ($CaptureScreenshots -and $round -eq 1) { $prefix } else { '' }
            & $program.Executable SclTask10Baseline -o ($prefix + '.log,txt')
            if ($LASTEXITCODE -ne 0) { throw "Local baseline failed: $prefix.log (exit $LASTEXITCODE)" }
            Write-Output ('T10 baseline {0}/{1} round {2}/{3} complete' -f $program.Label, $sample.Label, $round, $Rounds)
          }
        }
    }
    foreach ($sample in $samples) {
        $after = (Get-FileHash -LiteralPath $sample.Path -Algorithm SHA256).Hash
        if ($sample.Before -ne $after) { throw "Source fingerprint changed: $($sample.Label)" }
    }
    foreach ($program in $programs) {
        if ($program.Before -ne (Get-FileHash -LiteralPath $program.Executable -Algorithm SHA256).Hash) { throw 'Executable changed during measurement.' }
    }
    Write-Output 'Local sources unchanged. Each sample/round used a fresh native Qt process.'
}
finally {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $original[$name], 'Process') }
}
