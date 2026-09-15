namespace helengine.dreamcast.fixture;

/// <summary>Runs the Dreamcast authored-scene fixture writer.</summary>
public static class Program {
    /// <summary>Parses the fixture command and writes the requested variant.</summary>
    /// <param name="args">Expected form: <c>--output &lt;directory&gt; [--variant &lt;name&gt;]</c>.</param>
    /// <returns>Zero after a successful write; one after an invalid command or write failure.</returns>
    public static int Main(string[] args) {
        try {
            ParseArguments(args, out string outputPath, out string variant);
            new DreamcastAuthoredSceneFixtureWriter().Write(outputPath, variant);
            Console.WriteLine($"Dreamcast authored fixture '{variant}' written to '{Path.GetFullPath(outputPath)}'.");
            return 0;
        } catch (Exception exception) {
            Console.Error.WriteLine(exception.Message);
            return 1;
        }
    }

    /// <summary>Parses and validates the supported fixture arguments.</summary>
    /// <param name="args">Raw command-line arguments.</param>
    /// <param name="outputPath">Output directory.</param>
    /// <param name="variant">Requested authored variant.</param>
    static void ParseArguments(string[] args, out string outputPath, out string variant) {
        outputPath = string.Empty;
        variant = "baseline";
        if (args == null) {
            throw new ArgumentNullException(nameof(args));
        }
        for (int index = 0; index < args.Length; index++) {
            if (string.Equals(args[index], "--output", StringComparison.OrdinalIgnoreCase) && index + 1 < args.Length) {
                outputPath = args[++index];
            } else if (string.Equals(args[index], "--variant", StringComparison.OrdinalIgnoreCase) && index + 1 < args.Length) {
                variant = args[++index];
            } else {
                throw new ArgumentException("Usage: helengine.dreamcast.fixture --output <directory> --variant baseline|transform|color|camera|geometry.");
            }
        }
        if (string.IsNullOrWhiteSpace(outputPath)) {
            throw new ArgumentException("The --output directory is required.");
        } else if (!DreamcastAuthoredSceneFixtureWriter.SupportedVariants.Contains(variant, StringComparer.OrdinalIgnoreCase)) {
            throw new ArgumentException($"Unsupported fixture variant '{variant}'.");
        }
    }
}
