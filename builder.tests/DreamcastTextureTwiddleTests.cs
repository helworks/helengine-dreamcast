using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Verifies the PVR Morton interleave against hand-computed values.</summary>
public sealed class DreamcastTextureTwiddleTests {
    /// <summary>Confirms the interleave puts y in even bits and x in odd bits.</summary>
    [Theory]
    [InlineData(0, 0, 0)]
    [InlineData(0, 1, 1)]
    [InlineData(1, 0, 2)]
    [InlineData(1, 1, 3)]
    [InlineData(0, 2, 4)]
    [InlineData(2, 0, 8)]
    [InlineData(3, 3, 15)]
    [InlineData(0, 3, 5)]
    [InlineData(3, 0, 10)]
    public void TwiddledIndex_MatchesHandComputedMortonOrder(int x, int y, int expected) {
        Assert.Equal(expected, DreamcastTextureTwiddle.TwiddledIndex(x, y));
    }

    /// <summary>Confirms a 4x4 image is reordered exactly, not merely permuted.</summary>
    [Fact]
    public void Twiddle_ReordersAFourByFourImageExactly() {
        ushort[] linear = new ushort[16];
        for (int i = 0; i < 16; i++) {
            linear[i] = (ushort)(i + 1);
        }

        ushort[] twiddled = DreamcastTextureTwiddle.Twiddle(linear, 4);

        // linear[y * 4 + x] must land at TwiddledIndex(x, y).
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                Assert.Equal(linear[y * 4 + x], twiddled[DreamcastTextureTwiddle.TwiddledIndex(x, y)]);
            }
        }
        Assert.Equal(16, twiddled.Length);
    }

    /// <summary>Confirms a non-square or non-power-of-two side is rejected rather than silently mishandled.</summary>
    [Theory]
    [InlineData(3)]
    [InlineData(0)]
    [InlineData(6)]
    public void Twiddle_RejectsASideThatIsNotAPositivePowerOfTwo(int side) {
        ushort[] linear = new ushort[16];

        ArgumentOutOfRangeException exception =
            Assert.Throws<ArgumentOutOfRangeException>(() => DreamcastTextureTwiddle.Twiddle(linear, side));

        Assert.Equal("side", exception.ParamName);
    }
}
