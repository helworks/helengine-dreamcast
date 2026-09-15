[CmdletBinding()]
param(
    [string]$KosPath = 'C:\dev\helworks\builds\helengine-dc\dependencies\kos',
    [ValidateSet('helengine-dc-sdk:v01')]
    [string]$ImageTag = 'helengine-dc-sdk:v01'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$expectedKosCommit = 'fcfa7d869471591ca1c777543261a7bfea7cb726'
$expectedBaseImage = 'pcercuei/dreamcast-toolchain@sha256:504ed29fad5e5565579bf00235fc011fdde656004e50bde4aa24a93b59e2a7bb'
$expectedCompilerAbi = 'sh-elf little-endian -m4-single'
$expectedFlycastVersion = '2.7'
$expectedFlycastPath = 'C:\dev\helworks\builds\helengine-dc\dependencies\flycast-2.7\flycast.exe'
$expectedFlycastSha256 = 'f41c1a4243274caf9a183114bd610407835521a011a48b18326e5e957e6f4e6b'
$scriptRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$externalRoot = [IO.Path]::GetFullPath('C:\dev\helworks\builds\helengine-dc')
$KosPath = [IO.Path]::GetFullPath($KosPath)
$lockPath = Join-Path $scriptRoot 'toolchain-lock.txt'
$dockerfilePath = Join-Path $scriptRoot 'docker\Dockerfile'

function Invoke-CheckedNative {
    param([string]$FilePath, [string[]]$ArgumentList)
    $output = & $FilePath @ArgumentList 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Command failed ($exitCode): $FilePath $($ArgumentList -join ' ')`n$($output -join [Environment]::NewLine)"
    }
    return @($output)
}

if (-not (Test-Path -LiteralPath $KosPath -PathType Container)) {
    throw "KOS checkout does not exist: $KosPath"
}
if (-not (Test-Path -LiteralPath $dockerfilePath -PathType Leaf)) {
    throw "Dockerfile does not exist: $dockerfilePath"
}

$actualKosCommit = ((Invoke-CheckedNative 'git' @('-c', "safe.directory=$KosPath", '-C', $KosPath, 'rev-parse', 'HEAD')) -join '').Trim()
if ($actualKosCommit -ne $expectedKosCommit) {
    throw "KOS commit mismatch. Expected $expectedKosCommit, found $actualKosCommit."
}

$buildArgs = @(
    'build',
    '--file', $dockerfilePath,
    '--tag', $ImageTag,
    '--build-arg', "KOS_COMMIT=$expectedKosCommit",
    '--build-arg', 'KOS_REPOSITORY=https://github.com/KallistiOS/KallistiOS.git',
    $scriptRoot
)
$buildOutput = @()
try {
    $buildOutput = Invoke-CheckedNative 'docker' $buildArgs
}
finally {
    if ($buildOutput.Count -gt 0) {
        $buildOutput | Write-Output
    }
}

$imageId = ((Invoke-CheckedNative 'docker' @('image', 'inspect', $ImageTag, '--format', '{{.Id}}')) -join '').Trim()
if ($imageId -notmatch '^sha256:[0-9a-f]{64}$') {
    throw "Docker returned an invalid image ID for ${ImageTag}: $imageId"
}
$baseLabel = ((Invoke-CheckedNative 'docker' @('image', 'inspect', $ImageTag, '--format', '{{index .Config.Labels "org.helengine.dreamcast.upstream-base"}}')) -join '').Trim()
$kosLabel = ((Invoke-CheckedNative 'docker' @('image', 'inspect', $ImageTag, '--format', '{{index .Config.Labels "org.helengine.dreamcast.kos-commit"}}')) -join '').Trim()
$abiLabel = ((Invoke-CheckedNative 'docker' @('image', 'inspect', $ImageTag, '--format', '{{index .Config.Labels "org.helengine.dreamcast.compiler-abi"}}')) -join '').Trim()
if ($baseLabel -ne $expectedBaseImage) {
    throw "SDK image base metadata mismatch. Expected $expectedBaseImage, found $baseLabel"
}
if ($kosLabel -ne $expectedKosCommit) {
    throw "SDK image KOS metadata mismatch. Expected $expectedKosCommit, found $kosLabel"
}
if ($abiLabel -ne $expectedCompilerAbi) {
    throw "SDK image ABI metadata mismatch. Expected $expectedCompilerAbi, found $abiLabel"
}

$metadataScript = @'
set -e
source /opt/toolchains/dc/kos/environ.sh
printf 'CompilerC=%s\n' "$(sh-elf-gcc --version | head -n1)"
printf 'CompilerCxx=%s\n' "$(sh-elf-g++ --version | head -n1)"
printf 'ReadelfPath=%s\n' "$(command -v sh-elf-readelf)"
printf 'KosCxxPath=%s\n' "$(command -v kos-c++)"
printf 'KOSBase=%s\n' "$KOS_BASE"
printf 'KosCommit=%s\n' "$(git -C "$KOS_BASE" rev-parse HEAD)"
printf 'CompilerTarget=%s\n' "$(sh-elf-gcc -dumpmachine)"
printf 'CompilerFlags=%s\n' "$KOS_SH4_PRECISION -ml"
'@
$metadataOutput = Invoke-CheckedNative 'docker' @('run', '--rm', '--entrypoint', '/bin/bash', $ImageTag, '-lc', $metadataScript)
$metadata = @{}
foreach ($line in $metadataOutput) {
    $text = [string]$line
    $separator = $text.IndexOf('=')
    if ($separator -gt 0) {
        $metadata[$text.Substring(0, $separator)] = $text.Substring($separator + 1)
    }
}
if ($metadata['KosCommit'] -ne $expectedKosCommit) {
    throw "SDK image KOS commit mismatch: $($metadata['KosCommit'])"
}
if ($metadata['CompilerTarget'] -ne 'sh-elf') {
    throw "SDK compiler target mismatch: $($metadata['CompilerTarget'])"
}
if ($metadata['CompilerFlags'] -ne '-m4-single -ml') {
    throw "SDK compiler flags mismatch: $($metadata['CompilerFlags'])"
}

if (-not (Test-Path -LiteralPath $expectedFlycastPath -PathType Leaf)) {
    throw "Pinned Flycast $expectedFlycastVersion executable is required: $expectedFlycastPath"
}
$flycastHash = (Get-FileHash -LiteralPath $expectedFlycastPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($flycastHash -ne $expectedFlycastSha256) {
    throw "Flycast hash mismatch. Expected $expectedFlycastSha256, found $flycastHash."
}

$lockLines = @(
    '# Generated by scripts/prepare_toolchain.ps1. Review changes before use.',
    'LockFormat=1',
    "LocalImageTag=$ImageTag",
    "LocalImageId=$imageId",
    "UpstreamBaseImage=$expectedBaseImage",
    "KosRepository=https://github.com/KallistiOS/KallistiOS.git",
    "KosCommit=$expectedKosCommit",
    "CompilerC=$($metadata['CompilerC'])",
    "CompilerCxx=$($metadata['CompilerCxx'])",
    "ReadelfPath=$($metadata['ReadelfPath'])",
    "KosCxxPath=$($metadata['KosCxxPath'])",
    "KOS_BASE=$($metadata['KOSBase'])",
    "CompilerFlags=$($metadata['CompilerFlags'])",
    'Endian=sh-elf little-endian',
    'Architecture=SuperH SH-4',
    "FlycastVersion=$expectedFlycastVersion",
    "FlycastPath=$expectedFlycastPath",
    "FlycastSha256=$flycastHash"
)
Set-Content -LiteralPath $lockPath -Value $lockLines -Encoding UTF8
Write-Output "Prepared $ImageTag ($imageId) with KOS $expectedKosCommit."
