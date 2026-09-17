[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildRoot,
    [Parameter(Mandatory = $true)]
    [string]$FlycastPath,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [string]$DockerImage = 'helengine-dc-sdk:v01',
    # Comma-separated loop frames to capture; a string so `pwsh -File` callers can pass it.
    [string]$CaptureFrames = '120,180',
    [ValidateRange(1, 100000)]
    [int]$StartAfterFrames = 240,
    [ValidateRange(60, 200000)]
    [int]$TimeoutVblanks = 1800,
    # Minimum directional lights the authored snapshot must report for a PASS.
    # Default 0 keeps unlit scenes verifiable; lit scenes should pass 1 or more.
    [ValidateRange(0, 255)]
    [int]$MinimumLights = 0,
    [switch]$NoController,
    [ValidateRange(10, 3600)]
    [int]$WaitSeconds = 300
)

# Verifies one authored Dreamcast build in a hidden, bounded Flycast session:
# manifest and ELF hash first, per-ELF symbol resolution with the pinned
# toolchain, runtime/authored snapshot observation, raw RGB565 framebuffer
# captures through the 32-bit VRAM aperture, optional Start injection and a
# clean-shutdown requirement. It never launches a visible window.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
[int[]]$captureFrameList = @($CaptureFrames -split '[,; ]+' | Where-Object { $_ -ne '' } | ForEach-Object { [int]$_ })
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$FlycastPath = [IO.Path]::GetFullPath($FlycastPath)

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

function Invoke-Docker {
    param([string[]]$ArgumentList)
    $output = & docker @ArgumentList 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "docker $($ArgumentList -join ' ') failed ($LASTEXITCODE): $($output -join [Environment]::NewLine)"
    }
    return @($output)
}

