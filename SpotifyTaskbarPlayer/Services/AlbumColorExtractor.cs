using Windows.Graphics.Imaging;
using Windows.Storage.Streams;
using Windows.UI;

namespace SpotifyTaskbarPlayer.Services;

/// <summary>
/// Computes an accent color from an album-art image stream.
/// Strategy: downscale to 64x64, average RGB while discarding near-black /
/// near-white pixels (which would wash out the result), then boost HSV
/// saturation so the color reads on a dark taskbar.
/// </summary>
public static class AlbumColorExtractor
{
    public readonly record struct MeanRgb(byte R, byte G, byte B);

    /// <summary>
    /// Returns the mean-RGB of the cover (post-filtering). Cache this once per
    /// track and re-apply <see cref="BoostSaturation"/> when the saturation
    /// setting changes — saves decoding the image again.
    /// </summary>
    public static async Task<MeanRgb?> ComputeMeanAsync(IRandomAccessStream stream)
    {
        try
        {
            stream.Seek(0);
            var decoder = await BitmapDecoder.CreateAsync(stream);
            var transform = new BitmapTransform { ScaledWidth = 64, ScaledHeight = 64 };
            var pixelData = await decoder.GetPixelDataAsync(
                BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied, transform,
                ExifOrientationMode.IgnoreExifOrientation, ColorManagementMode.DoNotColorManage);
            var pixels = pixelData.DetachPixelData();

            long r = 0, g = 0, b = 0; int count = 0;
            for (int i = 0; i + 3 < pixels.Length; i += 4)
            {
                int pb = pixels[i];
                int pg = pixels[i + 1];
                int pr = pixels[i + 2];
                int max = Math.Max(pr, Math.Max(pg, pb));
                int min = Math.Min(pr, Math.Min(pg, pb));
                if (max < 30 || min > 230) continue;
                r += pr; g += pg; b += pb; count++;
            }
            if (count == 0) return null;
            return new MeanRgb((byte)(r / count), (byte)(g / count), (byte)(b / count));
        }
        catch
        {
            return null;
        }
    }

    public static Color BoostSaturation(MeanRgb mean, double factor)
        => BoostSaturation(mean.R, mean.G, mean.B, factor);

    public static Color BoostSaturation(byte r, byte g, byte b, double factor)
    {
        var rf = r / 255.0;
        var gf = g / 255.0;
        var bf = b / 255.0;
        var max = Math.Max(rf, Math.Max(gf, bf));
        var min = Math.Min(rf, Math.Min(gf, bf));
        var v = max;
        var s = max == 0 ? 0 : (max - min) / max;
        double h = 0;
        if (max != min)
        {
            if (max == rf)      h = ((gf - bf) / (max - min)) % 6;
            else if (max == gf) h = (bf - rf) / (max - min) + 2;
            else                h = (rf - gf) / (max - min) + 4;
            h *= 60;
            if (h < 0) h += 360;
        }
        s = Math.Min(1.0, s * factor);

        var c = v * s;
        var x = c * (1 - Math.Abs((h / 60) % 2 - 1));
        var m = v - c;
        double rp, gp, bp;
        if      (h <  60) { rp = c; gp = x; bp = 0; }
        else if (h < 120) { rp = x; gp = c; bp = 0; }
        else if (h < 180) { rp = 0; gp = c; bp = x; }
        else if (h < 240) { rp = 0; gp = x; bp = c; }
        else if (h < 300) { rp = x; gp = 0; bp = c; }
        else              { rp = c; gp = 0; bp = x; }

        return Color.FromArgb(255,
            (byte)Math.Clamp((rp + m) * 255, 0, 255),
            (byte)Math.Clamp((gp + m) * 255, 0, 255),
            (byte)Math.Clamp((bp + m) * 255, 0, 255));
    }
}
