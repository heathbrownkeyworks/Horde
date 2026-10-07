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
$required = @('plugin/Horde.esp', 'assets/fonts/Poppins-Regular.ttf', 'assets/fonts/Poppins-Medium.ttf',
    'assets/fonts/Poppins-SemiBold.ttf', 'assets/fonts/Montserrat-Black.ttf', 'assets/fonts/HordeIcons.ttf',
    'LICENSE', 'EXCEPTIONS.md', 'LICENSING.md', 'THIRD_PARTY_NOTICES.md', 'README.md', 'IMGUI-VALIDATION.md',
    'IMPROVEMENTS-VALIDATION.md', 'AUDIT-FIXES-VALIDATION.md')
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $file) -PathType Leaf)) { throw "Missing package input: $file" }
}
$revision = & git -C $repoRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot record source revision.' }
$sourceDirty = [bool](& git -C $repoRoot status --porcelain --untracked-files=normal)
$pluginDirectory = Join-Path $destination 'SKSE/Plugins'
$fontDirectory = Join-Path $pluginDirectory 'Horde/fonts'
New-Item -ItemType Directory -Path $fontDirectory -Force | Out-Null
Copy-Item -LiteralPath $DllPath -Destination (Join-Path $pluginDirectory 'Horde.dll')
Copy-Item -LiteralPath (Join-Path $repoRoot 'plugin/Horde.esp') -Destination $destination
Copy-Item -Path (Join-Path $repoRoot 'assets/fonts/*.ttf') -Destination $fontDirectory
Copy-Item -LiteralPath (Join-Path $repoRoot 'licenses') -Destination $destination -Recurse
foreach ($file in @('LICENSE','EXCEPTIONS.md','LICENSING.md','THIRD_PARTY_NOTICES.md','README.md','IMGUI-VALIDATION.md','IMPROVEMENTS-VALIDATION.md','AUDIT-FIXES-VALIDATION.md')) {
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
    variant='Horde 3.0.1'; version='3.0.1'; ui='Dear ImGui / DX11'
    sourceRevision=$revision.Trim(); sourceDirty=$sourceDirty
    commonLibRevision=(& git -C (Join-Path $repoRoot 'lib/commonlibsse-ng') rev-parse HEAD).Trim()
    createdUtc=[DateTime]::UtcNow.ToString('o'); inGameValidation='NOT RUN'; vrSupported=$false
    signatureStatus=$signature.Status.ToString(); files=$files
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding UTF8
Write-Host "Staged $($files.Count) files at $destination"
Write-Host "DLL signature: $($signature.Status); source dirty: $sourceDirty; Skyrim testing: NOT RUN"
