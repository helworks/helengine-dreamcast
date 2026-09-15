[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string]$BuildId,
    [Parameter(Mandatory = $true)]
    [string]$FlycastPath,
    [string]$OutputRoot,
    [switch]$Visible
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$externalRoot = [IO.Path]::GetFullPath('C:\dev\helworks\builds\helengine-dc')
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = $externalRoot
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$repoPrefix = $repoRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if ($OutputRoot.Equals($repoRoot, [StringComparison]::OrdinalIgnoreCase) -or $OutputRoot.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be outside the source worktree: $OutputRoot"
}
$FlycastPath = [IO.Path]::GetFullPath($FlycastPath)
$buildRoot = [IO.Path]::GetFullPath((Join-Path $OutputRoot $BuildId))
$manifestPath = Join-Path $buildRoot 'build-manifest.txt'
if (-not (Test-Path -LiteralPath $buildRoot -PathType Container)) {
    throw "Build directory does not exist: $buildRoot"
}
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Build manifest is missing; refusing to launch: $manifestPath"
}

function Read-KeyValueFile {
    param([string]$Path)
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) { continue }
        $separator = $line.IndexOf('=')
        if ($separator -le 0) { continue }
        $values[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
    }
    return $values
}

$manifest = Read-KeyValueFile $manifestPath
if ($manifest['BuildId'] -ne $BuildId) {
    throw "Manifest BuildId '$($manifest['BuildId'])' does not match requested '$BuildId'."
}
if ($manifest['ELFRelativePath'] -ne 'helengine_dc.elf') {
    throw "Manifest ELF path is not the supported direct ELF name."
}
$elfPath = Join-Path $buildRoot 'helengine_dc.elf'
if (-not (Test-Path -LiteralPath $elfPath -PathType Leaf)) {
    throw "ELF is missing; refusing to launch: $elfPath"
}
$actualHash = (Get-FileHash -LiteralPath $elfPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $manifest['ELFSHA256'].ToLowerInvariant()) {
    throw "ELF SHA-256 mismatch; refusing to launch. Expected $($manifest['ELFSHA256']), found $actualHash."
}
if (-not (Test-Path -LiteralPath $FlycastPath -PathType Leaf)) {
    throw "Flycast executable does not exist: $FlycastPath"
}
$lockPath = Join-Path $repoRoot 'toolchain-lock.txt'
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "Toolchain lock is missing: $lockPath"
}
$lock = Read-KeyValueFile $lockPath
$actualFlycastHash = (Get-FileHash -LiteralPath $FlycastPath -Algorithm SHA256).Hash.ToLowerInvariant()
if (-not [string]::IsNullOrWhiteSpace($lock['FlycastSha256']) -and $actualFlycastHash -ne $lock['FlycastSha256'].ToLowerInvariant()) {
    throw "Flycast SHA-256 mismatch; refusing to launch. Expected $($lock['FlycastSha256']), found $actualFlycastHash."
}
if ($lock['FlycastVersion'] -ne '2.7') {
    throw "Unsupported Flycast version in toolchain lock: $($lock['FlycastVersion'])"
}

$profileRoot = Join-Path $buildRoot 'flycast-profile'
New-Item -ItemType Directory -Path $profileRoot -Force | Out-Null
$profileExe = Join-Path $profileRoot 'flycast.exe'
Copy-Item -LiteralPath $FlycastPath -Destination $profileExe -Force
$flycastData = Join-Path (Split-Path $FlycastPath -Parent) 'data'
if (Test-Path -LiteralPath $flycastData -PathType Container) {
    Copy-Item -LiteralPath $flycastData -Destination (Join-Path $profileRoot 'data') -Recurse -Force
}

$stdoutPath = Join-Path $buildRoot 'emulator-stdout.log'
$stderrPath = Join-Path $buildRoot 'emulator-stderr.log'
$quotedElfPath = '"' + $elfPath.Replace('"', '\"') + '"'
$argumentText = '-config "config:UseReios=yes" -config "log:LogToFile=yes" ' + $quotedElfPath
Set-Content -LiteralPath (Join-Path $buildRoot 'emulator-command.txt') -Value ("$profileExe $argumentText") -Encoding UTF8
Set-Content -LiteralPath (Join-Path $buildRoot 'emulator-manifest.txt') -Value @(
    'LauncherVersion=1',
    "BuildId=$BuildId",
    "ELFSHA256=$actualHash",
    'Route=Flycast 2.7 direct ELF with HLE BIOS',
    "FlycastExecutable=$FlycastPath",
    "FlycastSha256=$actualFlycastHash",
    "FlycastProfile=$profileRoot",
    'FlycastLog=flycast.log',
    'StdoutLog=emulator-stdout.log',
    'StderrLog=emulator-stderr.log'
) -Encoding UTF8

$windowStyle = if ($Visible) { 'Normal' } else { 'Hidden' }
$process = Start-Process -FilePath $profileExe -WorkingDirectory $profileRoot -ArgumentList $argumentText -WindowStyle $windowStyle -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru -Wait
if ($process.ExitCode -ne 0) {
    throw "Flycast exited with code $($process.ExitCode). See $stdoutPath and $stderrPath"
}
Write-Output "Flycast completed for $BuildId. Logs: $stdoutPath, $stderrPath"
