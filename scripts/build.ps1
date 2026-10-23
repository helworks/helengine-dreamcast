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
    [string]$NativeObjectCacheRoot,
    [switch]$ReuseBuildDirectory,
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
        Get-Item -LiteralPath (Join-Path $repoRoot 'scripts/dc-sdk-identity.ps1')
        Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src') -File -Recurse
        Get-ChildItem -LiteralPath (Join-Path $repoRoot 'tests') -File -Recurse
    )
    $sourceHashLines = @(
        foreach ($sourceInputFile in $sourceInputFiles) {
            $relativePath = (Get-RelativePath $repoRoot $sourceInputFile.FullName).Replace('\', '/')
            $contentHash = (Get-FileHash -LiteralPath $sourceInputFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$relativePath=$contentHash"
        }
    ) | Sort-Object
    $sourceHashText = $sourceHashLines -join "`n"
    [PSCustomObject]@{
        Lines = @($sourceHashLines)
        Hash = Get-TextSha256 $sourceHashText
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
            $relativePath = (Get-RelativePath $rootFullPath $inputFile.FullName).Replace('\', '/')
            $contentHash = (Get-FileHash -LiteralPath $inputFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$relativePath=$contentHash"
        }
    ) | Sort-Object
    $text = $lines -join "`n"
    [PSCustomObject]@{
        Lines = @($lines)
        Hash = Get-TextSha256 $text
    }
}

if ((Test-Path -LiteralPath $buildRoot) -and -not $ReuseBuildDirectory) {
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
. (Join-Path $PSScriptRoot 'dc-sdk-identity.ps1')
$sdkIdentity = Get-DreamcastSdkImageIdentity -CanonicalImageId $lock['LocalImageId'] -LockedTag $DockerImage
$actualImageId = $sdkIdentity.CanonicalImageId
$localImageId = $sdkIdentity.LocalImageId

function Get-TextSha256 {
    param([string]$Text)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        return ([BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

function Get-RelativePath {
    param([string]$BasePath, [string]$TargetPath)
    $baseFullPath = [IO.Path]::GetFullPath($BasePath).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $targetFullPath = [IO.Path]::GetFullPath($TargetPath)
    $relativeUri = ([Uri]$baseFullPath).MakeRelativeUri([Uri]$targetFullPath)
    return [Uri]::UnescapeDataString($relativeUri.ToString()).Replace('/', [IO.Path]::DirectorySeparatorChar)
}

$sourceFingerprintBefore = Get-SourceInputFingerprint
if ($ReuseBuildDirectory -and $Mode -eq 'Authored' -and $env:HELENGINE_MENU_GENERATED_AUDIT_SCRIPT) {
    & $env:HELENGINE_MENU_GENERATED_AUDIT_SCRIPT -GeneratedCoreRoot $GeneratedCoreRoot
}
$nativeObjectRoot = Join-Path $buildRoot 'obj'
if (-not [string]::IsNullOrWhiteSpace($NativeObjectCacheRoot)) {
    $NativeObjectCacheRoot = [IO.Path]::GetFullPath($NativeObjectCacheRoot)
    $isNativeCacheInRepository = $NativeObjectCacheRoot.Equals($repoRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $NativeObjectCacheRoot.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)
    $isNativeCacheInDreamcastRoot = $NativeObjectCacheRoot.Equals($dreamcastRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $NativeObjectCacheRoot.StartsWith($dreamcastPrefix, [StringComparison]::OrdinalIgnoreCase)
    $outputPrefix = $OutputRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (($isNativeCacheInRepository -and -not $isNativeCacheInDreamcastRoot) -or
        $NativeObjectCacheRoot.Equals($OutputRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $NativeObjectCacheRoot.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $OutputRoot.StartsWith($NativeObjectCacheRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "NativeObjectCacheRoot must be outside source roots and disjoint from output: $NativeObjectCacheRoot"
    }
}

New-Item -ItemType Directory -Path $buildRoot -Force:$ReuseBuildDirectory | Out-Null
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
    Hash = Get-TextSha256 $externalFingerprintBeforeText
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
$packageObjectRoot = Join-Path $buildRoot 'package-obj'
if (-not [string]::IsNullOrWhiteSpace($NativeObjectCacheRoot)) {
    $generatedFingerprint = (Get-ExternalInputFingerprint $GeneratedCoreRoot).Hash
    $catalogFingerprint = (Get-ExternalInputFingerprint $CatalogRoot).Hash
    $cacheInputs = @(
        $actualImageId, $actualKosCommit, $Mode, $diagnosticText,
        $sourceFingerprintBefore.Hash, $generatedFingerprint, $catalogFingerprint
    ) -join "`n"
    $cacheFingerprint = Get-TextSha256 $cacheInputs
    $nativeObjectRoot = Join-Path (Join-Path $NativeObjectCacheRoot 'dreamcast') $cacheFingerprint
    $contentFingerprint = (Get-ExternalInputFingerprint $ContentRoot).Hash
    $packageObjectRoot = Join-Path (Join-Path $nativeObjectRoot 'package') $contentFingerprint
    New-Item -ItemType Directory -Path $nativeObjectRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $packageObjectRoot -Force | Out-Null
} else {
    New-Item -ItemType Directory -Path $nativeObjectRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $packageObjectRoot -Force | Out-Null
}
$diagnosticCppFlags = $diagnosticDefines -join ' '
$makeDiagnosticArgument = if ($diagnosticCppFlags.Length -gt 0) { " CPPFLAGS='$diagnosticCppFlags'" } else { '' }
$innerCommand = 'set -e; actual=$(git -C /opt/toolchains/dc/kos rev-parse HEAD); test "$actual" = "' + $expectedKosCommit + '"; export KOS_BASE=/opt/toolchains/dc/kos-built; source /opt/toolchains/dc/kos-built/environ.sh; make -j8 -C /src BUILD_ROOT=/out OBJ_ROOT=/native-cache PACKAGE_OBJ_ROOT=/package-cache MODE=' + $Mode
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
    '--mount', "type=bind,source=$nativeObjectRoot,target=/native-cache",
    '--mount', "type=bind,source=$packageObjectRoot,target=/package-cache",
    '--entrypoint', '/bin/bash',
    $localImageId,
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
        '--mount', "type=bind,source=$nativeObjectRoot,target=/native-cache",
        '--mount', "type=bind,source=$packageObjectRoot,target=/package-cache",
        '--entrypoint', '/bin/bash',
        $localImageId,
        '-lc', $innerCommand
    )
}
$commandText = "docker $($dockerArgs -join ' ')"
$dockerExitCode = 0
$previousErrorActionPreference = $ErrorActionPreference
try {
    # Windows PowerShell 5 turns native stderr into NativeCommandError records;
    # container tools such as genromfs use stderr for ordinary verbose output.
    $ErrorActionPreference = 'Continue'
    & docker @dockerArgs 2>&1 | Tee-Object -FilePath $dockerLogPath
    $dockerExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousErrorActionPreference
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
$readelfOutput = @(& docker run --rm --mount "type=bind,source=$buildRoot,target=/out,readonly" --entrypoint $lock['ReadelfPath'] $localImageId -h /out/helengine_dc.elf 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "Pinned sh-elf-readelf failed: $($readelfOutput -join [Environment]::NewLine)"
}
$readelfText = $readelfOutput -join [Environment]::NewLine
if ($readelfText -notmatch 'little endian' -or $readelfText -notmatch 'SuperH') {
    throw "ELF header is not a SuperH little-endian ELF:`n$readelfText"
}
$readelfOutput | Set-Content -LiteralPath (Join-Path $buildRoot 'elf-header.txt') -Encoding UTF8

$elfHash = (Get-FileHash -LiteralPath $elfPath -Algorithm SHA256).Hash.ToLowerInvariant()
# Optimized DWARF can exceed Flycast's 16 MiB whole-file ELF loader limit.
# Keep the complete debugging artifact and package a separate runtime ELF.
$runtimeElfPath = Join-Path $buildRoot 'helengine_dc.runtime.elf'
$objcopyPath = $lock['ReadelfPath'] -replace 'sh-elf-readelf$', 'sh-elf-objcopy'
$objcopyOutput = @(& docker run --rm --mount "type=bind,source=$buildRoot,target=/out" --entrypoint $objcopyPath $localImageId --strip-debug /out/helengine_dc.elf /out/helengine_dc.runtime.elf 2>&1)
if ($LASTEXITCODE -ne 0) { throw "Pinned sh-elf-objcopy failed: $($objcopyOutput -join [Environment]::NewLine)" }
function Get-ElfLoadImageSignature {
    param([string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 52 -or [BitConverter]::ToUInt32($bytes, 0) -ne 0x464c457f -or $bytes[4] -ne 1 -or $bytes[5] -ne 1) { throw "Invalid ELF32 little-endian image: $Path" }
    $entry = [BitConverter]::ToUInt32($bytes, 24)
    $table = [BitConverter]::ToUInt32($bytes, 28)
    $stride = [BitConverter]::ToUInt16($bytes, 42)
    $count = [BitConverter]::ToUInt16($bytes, 44)
    if ($stride -lt 32 -or ([uint64]$table + [uint64]$stride * $count) -gt $bytes.Length) { throw "Invalid ELF program header table: $Path" }
    $lines = @("Entry=$entry")
    for ($index = 0; $index -lt $count; $index++) {
        $offset = [int]($table + $index * $stride)
        if ([BitConverter]::ToUInt32($bytes, $offset) -ne 1) { continue }
        $fileOffset = [BitConverter]::ToUInt32($bytes, $offset + 4)
        $fileSize = [BitConverter]::ToUInt32($bytes, $offset + 16)
        if (([uint64]$fileOffset + $fileSize) -gt $bytes.Length) { throw "ELF LOAD exceeds file: $Path" }
        $metadata = @(for ($field = 8; $field -lt 32; $field += 4) { [BitConverter]::ToUInt32($bytes, $offset + $field) }) -join ','
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $digest = [BitConverter]::ToString($sha.ComputeHash($bytes, [int]$fileOffset, [int]$fileSize)).Replace('-', '').ToLowerInvariant() }
        finally { $sha.Dispose() }
        $lines += "$metadata=$digest"
    }
    return $lines -join "`n"
}
if ((Get-ElfLoadImageSignature $elfPath) -cne (Get-ElfLoadImageSignature $runtimeElfPath)) { throw 'Runtime ELF LOAD segments or entry changed during debug stripping' }
$runtimeElfBytes = (Get-Item -LiteralPath $runtimeElfPath).Length
if ($runtimeElfBytes -gt 16MB -or $runtimeElfBytes -eq 0) { throw "Runtime ELF exceeds Flycast loader limit: $runtimeElfBytes bytes" }
$runtimeElfHash = (Get-FileHash -LiteralPath $runtimeElfPath -Algorithm SHA256).Hash.ToLowerInvariant()
[ordered]@{debug_elf_sha256=$elfHash;runtime_elf_sha256=$runtimeElfHash;debug_bytes=(Get-Item -LiteralPath $elfPath).Length;runtime_bytes=$runtimeElfBytes;load_segments_byte_identical=$true;command="$objcopyPath --strip-debug helengine_dc.elf helengine_dc.runtime.elf"}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $buildRoot 'runtime-elf-manifest.json') -Encoding UTF8
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
$externalFingerprintAfterHash = Get-TextSha256 $externalFingerprintAfterText
if ($externalFingerprintBefore.Hash -ne $externalFingerprintAfterHash) {
    throw "External build inputs changed during the build. Before=$($externalFingerprintBefore.Hash), After=$externalFingerprintAfterHash"
}
Set-Content -LiteralPath (Join-Path $buildRoot 'external-input-hashes.txt') -Value $externalFingerprintAfterLines -Encoding UTF8
$gitCommit = ((Invoke-CheckedNative 'git' @('-c', "safe.directory=$repoRoot", '-C', $repoRoot, 'rev-parse', 'HEAD')) -join '').Trim()
$gitStatus = (Invoke-CheckedNative 'git' @('-c', "safe.directory=$repoRoot", '-C', $repoRoot, 'status', '--porcelain', '--untracked-files=all')) -join "`n"
$gitDirty = if ([string]::IsNullOrWhiteSpace($gitStatus)) { 'false' } else { 'true' }
$statusListingHash = if ([string]::IsNullOrWhiteSpace($gitStatus)) { 'none' } else { Get-TextSha256 $gitStatus }
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
    "DockerLocalImageId=$localImageId",
    "DockerIndexSha256=$($sdkIdentity.IndexSha256)",
    "DockerManifestSha256=$($sdkIdentity.ManifestSha256)",
    "DockerConfigSha256=$($sdkIdentity.ConfigSha256)",
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
    'RuntimeELFRelativePath=helengine_dc.runtime.elf',
    "RuntimeELFSHA256=$runtimeElfHash",
    'RuntimeELFManifest=runtime-elf-manifest.json',
    "ExactDockerCommand=$commandText",
    'ReadelfHeader=elf-header.txt',
    'BuildLog=docker-build.log'
)
Set-Content -LiteralPath (Join-Path $buildRoot 'build-manifest.txt') -Value $manifestLines -Encoding UTF8
Set-Content -LiteralPath (Join-Path $buildRoot 'build-command.txt') -Value $commandText -Encoding UTF8
Write-Output "Built $elfPath"
Write-Output "SHA256 $elfHash"
