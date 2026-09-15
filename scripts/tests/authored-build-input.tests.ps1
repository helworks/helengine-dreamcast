$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$buildScript = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\build.ps1'))
$testRoot = [IO.Path]::GetFullPath((Join-Path 'C:\dev\helworks\builds\helengine-dc\v02\execution' ("authored-build-input-tests-{0}" -f $PID)))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null

function Invoke-ExpectedFailure {
    param(
        [string]$Name,
        [string[]]$Arguments,
        [string]$Message
    )
    $output = @(& pwsh -NoProfile -File $buildScript @Arguments 2>&1)
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
        -Name 'missing authored roots' `
        -Arguments @('-BuildId', 'missing-roots', '-Mode', 'Authored', '-OutputRoot', $testRoot) `
        -Message 'Authored mode requires GeneratedCoreRoot, ContentRoot and CatalogRoot.'

    $generatedRoot = Join-Path $testRoot 'generated'
    $contentRoot = Join-Path $testRoot 'content'
    $catalogRoot = Join-Path $testRoot 'catalog'
    New-Item -ItemType Directory -Path $generatedRoot, $contentRoot, $catalogRoot -Force | Out-Null

    Invoke-ExpectedFailure `
        -Name 'missing generated Core' `
        -Arguments @('-BuildId', 'missing-core', '-Mode', 'Authored', '-GeneratedCoreRoot', $generatedRoot, '-ContentRoot', $contentRoot, '-CatalogRoot', $catalogRoot, '-RuntimeInitProbe', '-OutputRoot', $testRoot) `
        -Message 'Generated Core root does not contain Core.hpp'

    Set-Content -LiteralPath (Join-Path $generatedRoot 'Core.hpp') -Value '#pragma once' -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $catalogRoot 'fake.catalog') -Value 'fake' -Encoding UTF8
    Invoke-ExpectedFailure `
        -Name 'missing probe catalog ABI' `
        -Arguments @('-BuildId', 'missing-probe-catalog', '-Mode', 'Authored', '-GeneratedCoreRoot', $generatedRoot, '-ContentRoot', $contentRoot, '-CatalogRoot', $catalogRoot, '-RuntimeInitProbe', '-OutputRoot', $testRoot) `
        -Message 'RuntimeInitProbe requires the dedicated Task 4 catalog ABI file'

    Invoke-ExpectedFailure `
        -Name 'non-probe authored catalog' `
        -Arguments @('-BuildId', 'non-probe-authored', '-Mode', 'Authored', '-GeneratedCoreRoot', $generatedRoot, '-ContentRoot', $contentRoot, '-CatalogRoot', $catalogRoot, '-OutputRoot', $testRoot) `
        -Message 'Authored scene loading is not available until the Task 4 startup catalog is connected'

    Set-Content -LiteralPath (Join-Path $catalogRoot 'dc_runtime_scene_catalog.hpp') -Value @'
#pragma once
#include <cstddef>
#include "platform/dreamcast/DreamcastRuntimeSceneCatalog.hpp"
'@ -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $catalogRoot 'dc_runtime_scene_catalog.cpp') -Value @'
#include "dc_runtime_scene_catalog.hpp"
extern "C" const char* he_dc_startup_scene_id() { return nullptr; }
extern "C" const HEDcSceneCatalogEntry* he_dc_scene_catalog_entries(std::size_t* count) {
    if (count != nullptr) { *count = 0; }
    return nullptr;
}
'@ -Encoding UTF8

    Invoke-ExpectedFailure `
        -Name 'native compiler failure' `
        -Arguments @('-BuildId', 'compiler-failure', '-Mode', 'Authored', '-GeneratedCoreRoot', $generatedRoot, '-ContentRoot', $contentRoot, '-CatalogRoot', $catalogRoot, '-RuntimeInitProbe', '-OutputRoot', $testRoot) `
        -Message 'Docker build failed with exit code'

    Invoke-ExpectedFailure `
        -Name 'diagnostic validation' `
        -Arguments @('-BuildId', 'bad-image', '-Mode', 'Diagnostic', '-DockerImage', 'not-the-locked-image', '-OutputRoot', $testRoot) `
        -Message "not the locked tag"

    foreach ($buildId in @('missing-roots', 'missing-core', 'missing-probe-catalog', 'non-probe-authored', 'compiler-failure', 'bad-image')) {
        $manifest = Join-Path (Join-Path $testRoot $buildId) 'build-manifest.txt'
        if (Test-Path -LiteralPath $manifest) {
            throw "Failed build $buildId left a success manifest: $manifest"
        }
    }

    $makefile = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\..\Makefile') -Raw
    if ($makefile -notmatch '(?m)^MODE \?= Diagnostic\r?$' -or
        $makefile -notmatch 'MODE.*Authored' -or
        $makefile -notmatch 'GENERATED_CORE_ROOT') {
        throw 'Makefile does not preserve the Diagnostic default and Authored input gate.'
    }
    Write-Output 'authored-build-input.tests.ps1: PASS'
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
