[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string]$BuildId,
    [string]$DockerImage = 'helengine-dc-sdk:v01',
    [string]$KosPath = 'C:\dev\helworks\builds\helengine-dc\dependencies\kos',
    [string]$OutputRoot,
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
$externalRoot = [IO.Path]::GetFullPath('C:\dev\helworks\builds\helengine-dc')
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = $externalRoot
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$KosPath = [IO.Path]::GetFullPath($KosPath)
$repoPrefix = $repoRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if ($OutputRoot.Equals($repoRoot, [StringComparison]::OrdinalIgnoreCase) -or $OutputRoot.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be outside the source worktree: $OutputRoot"
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

if (Test-Path -LiteralPath $buildRoot) {
    throw "Refusing to reuse existing build directory: $buildRoot"
}
if (-not (Test-Path -LiteralPath $KosPath -PathType Container)) {
    throw "KOS checkout does not exist: $KosPath"
}
if (-not (Test-Path -LiteralPath $repoRoot -PathType Container)) {
    throw "Source root does not exist: $repoRoot"
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
$innerCommand = 'set -e; actual=$(git -C /opt/toolchains/dc/kos rev-parse HEAD); test "$actual" = "' + $expectedKosCommit + '"; export KOS_BASE=/opt/toolchains/dc/kos-built; source /opt/toolchains/dc/kos-built/environ.sh; make -j8 -C /src BUILD_ROOT=/out'
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
$commandText = "docker $($dockerArgs -join ' ')"
$dockerOutput = @()
$dockerExitCode = 0
try {
    $dockerOutput = @(& docker @dockerArgs 2>&1)
    $dockerExitCode = $LASTEXITCODE
}
finally {
    if ($dockerOutput.Count -gt 0) {
        $dockerOutput | Set-Content -LiteralPath $dockerLogPath -Encoding UTF8
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
$gitCommit = ((Invoke-CheckedNative 'git' @('-c', "safe.directory=$repoRoot", '-C', $repoRoot, 'rev-parse', 'HEAD')) -join '').Trim()
$gitStatus = (Invoke-CheckedNative 'git' @('-c', "safe.directory=$repoRoot", '-C', $repoRoot, 'status', '--porcelain', '--untracked-files=all')) -join "`n"
$gitDirty = if ([string]::IsNullOrWhiteSpace($gitStatus)) { 'false' } else { 'true' }
$statusHash = if ([string]::IsNullOrWhiteSpace($gitStatus)) { 'none' } else { [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($gitStatus))).ToLowerInvariant() }
$manifestLines = @(
    'ManifestVersion=1',
    "BuildId=$BuildId",
    "SourceRoot=$repoRoot",
    "GitCommit=$gitCommit",
    "GitDirty=$gitDirty",
    "GitStatusSha256=$statusHash",
    "KosCommit=$expectedKosCommit",
    "DockerImage=$DockerImage",
    "DockerImageId=$actualImageId",
    "UpstreamBaseImage=$($lock['UpstreamBaseImage'])",
    "CompilerFlags=$($lock['CompilerFlags'])",
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
