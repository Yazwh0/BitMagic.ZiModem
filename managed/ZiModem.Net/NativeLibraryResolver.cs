using System.Reflection;
using System.Runtime.InteropServices;

namespace ZiModem.Net;

// Resolves the "zimodem_host" DllImport to the correct platform-specific binary under
// runtimes/<rid>/native/ next to this assembly, instead of relying on the OS loader's
// default search (same directory, PATH/LD_LIBRARY_PATH) finding a single flat-named
// file. That default behavior is exactly what broke once already during development:
// testing the Linux build from WSL and the Windows build from Windows both resolve to
// literally the same physical output folder (WSL's /mnt/c/... is the same disk as
// Windows' C:\...), so whichever platform's native binary was copied there last silently
// overwrote the other's. Routing both platforms' binaries into their own
// runtimes/<rid>/native/ subfolder (the same layout a real NuGet package with native
// assets uses -- see ZiModem.Net.csproj's Pack/PackagePath) means they can coexist on
// disk at the same time, and this resolver is what picks the right one at load time.
internal static class NativeLibraryResolver
{
    private const string LibraryName = "zimodem_host";
    private static bool _registered;
    private static readonly object RegisterLock = new();

    public static void EnsureRegistered()
    {
        if (_registered)
            return;
        lock (RegisterLock)
        {
            if (_registered)
                return;
            NativeLibrary.SetDllImportResolver(typeof(NativeLibraryResolver).Assembly, Resolve);
            _registered = true;
        }
    }

    private static nint Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (libraryName != LibraryName)
            return 0; // not ours to resolve -- let the runtime fall back to default resolution

        var (rid, fileName) = CurrentRidAndFileName();
        string path = Path.Combine(AppContext.BaseDirectory, "runtimes", rid, "native", fileName);

        if (!File.Exists(path))
        {
            throw new DllNotFoundException(
                $"Could not find '{fileName}' at '{path}'. Expected a native asset laid out under " +
                $"runtimes/{rid}/native/ next to {assembly.GetName().Name}.dll -- did the native build " +
                "for this platform run, and did ZiModem.Net.csproj's build pick it up?");
        }

        return NativeLibrary.Load(path);
    }

    private static (string Rid, string FileName) CurrentRidAndFileName()
    {
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            return ("win-x64", $"{LibraryName}.dll");
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            return ("linux-x64", $"lib{LibraryName}.so");

        throw new PlatformNotSupportedException(
            $"No {LibraryName} native asset for '{RuntimeInformation.OSDescription}'. " +
            "Only win-x64 and linux-x64 are built today (see docs/native-wrapper-spec.md section 10).");
    }
}
