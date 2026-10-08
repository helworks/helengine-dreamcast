using System.Diagnostics;
using helengine.baseplatform.Requests;

namespace helengine.dreamcast.builder;

/// <summary>Runs the pinned PowerShell/Docker native build in an explicit source checkout.</summary>
public sealed class DreamcastNativeBuildExecutor : IDreamcastNativeBuildExecutor {
    /// <summary>Builds a normal Authored image from staged content and external generated Core sources.</summary>
    /// <param name="request">Complete platform request.</param>
    /// <param name="contentRoot">Staged content root.</param>
    /// <param name="catalogRoot">Startup catalog root.</param>
    /// <param name="cancellationToken">Cooperative cancellation token.</param>
    public void Build(
        PlatformBuildRequest request,
        string contentRoot,
        string catalogRoot,
        CancellationToken cancellationToken) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        }
        cancellationToken.ThrowIfCancellationRequested();
        if (string.IsNullOrWhiteSpace(request.GeneratedCoreCppRootPath)) {
            throw new InvalidOperationException("Dreamcast Authored build requires GeneratedCoreCppRootPath.");
        }

        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_DC_REPOSITORY_ROOT");
        if (string.IsNullOrWhiteSpace(repositoryRoot)) {
            throw new InvalidOperationException("Set HELENGINE_DC_REPOSITORY_ROOT to the isolated Dreamcast source checkout before invoking the native executor.");
        }
        repositoryRoot = Path.GetFullPath(repositoryRoot);
        string buildScript = Path.Combine(repositoryRoot, "scripts", "build.ps1");
        if (!File.Exists(buildScript)) {
            throw new FileNotFoundException("Dreamcast native build script was not found in HELENGINE_DC_REPOSITORY_ROOT.", buildScript);
        }

        string powershell = Environment.GetEnvironmentVariable("HELENGINE_DC_POWERSHELL") ?? "pwsh";
        string fixedBuildRoot = Environment.GetEnvironmentVariable("HELENGINE_DC_BUILD_RUN_ROOT");
        bool reuseBuildDirectory = !string.IsNullOrWhiteSpace(fixedBuildRoot);
        string outputRoot = reuseBuildDirectory ? Path.GetDirectoryName(Path.GetFullPath(fixedBuildRoot))! : request.OutputRoot;
        string buildId = reuseBuildDirectory ? Path.GetFileName(Path.GetFullPath(fixedBuildRoot)) : $"authored-{Guid.NewGuid():N}";
        ProcessStartInfo startInfo = new(powershell) {
            WorkingDirectory = repositoryRoot,
            UseShellExecute = false,
            RedirectStandardOutput = false,
            RedirectStandardError = false
        };
        startInfo.ArgumentList.Add("-NoProfile");
        startInfo.ArgumentList.Add("-ExecutionPolicy");
        startInfo.ArgumentList.Add("Bypass");
        startInfo.ArgumentList.Add("-File");
        startInfo.ArgumentList.Add(buildScript);
        startInfo.ArgumentList.Add("-BuildId");
        startInfo.ArgumentList.Add(buildId);
        startInfo.ArgumentList.Add("-Mode");
        startInfo.ArgumentList.Add("Authored");
        startInfo.ArgumentList.Add("-OutputRoot");
        startInfo.ArgumentList.Add(outputRoot);
        if (reuseBuildDirectory) startInfo.ArgumentList.Add("-ReuseBuildDirectory");
        startInfo.ArgumentList.Add("-GeneratedCoreRoot");
        startInfo.ArgumentList.Add(request.GeneratedCoreCppRootPath);
        startInfo.ArgumentList.Add("-ContentRoot");
        startInfo.ArgumentList.Add(contentRoot);
        startInfo.ArgumentList.Add("-CatalogRoot");
        startInfo.ArgumentList.Add(catalogRoot);
        if (!string.IsNullOrWhiteSpace(request.NativeObjectCacheRoot)) {
            startInfo.ArgumentList.Add("-NativeObjectCacheRoot");
            startInfo.ArgumentList.Add(request.NativeObjectCacheRoot);
        }

        using Process process = new() { StartInfo = startInfo, EnableRaisingEvents = true };
        if (!process.Start()) {
            throw new InvalidOperationException("Failed to start the Dreamcast native build process.");
        }
        try {
            process.WaitForExitAsync(cancellationToken).GetAwaiter().GetResult();
        } catch (OperationCanceledException) {
            try {
                process.Kill(entireProcessTree: true);
            } catch (InvalidOperationException) {
            }
            throw;
        }
        if (process.ExitCode != 0) {
            throw new InvalidOperationException($"Dreamcast native build failed with exit code {process.ExitCode}.");
        }

        string buildRoot = Path.Combine(outputRoot, buildId);
        if (!File.Exists(Path.Combine(buildRoot, "helengine_dc.elf")) ||
            !File.Exists(Path.Combine(buildRoot, "helengine_dc.map")) ||
            !File.Exists(Path.Combine(buildRoot, "build-manifest.txt"))) {
            throw new InvalidOperationException($"Dreamcast native build completed without a verified output manifest: {buildRoot}");
        }
    }
}
