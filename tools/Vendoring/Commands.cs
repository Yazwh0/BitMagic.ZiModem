namespace ZiModem.Vendoring;

internal static class Commands
{
    public static int Vendor()
    {
        if (!Directory.Exists(RepoPaths.SubmoduleDir))
        {
            Console.Error.WriteLine($"error: {RepoPaths.SubmoduleDir} does not exist. " +
                                     "Did you run 'git submodule update --init'?");
            return 1;
        }

        if (Directory.Exists(RepoPaths.VendorDir))
            Directory.Delete(RepoPaths.VendorDir, recursive: true);
        Directory.CreateDirectory(Path.GetDirectoryName(RepoPaths.VendorDir)!);

        DirectoryCopy.CopyExcludingGit(RepoPaths.SubmoduleDir, RepoPaths.VendorDir);

        foreach (var patchName in ReadSeries())
        {
            var patchPath = Path.Combine(RepoPaths.PatchDir, patchName);
            if (!File.Exists(patchPath))
            {
                Console.Error.WriteLine($"error: patch listed in SERIES not found: {patchPath}");
                return 1;
            }

            var relVendorDir = Path.GetRelativePath(RepoPaths.Root, RepoPaths.VendorDir).Replace('\\', '/');
            var relPatchPath = Path.GetRelativePath(RepoPaths.Root, patchPath).Replace('\\', '/');

            // -c core.autocrlf=false: without this, git apply re-applies whatever
            // autocrlf the *invoking machine's* git installation defaults to when it
            // writes the patched file back out -- Git for Windows ships core.autocrlf=true
            // system-wide, Linux git installations typically don't, so the exact same
            // patch + vendored tree behaves differently depending on which OS applied it.
            // Forcing it off here (rather than relying on every dev/CI machine to have a
            // matching git config) is what makes this deterministic across platforms.
            var result = GitProcess.Run(
                RepoPaths.Root,
                "-c", "core.autocrlf=false",
                "apply",
                $"--directory={relVendorDir}",
                "--whitespace=nowarn",
                relPatchPath);

            if (result.ExitCode != 0)
            {
                Console.Error.WriteLine($"error: failed to apply {patchName}");
                Console.Error.WriteLine(result.StdOut);
                Console.Error.WriteLine(result.StdErr);
                Console.Error.WriteLine();
                Console.Error.WriteLine("This usually means external/zimodem has drifted from what the " +
                                         "patch was written against. Rebase the patch (tools Vendoring " +
                                         "new-patch) and re-run.");
                return 1;
            }

            Console.WriteLine($"applied {patchName}");
        }

        Console.WriteLine($"vendored zimodem into {Path.GetRelativePath(RepoPaths.Root, RepoPaths.VendorDir)}");
        return 0;
    }

    public static int NewPatch(string patchName)
    {
        if (!Directory.Exists(RepoPaths.VendorDir))
        {
            Console.Error.WriteLine($"error: {RepoPaths.VendorDir} does not exist -- run 'vendor' first");
            return 1;
        }

        var outPath = Path.Combine(RepoPaths.PatchDir, patchName);
        var tempDir = Path.Combine(Path.GetTempPath(), "zimodem-new-patch-" + Guid.NewGuid().ToString("N"));
        var pristineDir = Path.Combine(tempDir, "pristine");

        try
        {
            DirectoryCopy.CopyExcludingGit(RepoPaths.SubmoduleDir, pristineDir);

            var result = GitProcess.Run(
                RepoPaths.Root,
                "-c", "core.autocrlf=false",
                "diff", "--no-index",
                "--src-prefix=a/", "--dst-prefix=b/",
                pristineDir, RepoPaths.VendorDir);

            // git diff --no-index exits 1 when differences are found -- that's expected, not an error.
            if (result.ExitCode is not (0 or 1))
            {
                Console.Error.WriteLine("error: git diff failed");
                Console.Error.WriteLine(result.StdErr);
                return 1;
            }

            if (string.IsNullOrWhiteSpace(result.StdOut))
            {
                Console.WriteLine("no differences found between external/zimodem and build/vendor/zimodem-src");
                return 1;
            }

            File.WriteAllText(outPath, result.StdOut.Replace("\r\n", "\n"));
            Console.WriteLine($"wrote {Path.GetRelativePath(RepoPaths.Root, outPath)}");

            var seriesLines = ReadSeries();
            if (!seriesLines.Contains(patchName))
            {
                File.AppendAllText(RepoPaths.SeriesFile, patchName + "\n");
                Console.WriteLine($"appended {patchName} to SERIES -- reorder it by hand if it must apply before an existing patch");
            }
            else
            {
                Console.WriteLine($"{patchName} already listed in SERIES");
            }

            return 0;
        }
        finally
        {
            if (Directory.Exists(tempDir))
                Directory.Delete(tempDir, recursive: true);
        }
    }

    private static List<string> ReadSeries()
    {
        if (!File.Exists(RepoPaths.SeriesFile))
            return new List<string>();

        var result = new List<string>();
        foreach (var rawLine in File.ReadAllLines(RepoPaths.SeriesFile))
        {
            var line = rawLine.Trim();
            if (line.Length == 0 || line.StartsWith('#'))
                continue;
            result.Add(line);
        }
        return result;
    }
}
