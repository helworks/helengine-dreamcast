[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$FixtureRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,

    [Parameter()]
    [string]$EngineRoot = "C:\dev\helworks\helengine",

    [Parameter()]
    [string]$BuilderAssemblyPath = "",

    [Parameter()]
    [string]$CodegenToolPath = "C:\dev\helworks\csharpcodegen\codegen\bin\Release\net9.0\codegen.exe",

    [Parameter()]
    [string]$EngineVersion = "0.4.0",

    [Parameter()]
    [string]$Platform = "dc",

    [Parameter()]
    [string]$Configuration = "Debug",

    [Parameter()]
    [string]$BuildProfile = "release",

    [Parameter()]
    [switch]$Build
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$CanonicalFixtureRoot = [System.IO.Path]::GetFullPath($FixtureRoot)
$CanonicalOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$CanonicalEngineRoot = [System.IO.Path]::GetFullPath($EngineRoot)
$ScriptRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$FixtureProjectPath = Join-Path $CanonicalFixtureRoot "project.heproj"
$BuildPlatformScript = Join-Path $CanonicalEngineRoot "scripts\build-platform.ps1"
$SettingsRoot = Join-Path $CanonicalOutputRoot "engine-user-settings"
$PlatformsPath = Join-Path $SettingsRoot "platforms.json"
$BuildOutputRoot = Join-Path $CanonicalOutputRoot "build"
$CacheRoot = Join-Path $CanonicalOutputRoot "cache"
$LogPath = Join-Path $CanonicalOutputRoot "authored-build.log"

if ([string]::IsNullOrWhiteSpace($BuilderAssemblyPath)) {
    $BuilderAssemblyPath = Join-Path $CanonicalOutputRoot "dotnet\bin\helengine.dreamcast.builder\debug\helengine.dreamcast.builder.dll"
}
$CanonicalBuilderAssemblyPath = [System.IO.Path]::GetFullPath($BuilderAssemblyPath)
$CanonicalCodegenToolPath = [System.IO.Path]::GetFullPath($CodegenToolPath)

foreach ($RequiredPath in @(
        @($FixtureProjectPath, "fixture project"),
        @($BuildPlatformScript, "generic build wrapper"),
        @($CanonicalBuilderAssemblyPath, "Dreamcast builder assembly"),
        @($CanonicalCodegenToolPath, "codegen tool"))) {
    if (-not (Test-Path -LiteralPath $RequiredPath[0] -PathType Leaf)) {
        throw "Required $($RequiredPath[1]) was not found at '$($RequiredPath[0])'."
    }
}

New-Item -ItemType Directory -Path $CanonicalOutputRoot -Force | Out-Null
New-Item -ItemType Directory -Path $SettingsRoot -Force | Out-Null

[ordered]@{
    platforms = @(
        [ordered]@{
            engineVersion = $EngineVersion
            platformId = $Platform
            displayName = "Sega Dreamcast"
            builderAssemblyPath = $CanonicalBuilderAssemblyPath
            playerSourceRootPath = $ScriptRoot
            codegenToolPath = $CanonicalCodegenToolPath
        }
    )
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $PlatformsPath -Encoding UTF8

Write-Output "Fixture: $CanonicalFixtureRoot"
Write-Output "Platforms: $PlatformsPath"
Write-Output "Builder: $CanonicalBuilderAssemblyPath"
Write-Output "Codegen: $CanonicalCodegenToolPath"

if (-not $Build) {
    exit 0
}

$OriginalEngineUserSettingsRoot = $env:HELENGINE_ENGINE_USER_SETTINGS_ROOT
$OriginalRepositoryRoot = $env:HELENGINE_DC_REPOSITORY_ROOT
$OriginalPowerShell = $env:HELENGINE_DC_POWERSHELL
try {
    $env:HELENGINE_ENGINE_USER_SETTINGS_ROOT = $SettingsRoot
    # The native executor runs this checkout's scripts/build.ps1 with the same
    # PowerShell 7 host that runs this script; pwsh is not assumed on PATH.
    $env:HELENGINE_DC_REPOSITORY_ROOT = $ScriptRoot
    $env:HELENGINE_DC_POWERSHELL = (Get-Process -Id $PID).Path
    Write-Output "RepositoryRoot: $($env:HELENGINE_DC_REPOSITORY_ROOT)"
    Write-Output "NativePowerShell: $($env:HELENGINE_DC_POWERSHELL)"
    $InvocationOutput = @(& $BuildPlatformScript `
        -Project $FixtureProjectPath `
        -Platform $Platform `
        -Output $BuildOutputRoot `
        -Configuration $Configuration `
        -BuildProfile $BuildProfile `
        -CacheRoot $CacheRoot 2>&1 | Tee-Object -FilePath $LogPath)
    $InvocationExitCode = $LASTEXITCODE
    $InvocationOutput | ForEach-Object { Write-Output $_ }
    Write-Output "BuildExitCode: $InvocationExitCode"
    exit $InvocationExitCode
} finally {
    $env:HELENGINE_ENGINE_USER_SETTINGS_ROOT = $OriginalEngineUserSettingsRoot
    $env:HELENGINE_DC_REPOSITORY_ROOT = $OriginalRepositoryRoot
    $env:HELENGINE_DC_POWERSHELL = $OriginalPowerShell
}
