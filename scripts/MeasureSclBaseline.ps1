param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $InputPath).Path
$destination = [System.IO.Path]::GetFullPath($OutputPath)
if ($source -eq $destination) { throw 'OutputPath must not overwrite the input.' }
$hashBefore = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$settings = [System.Xml.XmlReaderSettings]::new()
$settings.DtdProcessing = [System.Xml.DtdProcessing]::Prohibit
$settings.XmlResolver = $null
$reader = [System.Xml.XmlReader]::Create($source, $settings)
$standardNamespace = 'http://www.iec.ch/61850/2003/SCL'
$standardCounts = [ordered]@{}
$extensionCounts = [ordered]@{}
$rootAttributes = [ordered]@{}
$rootName = $null
$rootNamespace = $null
$lastElementLine = 0
$watch = [System.Diagnostics.Stopwatch]::StartNew()
try {
    while ($reader.Read()) {
        if ($reader.NodeType -ne [System.Xml.XmlNodeType]::Element) { continue }
        if ($null -eq $rootName) {
            $rootName = $reader.LocalName
            $rootNamespace = $reader.NamespaceURI
            if ($reader.MoveToFirstAttribute()) {
                do { $rootAttributes[$reader.Name] = $reader.Value } while ($reader.MoveToNextAttribute())
                [void]$reader.MoveToElement()
            }
        }
        if ($reader.NamespaceURI -eq $standardNamespace) {
            $key = $reader.LocalName
            if (!$standardCounts.Contains($key)) { $standardCounts[$key] = 0 }
            $standardCounts[$key]++
        } else {
            $key = '{' + $reader.NamespaceURI + '}' + $reader.LocalName
            if (!$extensionCounts.Contains($key)) { $extensionCounts[$key] = 0 }
            $extensionCounts[$key]++
        }
        $lastElementLine = ([System.Xml.IXmlLineInfo]$reader).LineNumber
    }
} finally {
    $watch.Stop()
    $reader.Dispose()
}
$hashAfter = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
if ($hashBefore -ne $hashAfter) { throw 'Input changed during scan; baseline discarded.' }
$process = [System.Diagnostics.Process]::GetCurrentProcess()
$result = [ordered]@{
    baselineVersion = 1
    inputFileName = [System.IO.Path]::GetFileName($source)
    bytes = (Get-Item -LiteralPath $source).Length
    sha256 = $hashBefore.ToLowerInvariant()
    scannedAt = [DateTimeOffset]::Now.ToString('o')
    scanner = 'PowerShell/.NET XmlReader; namespace URI + localName; DTD prohibited'
    runtime = [System.Runtime.InteropServices.RuntimeInformation]::FrameworkDescription
    xmlWellFormed = $true
    schemaValidated = $false
    semanticValidated = $false
    root = $rootName
    namespace = $rootNamespace
    rootAttributes = $rootAttributes
    standardElementCounts = $standardCounts
    extensionElementCounts = $extensionCounts
    lastElementLine = $lastElementLine
    scanElapsedMs = $watch.ElapsedMilliseconds
    processPeakWorkingSetBytes = $process.PeakWorkingSet64
    measurementScope = 'Streaming scan only; time excludes SHA256; process peak includes PowerShell host; not application performance.'
}
$parent = [System.IO.Path]::GetDirectoryName($destination)
[void][System.IO.Directory]::CreateDirectory($parent)
[System.IO.File]::WriteAllText($destination,
    (($result | ConvertTo-Json -Depth 8).Replace("`r`n", "`n") + "`n"),
    [System.Text.UTF8Encoding]::new($false))
Write-Output "Baseline written: $destination"
