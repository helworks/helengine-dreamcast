[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string]$BuildId,
    [string]$DockerImage = 'helengine-dc-sdk:v01',
    [string]$KosPath,
    [string]$OutputRoot,
    [ValidateSet('Diagnostic', 'Authored')]
    [string]$Mode = 'Diagnostic',
    [string]$GeneratedCoreRoot,
    [string]$ContentRoot,
    [string]$CatalogRoot,
    [switch]$RuntimeInitProbe,
    [ValidateSet('Cube', 'ClearOnly', 'ScreenTriangle')]
    [string]$Scene = 'Cube',
    [switch]$StaticPose,
    [switch]$DisableCulling,
    [switch]$ReverseTriangleOrder,
    [switch]$NearPlaneProbe,
    [switch]$ScreenEdgeProbe,
    [ValidateRange(0, 3600)]
    [double]$StopAfterSeconds = 0.0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$dreamcastRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.dreamcast'))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = $dreamcastRoot
}
if ([string]::IsNullOrWhiteSpace($KosPath)) {
    $KosPath = Join-Path $dreamcastRoot 'dependencies\kos'
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$KosPath = [IO.Path]::GetFullPath($KosPath)
$repoPrefix = $repoRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$dreamcastPrefix = $dreamcastRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$isInRepository = $OutputRoot.Equals($repoRoot, [StringComparison]::OrdinalIgnoreCase) -or $OutputRoot.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)
$isInDreamcastRoot = $OutputRoot.Equals($dreamcastRoot, [StringComparison]::OrdinalIgnoreCase) -or $OutputRoot.StartsWith($dreamcastPrefix, [StringComparison]::OrdinalIgnoreCase)
if ($isInRepository -and -not $isInDreamcastRoot) {
    throw "OutputRoot must be outside the source worktree or inside .dreamcast: $OutputRoot"
}
$buildRoot = Join-Path $OutputRoot $BuildId

$lockPath = Join-Path $repoRoot 'toolchain-lock.txt'
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "Toolchain lock does not exist: $lockPath"
}

function Read-LockFile {
    param([string]$Path)
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) { continue }
        $separator = $line.IndexOf('=')
        if ($separator -le 0) { throw "Malformed toolchain lock line: $line" }
        $values[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
    }
    return $values
}

function Invoke-CheckedNative {
    param([string]$FilePath, [string[]]$ArgumentList)
    $output = & $FilePath @ArgumentList 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Command failed ($exitCode): $FilePath $($ArgumentList -join ' ')`n$($output -join [Environment]::NewLine)"
    }
    return @($output)
}

function Get-SourceInputFingerprint {
    $sourceInputFiles = @(
        Get-Item -LiteralPath (Join-Path $repoRoot 'Makefile')
        Get-Item -LiteralPath (Join-Path $repoRoot 'scripts/build.ps1')
        Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src') -File -Recurse
        Get-ChildItem -LiteralPath (Join-Path $repoRoot 'tests') -File -Recurse
    )
    $sourceHashLines = @(
        foreach ($sourceInputFile in $sourceInputFiles) {
            $relativePath = [IO.Path]::GetRelativePath($repoRoot, $sourceInputFile.FullName).Replace('\', '/')
            $contentHash = (Get-FileHash -LiteralPath $sourceInputFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$relativePath=$contentHash"
        }
    ) | Sort-Object
    $sourceHashText = $sourceHashLines -join "`n"
    [PSCustomObject]@{
        Lines = @($sourceHashLines)
        Hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($sourceHashText))).ToLowerInvariant()
    }
}

function Get-ExternalInputFingerprint {
    param([string]$Root)
    if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path -LiteralPath $Root -PathType Container)) {
        return [PSCustomObject]@{ Lines = @(); Hash = 'none' }
    }
    $rootFullPath = [IO.Path]::GetFullPath($Root)
    $lines = @(
        foreach ($inputFile in Get-ChildItem -LiteralPath $rootFullPath -File -Recurse) {
            $relativePath = [IO.Path]::GetRelativePath($rootFullPath, $inputFile.FullName).Replace('\', '/')
            $contentHash = (Get-FileHash -LiteralPath $inputFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$relativePath=$contentHash"
        }
    ) | Sort-Object
    $text = $lines -join "`n"
    [PSCustomObject]@{
        Lines = @($lines)
        Hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($text))).ToLowerInvariant()
    }
}

