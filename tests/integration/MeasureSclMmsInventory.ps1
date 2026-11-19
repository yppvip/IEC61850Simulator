param(
    [Parameter(Mandatory = $true)][string]$Path
)

# Local read-only inventory. No engineering field values or network requests.
$ErrorActionPreference = 'Stop'
$file = Get-Item -LiteralPath $Path
if ($file.Length -gt 128MB) { throw 'Inventory limit: 128 MiB.' }
$before = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
$settings = [System.Xml.XmlReaderSettings]::new()
$settings.DtdProcessing = [System.Xml.DtdProcessing]::Prohibit
$settings.XmlResolver = $null
$settings.MaxCharactersInDocument = 256MB
$reader = [System.Xml.XmlReader]::Create($file.FullName, $settings)
try { $document = [System.Xml.Linq.XDocument]::Load($reader) }
finally { $reader.Dispose() }
$ns = [System.Xml.Linq.XNamespace]::Get('http://www.iec.ch/61850/2003/SCL')
if ($document.Root.Name -ne $ns + 'SCL') { throw 'Unsupported SCL root/namespace.' }

function AttributeStates($elements, [string]$name) {
    $states = [ordered]@{ missing = 0; empty = 0; nonEmpty = 0 }
    foreach ($element in $elements) {
        $attribute = $element.Attribute($name)
        if ($null -eq $attribute) { $states.missing++ }
        elseif ($attribute.Value.Length -eq 0) { $states.empty++ }
        else { $states.nonEmpty++ }
    }
    return $states
}

function Histogram($elements, [string]$attributeName) {
    $values = [ordered]@{}
    foreach ($element in $elements) {
        $attribute = $element.Attribute($attributeName)
        $key = if ($null -eq $attribute) { '(missing)' } else { $attribute.Value }
        if (-not $values.Contains($key)) { $values[$key] = 0 }
        $values[$key]++
    }
    return $values
}

$counts = [ordered]@{}
foreach ($name in @('IED','AccessPoint','Server','ServerAt','LDevice','LN0','LN',
        'DataSet','FCDA','ReportControl','RptEnabled','ClientLN','Association',
        'Services','ReportSettings','ConfReportControl','DynAssociation','ConnectedAP','ExtRef')) {
    $counts[$name] = @($document.Descendants($ns + $name)).Count
}
$reports = @($document.Descendants($ns + 'ReportControl'))
$fields = [ordered]@{}
foreach ($name in @('name','desc','rptID','datSet','buffered','indexed','confRev','bufTime','intgPd')) {
    $fields[$name] = AttributeStates $reports $name
}
$reportChildren = [ordered]@{}
foreach ($name in @('TrgOps','OptFields','RptEnabled')) {
    $reportChildren[$name] = @($reports | Where-Object { $null -ne $_.Element($ns + $name) }).Count
}
$parameters = [ordered]@{}
foreach ($name in @('TrgOps','OptFields')) {
    $elements = @($reports | ForEach-Object { $_.Elements($ns + $name) })
    $attributes = [ordered]@{}
    $names = @($elements | ForEach-Object { $_.Attributes() } | ForEach-Object { $_.Name.LocalName } | Sort-Object -Unique)
    foreach ($attributeName in $names) { $attributes[$attributeName] = Histogram $elements $attributeName }
    $parameters[$name] = $attributes
}
$rootVersion = [ordered]@{}
foreach ($name in @('version','revision','release')) {
    $attribute = $document.Root.Attribute($name)
    $rootVersion[$name] = if ($null -eq $attribute) { $null } else { $attribute.Value }
}
$header = $document.Root.Element($ns + 'Header')
$networkTypes = @($document.Descendants($ns + 'ConnectedAP') |
    ForEach-Object { $_.Elements($ns + 'Address') } | ForEach-Object { $_.Elements($ns + 'P') })
$after = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
if ($before -ne $after) { throw 'Source changed during inventory.' }
[ordered]@{
    inventoryVersion = 1
    meaning = 'XML declaration inventory only; no XSD, semantic or runtime validation'
    fileBytes = $file.Length
    sha256Before = $before
    sha256After = $after
    namespace = $document.Root.Name.NamespaceName
    rootVersion = $rootVersion
    headerHasVersion = ($null -ne $header -and $null -ne $header.Attribute('version'))
    counts = $counts
    reportFields = $fields
    reportChildren = $reportChildren
    buffered = Histogram $reports 'buffered'
    indexed = Histogram $reports 'indexed'
    rptEnabledMax = Histogram @($document.Descendants($ns + 'RptEnabled')) 'max'
    clientFields = [ordered]@{
        iedName = AttributeStates @($document.Descendants($ns + 'ClientLN')) 'iedName'
        apRef = AttributeStates @($document.Descendants($ns + 'ClientLN')) 'apRef'
    }
    extRefServiceTypes = Histogram @($document.Descendants($ns + 'ExtRef')) 'serviceType'
    connectedApAddressTypes = Histogram $networkTypes 'type'
    reportParameters = $parameters
} | ConvertTo-Json -Depth 12
