namespace ZiModem.Vendoring;

internal static class DirectoryCopy
{
    // The submodule's working-tree line endings depend on whatever core.autocrlf the
    // checking-out machine has (commonly CRLF on Windows, LF on Linux/CI) -- but every
    // patch in patches/zimodem/ was authored against LF content and git apply's context
    // matching is byte-exact, not EOL-aware, unless the local git config happens to
    // normalize it (which is exactly the kind of per-machine inconsistency this
    // vendoring pipeline is supposed to route around). Normalizing to LF for the C/C++
    // source extensions the patches actually touch means a patch that applies on one
    // machine applies on all of them, regardless of that machine's autocrlf setting.
    // Deliberately NOT applied to other extensions: external/zimodem also carries real
    // binary files (cbm8bit/*.d64 disk images, esp32_prod_bin/*, logo.jpg) that a
    // text-mode rewrite would corrupt.
    private static readonly HashSet<string> TextSourceExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".ino", ".h", ".hpp", ".c", ".cpp", ".cc", ".cxx",
    };

    /// <summary>Recursively copies <paramref name="sourceDir"/> to <paramref name="destDir"/>, skipping any ".git" entries.</summary>
    public static void CopyExcludingGit(string sourceDir, string destDir)
    {
        Directory.CreateDirectory(destDir);

        foreach (var dir in Directory.EnumerateDirectories(sourceDir))
        {
            var name = Path.GetFileName(dir);
            if (name == ".git")
                continue;
            CopyExcludingGit(dir, Path.Combine(destDir, name));
        }

        foreach (var file in Directory.EnumerateFiles(sourceDir))
        {
            var name = Path.GetFileName(file);
            if (name == ".git")
                continue;

            var destPath = Path.Combine(destDir, name);
            if (TextSourceExtensions.Contains(Path.GetExtension(name)))
            {
                var bytes = File.ReadAllBytes(file);
                var normalized = NormalizeToLf(bytes);
                File.WriteAllBytes(destPath, normalized);
            }
            else
            {
                File.Copy(file, destPath, overwrite: true);
            }
        }
    }

    // Operates on raw bytes (not File.ReadAllText/encoding-aware APIs) so this can't
    // itself corrupt content that isn't valid UTF-8/ASCII -- it only ever removes a 0x0D
    // that is immediately followed by 0x0A, leaving every other byte untouched.
    private static byte[] NormalizeToLf(byte[] input)
    {
        using var output = new MemoryStream(input.Length);
        for (int i = 0; i < input.Length; i++)
        {
            if (input[i] == (byte)'\r' && i + 1 < input.Length && input[i + 1] == (byte)'\n')
                continue; // drop the CR, keep the LF that follows
            output.WriteByte(input[i]);
        }
        return output.ToArray();
    }
}