if (Test-Path -LiteralPath $buildRoot) {
    throw "Refusing to reuse existing build directory: $buildRoot"
}
if (-not (Test-Path -LiteralPath $KosPath -PathType Container)) {
    throw "KOS checkout does not exist: $KosPath"
}
if (-not (Test-Path -LiteralPath $repoRoot -PathType Container)) {
    throw "Source root does not exist: $repoRoot"
}
if ($Mode -eq 'Authored') {
    if ([string]::IsNullOrWhiteSpace($GeneratedCoreRoot) -or
        [string]::IsNullOrWhiteSpace($ContentRoot) -or
        [string]::IsNullOrWhiteSpace($CatalogRoot)) {
        throw 'Authored mode requires GeneratedCoreRoot, ContentRoot and CatalogRoot.'
    }
    $GeneratedCoreRoot = [IO.Path]::GetFullPath($GeneratedCoreRoot)
    $ContentRoot = [IO.Path]::GetFullPath($ContentRoot)
    $CatalogRoot = [IO.Path]::GetFullPath($CatalogRoot)
    if (-not (Test-Path -LiteralPath (Join-Path $GeneratedCoreRoot 'Core.hpp') -PathType Leaf)) {
        throw "Generated Core root does not contain Core.hpp: $GeneratedCoreRoot"
    }
    if (-not (Test-Path -LiteralPath $ContentRoot -PathType Container)) {
        throw "Authored content root does not exist: $ContentRoot"
    }
    if (-not (Test-Path -LiteralPath $CatalogRoot -PathType Container)) {
        throw "Authored catalog root does not exist: $CatalogRoot"
    }
    foreach ($catalogFileName in @('dc_runtime_scene_catalog.hpp', 'dc_runtime_scene_catalog.cpp')) {
        if (-not (Test-Path -LiteralPath (Join-Path $CatalogRoot $catalogFileName) -PathType Leaf)) {
            throw "Authored mode requires the Task 4 catalog ABI file '$catalogFileName' in CatalogRoot: $CatalogRoot"
        }
    }
    if ($StaticPose -or $DisableCulling -or $ReverseTriangleOrder -or $NearPlaneProbe -or
        $ScreenEdgeProbe -or $StopAfterSeconds -gt 0.0 -or $Scene -ne 'Cube') {
        throw 'Diagnostic scene selectors cannot be used in Authored mode.'
    }
}

$lock = Read-LockFile $lockPath
$expectedKosCommit = $lock['KosCommit']
$actualKosCommit = ((Invoke-CheckedNative 'git' @('-c', "safe.directory=$KosPath", '-C', $KosPath, 'rev-parse', 'HEAD')) -join '').Trim()
if ($actualKosCommit -ne $expectedKosCommit) {
    throw "KOS commit mismatch. Expected $expectedKosCommit, found $actualKosCommit."
}
$expectedImageTag = $lock['LocalImageTag']
if ($DockerImage -ne $expectedImageTag) {
    throw "Docker image tag '$DockerImage' is not the locked tag '$expectedImageTag'. Run prepare_toolchain.ps1 first."
}
$actualImageId = ((Invoke-CheckedNative 'docker' @('image', 'inspect', $DockerImage, '--format', '{{.Id}}')) -join '').Trim()
if ($actualImageId -ne $lock['LocalImageId']) {
    throw "SDK image ID mismatch. Expected $($lock['LocalImageId']), found $actualImageId."
}