# --- Manifest, ELF and toolchain identity -----------------------------------
$manifestPath = Join-Path $BuildRoot 'build-manifest.txt'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Build manifest is missing; refusing to verify: $manifestPath"
}
$manifest = Read-KeyValueFile $manifestPath
$buildId = $manifest['BuildId']
if ([string]::IsNullOrWhiteSpace($buildId) -or (Split-Path $BuildRoot -Leaf) -ne $buildId) {
    throw "Manifest BuildId '$buildId' does not match the build directory name."
}
if ($manifest['Mode'] -ne 'Authored') {
    throw "Manifest Mode '$($manifest['Mode'])' is not an Authored build."
}
if ($manifest['RuntimeInitProbe'] -ne 'False') {
    throw 'RuntimeInitProbe builds are compatibility probes, not accepted authored builds.'
}
if ($manifest['ELFRelativePath'] -ne 'helengine_dc.elf') {
    throw 'Manifest ELF path is not the supported direct ELF name.'
}
$elfPath = Join-Path $BuildRoot 'helengine_dc.elf'
if (-not (Test-Path -LiteralPath $elfPath -PathType Leaf)) {
    throw "ELF is missing; refusing to verify: $elfPath"
}
$elfHash = (Get-FileHash -LiteralPath $elfPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($elfHash -ne $manifest['ELFSHA256'].ToLowerInvariant()) {
    throw "ELF SHA-256 mismatch. Expected $($manifest['ELFSHA256']), found $elfHash."
}
$lock = Read-KeyValueFile (Join-Path $repoRoot 'toolchain-lock.txt')
if ($DockerImage -ne $lock['LocalImageTag']) {
    throw "Docker image '$DockerImage' is not the locked tag '$($lock['LocalImageTag'])'."
}
$imageId = ((Invoke-Docker @('image', 'inspect', $DockerImage, '--format', '{{.Id}}')) -join '').Trim()
if ($imageId -ne $lock['LocalImageId']) {
    throw "SDK image ID mismatch. Expected $($lock['LocalImageId']), found $imageId."
}
if (-not (Test-Path -LiteralPath $FlycastPath -PathType Leaf)) {
    throw "Flycast executable does not exist: $FlycastPath"
}
$flycastHash = (Get-FileHash -LiteralPath $FlycastPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($flycastHash -ne $lock['FlycastSha256'].ToLowerInvariant()) {
    throw "Flycast SHA-256 mismatch. Expected $($lock['FlycastSha256']), found $flycastHash."
}

if (Test-Path -LiteralPath $EvidenceRoot) {
    if (Get-ChildItem -LiteralPath $EvidenceRoot -Force | Select-Object -First 1) {
        throw "Evidence root must be fresh and empty: $EvidenceRoot"
    }
}
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null

# --- Per-ELF symbol resolution with the pinned toolchain ----------------------
$nmOutput = Invoke-Docker @(
    'run', '--rm',
    '--mount', "type=bind,source=$BuildRoot,target=/out,readonly",
    '--entrypoint', '/opt/toolchains/dc/sh-elf/bin/sh-elf-nm',
    $DockerImage, '/out/helengine_dc.elf')
$symbols = @{}
foreach ($line in $nmOutput) {
    if ($line -match '^([0-9a-fA-F]{8})\s+[A-Za-z]\s+_(helengine_dreamcast_(?:runtime_probe|authored_snapshot|diagnostics|authored_scene_ready|authored_frame_count))$') {
        $symbols[$Matches[2]] = $Matches[1].ToLowerInvariant()
    }
}
foreach ($required in @('helengine_dreamcast_runtime_probe', 'helengine_dreamcast_authored_snapshot', 'helengine_dreamcast_diagnostics', 'helengine_dreamcast_authored_scene_ready', 'helengine_dreamcast_authored_frame_count')) {
    if (-not $symbols.ContainsKey($required)) {
        throw "Pinned nm did not resolve required symbol '$required'."
    }
}
$nmOutput | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'nm-symbols.txt') -Encoding UTF8

# --- Lua probe -----------------------------------------------------------------
$captureTable = ($captureFrameList | ForEach-Object { "[$_] = true" }) -join ', '
$luaTemplate = @'
local root = "@@ROOT@@"
local out = io.open(root .. "verify-probe.csv", "wb")
local probeAddress = @@PROBE@@
local authoredAddress = @@AUTHORED@@
local diagAddress = @@DIAG@@
local sceneReadyAddress = @@SCENE_READY@@
local frameCountAddress = @@FRAME_COUNT@@
local captureFrames = { @@CAPTURES@@ }
local startAfterFrames = @@START_AFTER@@
local timeoutVblanks = @@TIMEOUT_VBLANKS@@
local minimumLights = @@MINIMUM_LIGHTS@@
local injectStart = @@INJECT_START@@
local minimumFreeHeap = 4194304
local startButton = 8
local PVR = 0xA05F8000
local VRAM32 = 0x05000000
local frame = 0
local startPressedAt = nil
local startReleased = false
local captured = {}
local lastLoggedCount = -1
local sceneReadyLogged = false
local firstFrameLogged = false

local function line(s)
  print("VERIFY_PROBE " .. s)
  if out then out:write(s .. "\n"); out:flush() end
end

local function readWords(address, count)
  local v = {}
  for i = 0, count - 1 do v[i + 1] = flycast.memory.read32(address + i * 4) end
  return v
end

local function runtimeText(v)
  return string.format("rt_magic=%08x rt_status=%u rt_error=%u rt_stage=%u startFree=%u coreReadyFree=%u coreDisposedFree=%u shutdownFree=%u checkedFree=%u coreReady=%u frameComplete=%u coreDisposed=%u cleanShutdown=%u",
    v[1], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[16], v[17], v[18], v[19], v[20])
end

local function authoredText(a)
  return string.format("au_magic=%08x au_version=%u au_stage=%u au_error=%u sceneDigest=%08x cameras=%u drawables=%u tris=%u clipped=%u rejected=%u presented=%u firstPresented=%u clearArgb=%08x au_free=%u loopFrames=%u lights=%u",
    a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15], a[16])
end

local function diagText(d)
  return string.format("diag_status=%u diag_error=%u diag_frame=%u diag_tris=%u diag_verts=%u diag_clipped=%u diag_vram=%u", d[3], d[4], d[5], d[6], d[7], d[8], d[17])
end

local function dumpRaw(path, base, bytes)
  local f = io.open(path, "wb")
  if not f then line("raw_open_failed path=" .. path); return 0, 0 end
  local words = math.floor(bytes / 4)
  local t = flycast.memory.readTable32(base, words)
  local nonzero, sum = 0, 0
  for i = 0, words - 1 do
    local v = t[base + i * 4] or 0
    local b0 = v % 256
    local b1 = math.floor(v / 256) % 256
    local b2 = math.floor(v / 65536) % 256
    local b3 = math.floor(v / 16777216) % 256
    if b0 ~= 0 then nonzero = nonzero + 1 end
    if b1 ~= 0 then nonzero = nonzero + 1 end
    if b2 ~= 0 then nonzero = nonzero + 1 end
    if b3 ~= 0 then nonzero = nonzero + 1 end
    sum = (sum + b0 + b1 + b2 + b3) % 4294967296
    f:write(string.char(b0, b1, b2, b3))
  end
  f:close()
  return nonzero, sum
