using ZiModem.Vendoring;

if (args.Length == 0)
{
    PrintUsage();
    return 1;
}

switch (args[0])
{
    case "vendor":
        return Commands.Vendor();

    case "new-patch":
        if (args.Length < 2)
        {
            Console.Error.WriteLine("error: new-patch requires a patch file name, e.g. 0002-fix-something.patch");
            return 1;
        }
        return Commands.NewPatch(args[1]);

    default:
        PrintUsage();
        return 1;
}

static void PrintUsage()
{
    Console.WriteLine("""
        Usage:
          dotnet run --project tools/Vendoring -- vendor
              (Re)generate build/vendor/zimodem-src from external/zimodem, applying
              patches/zimodem/SERIES in order. Safe to re-run any time; always
              starts from a clean copy of the submodule.

          dotnet run --project tools/Vendoring -- new-patch <name>.patch
              Diff your hand-edits under build/vendor/zimodem-src against a fresh
              copy of external/zimodem, write patches/zimodem/<name>.patch, and
              append it to SERIES.
        """);
}
