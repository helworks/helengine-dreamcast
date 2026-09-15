namespace helengine.dreamcast.builder;

/// <summary>Provides the small discovery entry point used when inspecting a builder assembly.</summary>
public static class Program {
    /// <summary>Prints the Dreamcast builder identity for discovery diagnostics.</summary>
    /// <param name="args">Optional command-line arguments.</param>
    /// <returns>Zero when the descriptor was printed; otherwise zero after usage text.</returns>
    public static int Main(string[] args) {
        if (args != null && args.Length == 1 && string.Equals(args[0], "--describe", StringComparison.OrdinalIgnoreCase)) {
            DreamcastPlatformAssetBuilder builder = new();
            Console.WriteLine(builder.Descriptor.BuilderId);
            Console.WriteLine(builder.Descriptor.TargetPlatformId);
            Console.WriteLine(builder.Definition.DisplayName);
            return 0;
        }

        Console.WriteLine("helengine.dreamcast.builder --describe");
        return 0;
    }
}
