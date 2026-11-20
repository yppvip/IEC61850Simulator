param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$BuildDirectory = 'build/task7-t70',
    [ValidateSet('Measure', 'Review', 'Navigation')][string]$Mode = 'Measure',
    [string]$ArtifactDirectory = '',
    [ValidateRange(1, 10)][int]$Runs = 3
)

# Explicit local, read-only native Qt probe. Never used by ordinary CTest.
$ErrorActionPreference = 'Stop'
$taskRepository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskBuild = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    [System.IO.Path]::GetFullPath($BuildDirectory)
} else { [System.IO.Path]::GetFullPath((Join-Path $taskRepository $BuildDirectory)) }
$taskSample = (Get-Item -LiteralPath $Path).FullName
$taskExecutable = Join-Path $taskBuild 'bin/MainWindowBaselineTest.exe'
if (-not (Test-Path -LiteralPath $taskExecutable)) { throw "Missing test binary: $taskExecutable" }
$taskArtifacts = if ($ArtifactDirectory) { [System.IO.Path]::GetFullPath($ArtifactDirectory) } else { Join-Path $taskBuild 'artifacts' }
New-Item -ItemType Directory -Force -Path $taskArtifacts | Out-Null
$taskHashBefore = (Get-FileHash -LiteralPath $taskSample -Algorithm SHA256).Hash
$taskVariables = @('PATH','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_SCALE_FACTOR',
    'IEC61850_LOCAL_SCL','IEC61850_TASK7_ARTIFACTS','IEC61850_TASK6_ARTIFACT','IEC61850_MMS_WIDTH','IEC61850_MMS_HEIGHT')
$taskOriginal = @{}
foreach ($taskVariable in $taskVariables) {
    $taskOriginal[$taskVariable] = [System.Environment]::GetEnvironmentVariable($taskVariable, 'Process')
}
try {
    $env:PATH = "$(Join-Path $taskBuild 'bin');$env:SystemRoot/System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $taskBuild 'bin/platforms'
    $env:QT_SCALE_FACTOR = '1'
    $env:IEC61850_LOCAL_SCL = $taskSample
    if ($Mode -eq 'Navigation') {
        foreach ($taskCase in @(@('wide-100','1',1280,800),@('wide-150','1.5',1280,800),
            @('wide-200','2',1280,800),@('narrow-100','1',620,560),@('narrow-200','2',620,560))) {
            $env:QT_SCALE_FACTOR = $taskCase[1]
            $env:IEC61850_MMS_WIDTH = [string]$taskCase[2]
            $env:IEC61850_MMS_HEIGHT = [string]$taskCase[3]
            $env:IEC61850_TASK7_ARTIFACTS = Join-Path $taskArtifacts $taskCase[0]
            $taskLog = Join-Path $taskArtifacts "$($taskCase[0]).txt"
            & $taskExecutable SclMmsNavigation SclMmsNavigationPaging -o "$taskLog,txt" *> (Join-Path $taskArtifacts "$($taskCase[0])-Loader.txt")
            if ($LASTEXITCODE -ne 0) { throw "Native navigation case $($taskCase[0]) failed; see $taskLog" }
            Write-Output "Native navigation $($taskCase[0]) passed."
        }
    } elseif ($Mode -eq 'Review') {
        $env:IEC61850_TASK7_ARTIFACTS = Join-Path $taskArtifacts 'review'
        $taskLog = Join-Path $taskArtifacts 'Review.txt'
        & $taskExecutable SclNavigationReview -o "$taskLog,txt" *> (Join-Path $taskArtifacts 'Review-Loader.txt')
        if ($LASTEXITCODE -ne 0) { throw "Native review failed; see $taskArtifacts/Review.txt" }
    } else {
        for ($taskRun = 1; $taskRun -le $Runs; ++$taskRun) {
            $env:IEC61850_TASK7_ARTIFACTS = Join-Path $taskArtifacts "navigation-$taskRun"
            $taskLog = Join-Path $taskArtifacts "Navigation-$taskRun.txt"
            & $taskExecutable SclNavigationBaseline -o "$taskLog,txt" *> (Join-Path $taskArtifacts "Navigation-$taskRun-Loader.txt")
            if ($LASTEXITCODE -ne 0) { throw "Navigation run $taskRun failed; see $taskLog" }
            $env:IEC61850_TASK6_ARTIFACT = Join-Path $taskArtifacts "Advanced-$taskRun.png"
            $taskLog = Join-Path $taskArtifacts "Advanced-$taskRun.txt"
            & $taskExecutable SclMmsBaseline -o "$taskLog,txt" *> (Join-Path $taskArtifacts "Advanced-$taskRun-Loader.txt")
            if ($LASTEXITCODE -ne 0) { throw "Advanced run $taskRun failed; see $taskLog" }
            Write-Output "Native navigation and advanced baseline $taskRun/$Runs passed."
        }
    }
    $taskHashAfter = (Get-FileHash -LiteralPath $taskSample -Algorithm SHA256).Hash
    if ($taskHashBefore -ne $taskHashAfter) { throw 'Source changed during baseline.' }
    $taskActualRuns = if ($Mode -eq 'Navigation') { 5 } elseif ($Mode -eq 'Review') { 1 } else { $Runs }
    $taskEvidence = [ordered]@{mode=$Mode; runs=$taskActualRuns; sha256Before=$taskHashBefore; sha256After=$taskHashAfter;
        fileBytes=(Get-Item -LiteralPath $taskSample).Length; testSha256=(Get-FileHash -LiteralPath $taskExecutable -Algorithm SHA256).Hash;
        applicationSha256=(Get-FileHash -LiteralPath (Join-Path $taskBuild 'bin/IEC61850Simulator.exe') -Algorithm SHA256).Hash;
        path=$env:PATH; platform=$env:QT_QPA_PLATFORM; scale=$env:QT_SCALE_FACTOR}
    if ($Mode -eq 'Navigation') {
        $taskEvidence['scale'] = 'per case: wide-100=1, wide-150=1.5, wide-200=2, narrow-100=1, narrow-200=2'
    }
    [System.IO.File]::WriteAllText((Join-Path $taskArtifacts "$Mode-RunManifest.json"),
        ($taskEvidence | ConvertTo-Json -Depth 4), [System.Text.UTF8Encoding]::new($false))
    Write-Output "Read-only source SHA-256 verified: $taskHashAfter"
} finally {
    foreach ($taskVariable in $taskVariables) {
        [System.Environment]::SetEnvironmentVariable($taskVariable, $taskOriginal[$taskVariable], 'Process')
    }
}
