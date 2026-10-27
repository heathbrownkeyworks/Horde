param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$DllPath
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) { throw "Choose a new output directory: $destination" }
if (-not $DllPath) { $DllPath = Join-Path $repoRoot 'build/windows/x64/release/Horde.dll' }
$DllPath = (Resolve-Path -LiteralPath $DllPath).Path
$required = @('plugin/Horde.esp',
    'LICENSE', 'EXCEPTIONS.md', 'LICENSING.md', 'THIRD_PARTY_NOTICES.md', 'README.md', 'CHANGELOG.md',
    'IMPROVEMENTS-VALIDATION.md', 'AUDIT-FIXES-VALIDATION.md',
    'view/index.html', 'view/horde.js', 'view/horde.css', 'view/icons.js', 'view/emberfall-tokens.css')
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $file) -PathType Leaf)) { throw "Missing package input: $file" }
}
$revision = & git -C $repoRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot record source revision.' }
$sourceDirty = [bool](& git -C $repoRoot status --porcelain --untracked-files=normal)
$pluginDirectory = Join-Path $destination 'SKSE/Plugins'
New-Item -ItemType Directory -Path $pluginDirectory -Force | Out-Null
Copy-Item -LiteralPath $DllPath -Destination (Join-Path $pluginDirectory 'Horde.dll')
Copy-Item -LiteralPath (Join-Path $repoRoot 'plugin/Horde.esp') -Destination $destination
Copy-Item -LiteralPath (Join-Path $repoRoot 'licenses') -Destination $destination -Recurse
# The Magelight UI web interface: view/ without the browser-preview mock in
# view/dev/, loaded by HordeUI from Data/Magelight/Horde/index.html.
$viewSource = Join-Path $repoRoot 'view'
$viewDirectory = Join-Path $destination 'Magelight/Horde'
$viewFiles = @(Get-ChildItem -LiteralPath $viewSource -File -Recurse | Where-Object {
    -not $_.FullName.Substring($viewSource.Length + 1).StartsWith('dev\')
})
if (-not ($viewFiles | Where-Object { $_.Extension -eq '.woff' })) { throw 'Missing package input: view/fonts/*.woff' }
foreach ($file in $viewFiles) {
    $target = Join-Path $viewDirectory $file.FullName.Substring($viewSource.Length + 1)
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target
}
if (Test-Path -LiteralPath (Join-Path $viewDirectory 'dev')) { throw 'view/dev must not be packaged.' }
foreach ($file in @('LICENSE','EXCEPTIONS.md','LICENSING.md','THIRD_PARTY_NOTICES.md','README.md','CHANGELOG.md','IMPROVEMENTS-VALIDATION.md','AUDIT-FIXES-VALIDATION.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $file) -Destination $destination
}
$commonLibLicenses = Join-Path $destination 'licenses/CommonLibSSE-NG'
New-Item -ItemType Directory -Path $commonLibLicenses | Out-Null
foreach ($file in @('COPYING','EXCEPTIONS.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "lib/commonlibsse-ng/$file") -Destination $commonLibLicenses
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'lib/commonlibsse-ng/licenses') -Destination $commonLibLicenses -Recurse
$files = @(Get-ChildItem -LiteralPath $destination -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path=$_.FullName.Substring($destination.Length+1).Replace('\','/'); bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
$signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $pluginDirectory 'Horde.dll')
$manifest = [ordered]@{
    variant='Horde 3.1.0'; version='3.1.0'; ui='Magelight UI web interface (requires Magelight UI 1.1+)'
    sourceRevision=$revision.Trim(); sourceDirty=$sourceDirty
    commonLibRevision=(& git -C (Join-Path $repoRoot 'lib/commonlibsse-ng') rev-parse HEAD).Trim()
    createdUtc=[DateTime]::UtcNow.ToString('o'); inGameValidation='NOT RUN'; vrSupported=$false
    signatureStatus=$signature.Status.ToString(); files=$files
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding UTF8
Write-Host "Staged $($files.Count) files at $destination"
Write-Host "DLL signature: $($signature.Status); source dirty: $sourceDirty; Skyrim testing: NOT RUN"
