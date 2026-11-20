param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$OldBuildDirectory = 'build/task7-t70/frozen-baseline',
    [string]$NewBuildDirectory = 'build/task7-t71',
    [ValidateRange(3, 10)][int]$Pairs = 3
)

$ErrorActionPreference = 'Stop'
$taskRepository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput = Join-Path ([System.IO.Path]::GetFullPath((Join-Path $taskRepository $NewBuildDirectory))) 'artifacts/paired-native'
$taskRunner = Join-Path $PSScriptRoot 'RunSclNavigationBaseline.ps1'
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
$taskStarted = [DateTime]::UtcNow.ToString('o')
$taskSummaryPath = Join-Path $taskOutput 'Comparison.json'
[System.IO.File]::WriteAllText($taskSummaryPath, (@{status='running'; startedUtc=$taskStarted; pairs=$Pairs} | ConvertTo-Json), [System.Text.UTF8Encoding]::new($false))
try {
    for ($taskPair = 1; $taskPair -le $Pairs; ++$taskPair) {
        & $taskRunner -Path $Path -BuildDirectory $OldBuildDirectory -Runs 1 -ArtifactDirectory (Join-Path $taskOutput "pair-$taskPair/old")
        & $taskRunner -Path $Path -BuildDirectory $NewBuildDirectory -Runs 1 -ArtifactDirectory (Join-Path $taskOutput "pair-$taskPair/new")
    }
} catch {
    [System.IO.File]::WriteAllText($taskSummaryPath, (@{status='failed'; startedUtc=$taskStarted; pair=$taskPair; error=$_.Exception.Message} | ConvertTo-Json), [System.Text.UTF8Encoding]::new($false))
    throw
}
function Get-TaskMedian($Values) {
    $taskSorted = @($Values | Sort-Object)
    if ($taskSorted.Count % 2) { return $taskSorted[[int][math]::Floor($taskSorted.Count / 2)] }
    return ($taskSorted[$taskSorted.Count / 2 - 1] + $taskSorted[$taskSorted.Count / 2]) / 2
}
$taskOld = @(); $taskNew = @()
for ($taskPair = 1; $taskPair -le $Pairs; ++$taskPair) {
    $taskOld += Get-Content -LiteralPath (Join-Path $taskOutput "pair-$taskPair/old/navigation-1/NavigationBaseline.json") -Raw | ConvertFrom-Json
    $taskNew += Get-Content -LiteralPath (Join-Path $taskOutput "pair-$taskPair/new/navigation-1/NavigationBaseline.json") -Raw | ConvertFrom-Json
}
foreach ($taskResult in @($taskOld) + @($taskNew)) {
    if ($taskResult.sha256 -ne $taskOld[0].sha256) { throw 'Comparison source fingerprints differ.' }
    foreach ($taskRestore in $taskResult.restore.PSObject.Properties) {
        foreach ($taskCheck in $taskRestore.Value.PSObject.Properties) {
            if ($taskCheck.Value -ne $true) { throw "Invalid restored anchor: $($taskRestore.Name)/$($taskCheck.Name)" }
        }
    }
}
$taskMetrics = [ordered]@{}
foreach ($taskMetric in @('readyMs','peakWorkingSet')) {
    $taskOldMedian = Get-TaskMedian @($taskOld | ForEach-Object { $_.$taskMetric })
    $taskNewMedian = Get-TaskMedian @($taskNew | ForEach-Object { $_.$taskMetric })
    $taskMetrics[$taskMetric] = [ordered]@{old=$taskOldMedian; new=$taskNewMedian; percent=100*($taskNewMedian/$taskOldMedian-1)}
}
foreach ($taskMetric in $taskOld[0].timings.PSObject.Properties.Name) {
    $taskOldMedian = Get-TaskMedian @($taskOld | ForEach-Object { $_.timings.$taskMetric.p95 })
    $taskNewMedian = Get-TaskMedian @($taskNew | ForEach-Object { $_.timings.$taskMetric.p95 })
    $taskMetrics[$taskMetric] = [ordered]@{unit='us'; oldP95Median=$taskOldMedian; newP95Median=$taskNewMedian;
        percent=100*($taskNewMedian/$taskOldMedian-1)}
}
$taskSummary = [ordered]@{status='complete'; startedUtc=$taskStarted; pairs=$Pairs; order='old then new in each pair';
    meaning='Qt dispatch, worker delivery and visible anchor; no physical-input/DWM timing';
    thresholdPercent=15; comparisonPassed=(@($taskMetrics.Values | Where-Object { $_.percent -gt 15 }).Count -eq 0); metrics=$taskMetrics}
[System.IO.File]::WriteAllText((Join-Path $taskOutput 'Comparison.json'), ($taskSummary | ConvertTo-Json -Depth 6), [System.Text.UTF8Encoding]::new($false))
$taskSummary | ConvertTo-Json -Depth 6