New-Item -ItemType Directory -Path $buildRoot -Force:$false | Out-Null
$sourceFingerprintBefore = Get-SourceInputFingerprint
Set-Content -LiteralPath (Join-Path $buildRoot 'source-hashes-before.txt') -Value $sourceFingerprintBefore.Lines -Encoding UTF8
$externalRoots = @($GeneratedCoreRoot, $ContentRoot, $CatalogRoot) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$externalFingerprintBeforeLines = @(
    foreach ($root in $externalRoots) {
        $fingerprint = Get-ExternalInputFingerprint $root
        "$root=$($fingerprint.Hash)"
    }
) | Sort-Object
$externalFingerprintBeforeText = $externalFingerprintBeforeLines -join "`n"
$externalFingerprintBefore = [PSCustomObject]@{
    Lines = $externalFingerprintBeforeLines
    Hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($externalFingerprintBeforeText))).ToLowerInvariant()
}
Set-Content -LiteralPath (Join-Path $buildRoot 'external-input-hashes-before.txt') -Value $externalFingerprintBefore.Lines -Encoding UTF8
$dockerLogPath = Join-Path $buildRoot 'docker-build.log'
$diagnosticDefines = [Collections.Generic.List[string]]::new()
switch ($Scene) {
    'ClearOnly' { $diagnosticDefines.Add('-DHELENGINE_SCENE_CLEAR') }
    'ScreenTriangle' { $diagnosticDefines.Add('-DHELENGINE_SCENE_TRIANGLE') }
}
if ($StaticPose) { $diagnosticDefines.Add('-DHELENGINE_STATIC') }
if ($DisableCulling) { $diagnosticDefines.Add('-DHELENGINE_DISABLE_CULLING') }
if ($ReverseTriangleOrder) { $diagnosticDefines.Add('-DHELENGINE_REVERSE_TRIANGLES') }
if ($NearPlaneProbe) { $diagnosticDefines.Add('-DHELENGINE_NEAR_PLANE_PROBE') }
if ($ScreenEdgeProbe) { $diagnosticDefines.Add('-DHELENGINE_SCREEN_EDGE_PROBE') }
if ($StopAfterSeconds -gt 0.0) {
    $stopText = $StopAfterSeconds.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
    $diagnosticDefines.Add("-DHELENGINE_STOP_AFTER_SECONDS=$stopText")
}
$diagnosticText = "Scene=$Scene;StaticPose=$([bool]$StaticPose);DisableCulling=$([bool]$DisableCulling);ReverseTriangleOrder=$([bool]$ReverseTriangleOrder);NearPlaneProbe=$([bool]$NearPlaneProbe);ScreenEdgeProbe=$([bool]$ScreenEdgeProbe);StopAfterSeconds=$StopAfterSeconds;Defines=$($diagnosticDefines -join ',')"
$diagnosticCppFlags = $diagnosticDefines -join ' '
$makeDiagnosticArgument = if ($diagnosticCppFlags.Length -gt 0) { " CPPFLAGS='$diagnosticCppFlags'" } else { '' }
$innerCommand = 'set -e; actual=$(git -C /opt/toolchains/dc/kos rev-parse HEAD); test "$actual" = "' + $expectedKosCommit + '"; export KOS_BASE=/opt/toolchains/dc/kos-built; source /opt/toolchains/dc/kos-built/environ.sh; make -j8 -C /src BUILD_ROOT=/out MODE=' + $Mode
if ($Mode -eq 'Authored') {
    $innerCommand = $innerCommand + ' GENERATED_CORE_ROOT=/generated CONTENT_ROOT=/content CATALOG_ROOT=/catalog'
    if ($RuntimeInitProbe) { $innerCommand = $innerCommand + ' RUNTIME_INIT_PROBE=1' }
}
$innerCommand = $innerCommand + $makeDiagnosticArgument
$dockerArgs = @(
    'run', '--rm', '--workdir', '/src',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$KosPath,target=/opt/toolchains/dc/kos,readonly",
    '--mount', "type=bind,source=$buildRoot,target=/out",
    '--entrypoint', '/bin/bash',
    $DockerImage,
    '-lc', $innerCommand
)
if ($Mode -eq 'Authored') {
    $dockerArgs = @(
        'run', '--rm', '--workdir', '/src',
        '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
        '--mount', "type=bind,source=$KosPath,target=/opt/toolchains/dc/kos,readonly",
        '--mount', "type=bind,source=$GeneratedCoreRoot,target=/generated,readonly",
        '--mount', "type=bind,source=$ContentRoot,target=/content,readonly",
        '--mount', "type=bind,source=$CatalogRoot,target=/catalog,readonly",
        '--mount', "type=bind,source=$buildRoot,target=/out",
        '--entrypoint', '/bin/bash',
        $DockerImage,
        '-lc', $innerCommand
    )
}
$commandText = "docker $($dockerArgs -join ' ')"
$dockerExitCode = 0
try {
    & docker @dockerArgs 2>&1 | Tee-Object -FilePath $dockerLogPath
    $dockerExitCode = $LASTEXITCODE
}
finally {
    if (-not (Test-Path -LiteralPath $dockerLogPath -PathType Leaf)) {
        New-Item -ItemType File -Path $dockerLogPath -Force | Out-Null
    }
}
if ($dockerExitCode -ne 0) {
    throw "Docker build failed with exit code $dockerExitCode. See $dockerLogPath"
}

$elfPath = Join-Path $buildRoot 'helengine_dc.elf'
$mapPath = Join-Path $buildRoot 'helengine_dc.map'
if (-not (Test-Path -LiteralPath $elfPath -PathType Leaf)) {
    throw "Successful build did not produce $elfPath"
}
if (-not (Test-Path -LiteralPath $mapPath -PathType Leaf)) {
    throw "Successful build did not produce $mapPath"
}
$readelfOutput = @(& docker run --rm --mount "type=bind,source=$buildRoot,target=/out,readonly" --entrypoint $lock['ReadelfPath'] $DockerImage -h /out/helengine_dc.elf 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "Pinned sh-elf-readelf failed: $($readelfOutput -join [Environment]::NewLine)"
}
$readelfText = $readelfOutput -join [Environment]::NewLine
if ($readelfText -notmatch 'little endian' -or $readelfText -notmatch 'SuperH') {
    throw "ELF header is not a SuperH little-endian ELF:`n$readelfText"
}
$readelfOutput | Set-Content -LiteralPath (Join-Path $buildRoot 'elf-header.txt') -Encoding UTF8

