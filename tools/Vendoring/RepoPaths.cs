namespace ZiModem.Vendoring;

internal static class RepoPaths
{
    public static string Root { get; } = FindRepoRoot();

    public static string SubmoduleDir => Path.Combine(Root, "external", "zimodem");
    public static string PatchDir => Path.Combine(Root, "patches", "zimodem");
    public static string SeriesFile => Path.Combine(PatchDir, "SERIES");
    public static string VendorDir => Path.Combine(Root, "build", "vendor", "zimodem-src");

    private static string FindRepoRoot()
    {
        foreach (var start in new[] { Directory.GetCurrentDirectory(), AppContext.BaseDirectory })
        {
            var dir = new DirectoryInfo(start);
            while (dir is not null)
            {
                if (Directory.Exists(Path.Combine(dir.FullName, "external", "zimodem")) &&
                    Directory.Exists(Path.Combine(dir.FullName, "patches")))
                {
                    return dir.FullName;
                }
                dir = dir.Parent;
            }
        }

        throw new InvalidOperationException(
            "Could not locate the repo root (a directory containing both 'external/zimodem' " +
            "and 'patches'). Run this tool from within the BitMagic.ZiModem repo.");
    }
}
