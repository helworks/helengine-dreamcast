namespace helengine.dreamcast.builder;

/// <summary>
/// Reorders a square power-of-two image into the PVR's twiddled (Morton) order.
/// The convention is y in the even bit positions and x in the odd ones, which is
/// what the PVR expects for a texture flagged PVR_TXRFMT_TWIDDLED.
/// </summary>
public static class DreamcastTextureTwiddle {
    /// <summary>Returns the twiddled element index for a linear coordinate pair.</summary>
    /// <param name="x">Column, zero based.</param>
    /// <param name="y">Row, zero based.</param>
    /// <returns>Index into the twiddled array.</returns>
    public static int TwiddledIndex(int x, int y) {
        if (x < 0) {
            throw new ArgumentOutOfRangeException(nameof(x));
        } else if (y < 0) {
            throw new ArgumentOutOfRangeException(nameof(y));
        }

        int result = 0;
        for (int bit = 0; bit < 16; bit++) {
            result |= ((y >> bit) & 1) << (2 * bit);
            result |= ((x >> bit) & 1) << ((2 * bit) + 1);
        }
        return result;
    }

    /// <summary>Reorders a square power-of-two image into twiddled order.</summary>
    /// <param name="linear">Row-major pixels, length side * side.</param>
    /// <param name="side">Square side, a positive power of two.</param>
    /// <returns>A new array in twiddled order.</returns>
    public static ushort[] Twiddle(ushort[] linear, int side) {
        if (linear == null) {
            throw new ArgumentNullException(nameof(linear));
        } else if (side <= 0 || (side & (side - 1)) != 0) {
            throw new ArgumentOutOfRangeException(nameof(side), side, "Side must be a positive power of two.");
        } else if (linear.Length != side * side) {
            throw new ArgumentException($"Expected {side * side} pixels, received {linear.Length}.", nameof(linear));
        }

        ushort[] twiddled = new ushort[linear.Length];
        for (int y = 0; y < side; y++) {
            for (int x = 0; x < side; x++) {
                twiddled[TwiddledIndex(x, y)] = linear[(y * side) + x];
            }
        }
        return twiddled;
    }
}