$elfHash = (Get-FileHash -LiteralPath $elfPath -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceFingerprintAfter = Get-SourceInputFingerprint
if ($sourceFingerprintBefore.Hash -ne $sourceFingerprintAfter.Hash) {
    throw "Source inputs changed during the build; refusing to attribute the ELF to one source snapshot. Before=$($sourceFingerprintBefore.Hash), After=$($sourceFingerprintAfter.Hash)"
}
Set-Content -LiteralPath (Join-Path $buildRoot 'source-hashes.txt') -Value $sourceFingerprintAfter.Lines -Encoding UTF8
$externalFingerprintAfterLines = @(
    foreach ($root in $externalRoots) {
        $fingerprint = Get-ExternalInputFingerprint $root
        "$root=$($fingerprint.Hash)"
    }
) | Sort-Object
$externalFingerprintAfterText = $externalFingerprintAfterLines -join "`n"
$externalFingerprintAfterHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($externalFingerprintAfterText))).ToLowerInvariant()
if ($externalFingerprintBefore.Hash -ne $externalFingerprintAfterHash) {
    throw "External build inputs changed during the build. Before=$($externalFingerprintBefore.Hash), After=$externalFingerprintAfterHash"
}
Set-Content -LiteralPath (Join-Path $buildRoot 'external-input-hashes.txt') -Value $externalFingerprintAfterLines -Encoding UTF8
$gitCommit = ((Invoke-CheckedNative 'git' @('-c', "safe.directory=$repoRoot", '-C', $repoRoot, 'rev-parse', 'HEAD')) -join '').Trim()
$gitStatus = (Invoke-CheckedNative 'git' @('-c', "safe.directory=$repoRoot", '-C', $repoRoot, 'status', '--porcelain', '--untracked-files=all')) -join "`n"
$gitDirty = if ([string]::IsNullOrWhiteSpace($gitStatus)) { 'false' } else { 'true' }
$statusListingHash = if ([string]::IsNullOrWhiteSpace($gitStatus)) { 'none' } else { [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($gitStatus))).ToLowerInvariant() }
$manifestLines = @(
    'ManifestVersion=1',
    "BuildId=$BuildId",
    "SourceRoot=$repoRoot",
    "GitCommit=$gitCommit",
    "GitDirty=$gitDirty",
    "GitStatusListingSha256=$statusListingHash",
    "SourceInputSha256=$($sourceFingerprintAfter.Hash)",
    "SourceInputSha256Before=$($sourceFingerprintBefore.Hash)",
    "SourceInputSha256After=$($sourceFingerprintAfter.Hash)",
    'SourceInputHashBeforeManifest=source-hashes-before.txt',
    'SourceInputHashManifest=source-hashes.txt',
    "KosCommit=$expectedKosCommit",
    "DockerImage=$DockerImage",
    "DockerImageId=$actualImageId",
    "UpstreamBaseImage=$($lock['UpstreamBaseImage'])",
    "CompilerFlags=$($lock['CompilerFlags'])",
    "Mode=$Mode",
    "GeneratedCoreRoot=$GeneratedCoreRoot",
    "ContentRoot=$ContentRoot",
    "CatalogRoot=$CatalogRoot",
    "RuntimeInitProbe=$([bool]$RuntimeInitProbe)",
    "ExternalInputSha256=$externalFingerprintAfterHash",
    "DiagnosticSelectors=$diagnosticText",
    'Target=helengine_dc',
    'ELFRelativePath=helengine_dc.elf',
    'MapRelativePath=helengine_dc.map',
    "ELFSHA256=$elfHash",
    "ExactDockerCommand=$commandText",
    'ReadelfHeader=elf-header.txt',
    'BuildLog=docker-build.log'
)
Set-Content -LiteralPath (Join-Path $buildRoot 'build-manifest.txt') -Value $manifestLines -Encoding UTF8
Set-Content -LiteralPath (Join-Path $buildRoot 'build-command.txt') -Value $commandText -Encoding UTF8
Write-Output "Built $elfPath"
Write-Output "SHA256 $elfHash"