end

local function captureFramebuffer(loopFrame)
  local rsize = flycast.memory.read32(PVR + 0x5C)
  local spg = flycast.memory.read32(PVR + 0xD0)
  local sof1 = flycast.memory.read32(PVR + 0x50)
  local sof2 = flycast.memory.read32(PVR + 0x54)
  local wsof1 = flycast.memory.read32(PVR + 0x60)
  local width = ((rsize % 1024) + 1) * 2
  local height = (math.floor(rsize / 1024) % 1024) + 1
  local modulus = (math.floor(rsize / 1048576) % 1024 - 1) * 2
  local interlace = math.floor(spg / 16) % 2 == 1
  if interlace and width == modulus and sof2 == sof1 + width * 2 then modulus = 0; height = height * 2 end
  local stride = width + modulus
  local base = VRAM32 + wsof1
  local function pixel(x, y) return flycast.memory.read16(base + (y * stride + x) * 2) end
  local center = pixel(math.floor(width / 2), math.floor(height / 2))
  local corner = pixel(4, 4)
  local bytes = math.min(stride * height * 2, 1048576)
  local path = root .. string.format("frame-%04d.raw", loopFrame)
  local nonzero, sum = dumpRaw(path, base, bytes)
  line(string.format("capture loopFrame=%u vblank=%d width=%u height=%u stride=%u wsof1=%08x center=%04x corner=%04x bytes=%u nonzero=%u sum=%u path=%s",
    loopFrame, frame, width, height, stride, wsof1, center, corner, bytes, nonzero, sum, path))
end

local function finish(kind, v, a, d, ready, count)
  line(string.format("%s vblank=%d %s %s %s sceneReady=%u frameCount=%u", kind, frame, runtimeText(v), authoredText(a), diagText(d), ready, count))
  flycast.emulator.exit()
end

local function cbStart()
  local s = flycast.state
  line(string.format("start media=%s display=%dx%d", tostring(s.media), s.display.width, s.display.height))
end

local function cbVBlank()
  frame = frame + 1
  local v = readWords(probeAddress, 20)
  local a = readWords(authoredAddress, 16)
  local d = readWords(diagAddress, 20)
  local ready = flycast.memory.read32(sceneReadyAddress)
  local count = flycast.memory.read32(frameCountAddress)
  -- Authored snapshot version 2 added directionalLightCount. Pinned to the
  -- current kDreamcastAuthoredSnapshotVersion so a stale ELF fails loudly
  -- rather than being read against the wrong layout.
  local magicOk = v[1] == 0x48455250 and a[1] == 0x48454153
  local valid = magicOk and v[2] == 1 and a[2] == 2
  if frame == 1 then line(string.format("vblank=%d %s %s", frame, runtimeText(v), authoredText(a))) end
  -- Both magics present but a version we cannot read means a stale ELF. Fail on
  -- the spot: waiting for the timeout would hide the real reason, and every
  -- other predicate below is gated on `valid` so nothing would ever report.
  if magicOk and not valid then
    finish("version-mismatch", v, a, d, ready, count)
    return
  end
  if valid and (v[3] == 0xffffffff or a[4] ~= 0) then finish("error", v, a, d, ready, count); return end
  if valid and ready == 1 and not sceneReadyLogged then
    sceneReadyLogged = true
    line(string.format("scene-ready vblank=%d %s %s", frame, runtimeText(v), authoredText(a)))
  end
  if valid and ready == 1 and a[11] > 0 and not firstFrameLogged then
    firstFrameLogged = true
    line(string.format("first-presented vblank=%d %s %s", frame, authoredText(a), diagText(d)))
  end
  if valid and ready == 1 and startPressedAt == nil and count ~= lastLoggedCount and count % 60 == 0 then
    lastLoggedCount = count
    line(string.format("running vblank=%d %s %s %s frameCount=%u", frame, runtimeText(v), authoredText(a), diagText(d), count))
  end
  if valid and ready == 1 and startPressedAt == nil and captureFrames[count] and not captured[count] then
    captured[count] = true
    captureFramebuffer(count)
  end
  if valid and ready == 1 and startPressedAt == nil and count >= startAfterFrames then
    if v[16] < minimumFreeHeap then finish("heap-below-gate", v, a, d, ready, count); return end
    if not injectStart then finish("no-controller-complete", v, a, d, ready, count); return end
    line(string.format("press-start vblank=%d %s %s", frame, authoredText(a), diagText(d)))
    flycast.input.pressButtons(1, startButton)
    startPressedAt = frame
  end
  if startPressedAt ~= nil and not startReleased and frame >= startPressedAt + 20 then
    flycast.input.releaseButtons(1, startButton)
    startReleased = true
    line(string.format("release-start vblank=%d", frame))
  end
  if valid and startPressedAt ~= nil and v[3] == 5 and v[4] == 0 and v[5] == 6 and a[3] == 9 and a[4] == 0 and
      v[17] == 1 and v[18] == 1 and v[19] == 1 and v[20] == 1 and
      v[6] >= minimumFreeHeap and v[7] >= minimumFreeHeap and v[8] >= minimumFreeHeap and v[9] >= minimumFreeHeap then
    -- a[16] is directionalLightCount. Without this gate a lit scene that
    -- gathered no lights at all still reports success, which would leave the
    -- lighting feature with no automated check anywhere.
    if a[16] < minimumLights then
      finish("lights-below-gate", v, a, d, ready, count)
      return
    end
    finish("success", v, a, d, ready, count)
    return
  end
  if frame >= timeoutVblanks then finish("timeout", v, a, d, ready, count) end
