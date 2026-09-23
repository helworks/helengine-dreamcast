$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildScript = Join-Path $repoRoot 'scripts\build.ps1'
$launcherScript = Join-Path $repoRoot 'scripts\launch_in_emulator.ps1'
$powershellPath = (Get-Process -Id $PID).Path
$testRoot = Join-Path $repoRoot '.dreamcast\tests\output-root-policy'

function Invoke-ExpectedFailure {
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string[]]$Arguments,
        [string]$Message
    )

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& $powershellPath -NoProfile -ExecutionPolicy Bypass -File $ScriptPath @Arguments 2>&1)
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($LASTEXITCODE -eq 0) {
        throw "$Name unexpectedly succeeded."
    }
    $joined = $output -join [Environment]::NewLine
    if ($joined -notmatch [Regex]::Escape($Message)) {
        throw "$Name failed with an unexpected message:`n$joined"
    }
}

try {
    Invoke-ExpectedFailure `
        -Name 'build output under .dreamcast' `
        -ScriptPath $buildScript `
        -Arguments @('-BuildId', 'missing-authored-roots', '-Mode', 'Authored', '-OutputRoot', $testRoot) `
        -Message 'Authored mode requires GeneratedCoreRoot, ContentRoot and CatalogRoot.'

    Invoke-ExpectedFailure `
        -Name 'build output elsewhere in the worktree' `
        -ScriptPath $buildScript `
        -Arguments @('-BuildId', 'reject-worktree-output', '-Mode', 'Diagnostic', '-OutputRoot', (Join-Path $repoRoot 'scripts')) `
        -Message 'OutputRoot must be outside the source worktree or inside .dreamcast:'

    Invoke-ExpectedFailure `
        -Name 'emulator output under .dreamcast' `
        -ScriptPath $launcherScript `
        -Arguments @('-BuildId', 'missing-build-directory', '-FlycastPath', (Join-Path $testRoot 'flycast.exe'), '-OutputRoot', $testRoot) `
        -Message 'Build directory does not exist:'

    Invoke-ExpectedFailure `
        -Name 'emulator output elsewhere in the worktree' `
        -ScriptPath $launcherScript `
        -Arguments @('-BuildId', 'reject-worktree-output', '-FlycastPath', (Join-Path $testRoot 'flycast.exe'), '-OutputRoot', (Join-Path $repoRoot 'scripts')) `
        -Message 'OutputRoot must be outside the source worktree or inside .dreamcast:'

    Write-Output 'output-root-policy.tests.ps1: PASS'
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