end

local function cbTerminate()
  line(string.format("terminate vblanks=%d", frame))
  if out then out:close(); out = nil end
end

flycast_callbacks = { start = cbStart, vblank = cbVBlank, terminate = cbTerminate }
line(string.format("callbacks_ready probe=%08x authored=%08x diag=%08x sceneReady=%08x frameCount=%08x", probeAddress, authoredAddress, diagAddress, sceneReadyAddress, frameCountAddress))
'@

$lua = $luaTemplate.Replace('@@ROOT@@', ($EvidenceRoot.Replace('\', '/') + '/')).
    Replace('@@PROBE@@', '0x' + $symbols['helengine_dreamcast_runtime_probe']).
    Replace('@@AUTHORED@@', '0x' + $symbols['helengine_dreamcast_authored_snapshot']).
    Replace('@@DIAG@@', '0x' + $symbols['helengine_dreamcast_diagnostics']).
    Replace('@@SCENE_READY@@', '0x' + $symbols['helengine_dreamcast_authored_scene_ready']).
    Replace('@@FRAME_COUNT@@', '0x' + $symbols['helengine_dreamcast_authored_frame_count']).
    Replace('@@CAPTURES@@', $captureTable).
    Replace('@@START_AFTER@@', [string]$StartAfterFrames).
    Replace('@@TIMEOUT_VBLANKS@@', [string]$TimeoutVblanks).
    Replace('@@MINIMUM_LIGHTS@@', [string]$MinimumLights).
    Replace('@@INJECT_START@@', $(if ($NoController) { 'false' } else { 'true' }))
$luaPath = Join-Path $EvidenceRoot 'verify-probe.lua'
[IO.File]::WriteAllText($luaPath, $lua, (New-Object System.Text.UTF8Encoding($false)))

# --- Hidden bounded Flycast session --------------------------------------------
$profileExe = Join-Path $EvidenceRoot 'flycast.exe'
Copy-Item -LiteralPath $FlycastPath -Destination $profileExe -Force
$flycastData = Join-Path (Split-Path $FlycastPath -Parent) 'data'
if (Test-Path -LiteralPath $flycastData -PathType Container) {
    Copy-Item -LiteralPath $flycastData -Destination (Join-Path $EvidenceRoot 'data') -Recurse -Force
}
$argumentList = @(
    '-config', 'log:LogToFile=yes',
    '-config', 'config:UseReios=yes',
    '-config', 'config:rend.EmulateFramebuffer=yes',
    '-config', 'config:rend.ThreadedRendering=no',
    '-config', 'config:LuaFileName=verify-probe.lua')
if ($NoController) {
    $argumentList += @('-config', 'input:device1=10', '-config', 'input:device2=10', '-config', 'input:device3=10', '-config', 'input:device4=10')
}
$argumentList += $elfPath
Set-Content -LiteralPath (Join-Path $EvidenceRoot 'emulator-command.txt') -Value ("$profileExe " + ($argumentList -join ' ')) -Encoding UTF8
$process = Start-Process -FilePath $profileExe -WorkingDirectory $EvidenceRoot -ArgumentList $argumentList -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $EvidenceRoot 'stdout.log') -RedirectStandardError (Join-Path $EvidenceRoot 'stderr.log') -PassThru
if (-not $process.WaitForExit($WaitSeconds * 1000)) {
    try { $process.Kill() } catch { }
    throw "Flycast did not exit within $WaitSeconds seconds; the probe bound was not reached."
}

# --- Evaluate -------------------------------------------------------------------
$csvPath = Join-Path $EvidenceRoot 'verify-probe.csv'
if (-not (Test-Path -LiteralPath $csvPath -PathType Leaf)) {
    throw 'The Lua probe wrote no output; verification failed.'
}
$lines = @(Get-Content -LiteralPath $csvPath)
$expectedKind = if ($NoController) { 'no-controller-complete' } else { 'success' }
$resultLine = $lines | Where-Object { $_ -match "^(success|no-controller-complete|error|timeout|heap-below-gate|lights-below-gate|version-mismatch) " } | Select-Object -First 1
if ($null -eq $resultLine) {
    throw 'The Lua probe did not reach a terminal result.'
}
$resultKind = ($resultLine -split ' ')[0]
$captureLines = @($lines | Where-Object { $_ -match '^capture ' })
$frameReports = @()
foreach ($captureLine in $captureLines) {
    if ($captureLine -match 'loopFrame=(\d+) .* width=(\d+) height=(\d+) .* center=([0-9a-f]{4}) .* path=(.+)$') {
        $loopFrame = [int]$Matches[1]
        $width = [int]$Matches[2]
        $height = [int]$Matches[3]
        $rawPath = $Matches[5]
        $rawHash = (Get-FileHash -LiteralPath $rawPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $bytes = [IO.File]::ReadAllBytes($rawPath)
        $distinct = [Collections.Generic.HashSet[int]]::new()
        $histogram = @{}
        for ($index = 0; $index + 1 -lt $bytes.Length; $index += 2) {
            $value = [int]$bytes[$index] -bor ([int]$bytes[$index + 1] -shl 8)
            [void]$distinct.Add($value)
            if ($histogram.ContainsKey($value)) { $histogram[$value]++ } else { $histogram[$value] = 1 }
        }
        $dominant = ($histogram.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 3 | ForEach-Object { ('{0:x4}:{1}' -f $_.Key, $_.Value) }) -join ','
        $pngPath = [IO.Path]::ChangeExtension($rawPath, '.png')
        $pngStatus = 'skipped'
        try {
            Add-Type -AssemblyName System.Drawing
            $bitmap = New-Object System.Drawing.Bitmap($width, $height)
            for ($y = 0; $y -lt $height; $y++) {
                for ($x = 0; $x -lt $width; $x++) {
                    $offset = ($y * $width + $x) * 2
                    $pixel = [int]$bytes[$offset] -bor ([int]$bytes[$offset + 1] -shl 8)
                    $r = (($pixel -shr 11) -band 31) -shl 3
                    $g = (($pixel -shr 5) -band 63) -shl 2
                    $b = ($pixel -band 31) -shl 3
                    $bitmap.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, $r, $g, $b))
                }
            }
            $bitmap.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()
            $pngStatus = $pngPath
        } catch {
            $pngStatus = "failed: $($_.Exception.Message)"
        }
        $frameReports += "Frame$loopFrame=raw=$rawPath sha256=$rawHash distinctColors=$($distinct.Count) dominant=$dominant center=$($Matches[4]) png=$pngStatus"
    }
}

$report = @(
    'VerifierVersion=1',
    "BuildId=$buildId",
    "BuildRoot=$BuildRoot",
    "ELFSHA256=$elfHash",
    "GitCommit=$($manifest['GitCommit'])",
    "GitDirty=$($manifest['GitDirty'])",
    "ExternalInputSha256=$($manifest['ExternalInputSha256'])",
    "SdkImageId=$imageId",
    "FlycastSha256=$flycastHash",
    "Symbols=" + (($symbols.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=0x$($_.Value)" }) -join ';'),
    "Mode=$(if ($NoController) { 'no-controller' } else { 'start-exit' })",
    "StartAfterFrames=$StartAfterFrames",
    "ExpectedResult=$expectedKind",
    "Result=$resultKind",
    "ResultLine=$resultLine"
) + $frameReports
$report | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'verify-report.txt') -Encoding UTF8
$report | ForEach-Object { Write-Output $_ }
if ($resultKind -ne $expectedKind) {
    throw "Verification failed: expected '$expectedKind' but the probe ended with '$resultKind'."
}
if ($captureFrameList.Count -gt 0 -and $frameReports.Count -ne $captureFrameList.Count) {
    throw "Verification failed: expected $($captureFrameList.Count) framebuffer captures but recorded $($frameReports.Count)."
}
Write-Output "verify-authored-scene: PASS ($buildId)"
