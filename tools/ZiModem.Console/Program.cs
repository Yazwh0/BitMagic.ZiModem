using System.Net;
using System.Net.Sockets;
using System.Text;
using ZiModem.Net;

bool showLog = args.Any(a => a is "--log" or "-v" or "--verbose");
List<int> echoPorts = ParseEchoPorts(args);
string? explicitDataDir = ParseDataDir(args);

Console.WriteLine("ZiModem test console");
Console.WriteLine("Every keystroke (including backspace/DEL) forwards to the modem immediately and raw --");
Console.WriteLine("this is a real terminal, not a line editor, so the AT command engine's own line editing");
Console.WriteLine("is what you're actually exercising here, same as a real terminal talking to real hardware.");
Console.WriteLine("Ctrl+Q quits. Ctrl+D prints the data directory. Pass --echo <port> (repeatable) at");
Console.WriteLine("startup for loopback TCP echo servers to ATDT into, and --log/-v/--verbose for [LOG] lines.");
Console.WriteLine("This app adds NO local echo of its own -- everything you see typed is the modem's own");
Console.WriteLine("echo (doEcho defaults to on). Send ATE0 yourself if you want it off.");
Console.WriteLine();

string dataDir = explicitDataDir ?? Path.Combine(Path.GetTempPath(), "zimodem-console-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(dataDir);
Console.WriteLine($"Data directory: {dataDir}");

using ZiModemDevice device = new(dataDir);

// Raw modem output prints as-is; SIGNAL/LOG lines get a distinguishing prefix so they
// don't get mistaken for what the modem itself said. All three fire on the native
// background thread (see ZiModemDevice's remarks), not this thread -- fine for a
// console tool since Console.Write doesn't need UI-thread affinity.
device.SerialDataReceived += (_, e) => Console.Write(Encoding.Latin1.GetString(e.Data));
device.SignalChanged += (_, e) => Console.WriteLine($"\n[SIGNAL] pin={e.Pin} active={e.Active}");

// Off by default -- debugPrintf() firmware chatter is noisy and mostly not what you
// want when just exercising AT commands. Pass --log/-v/--verbose to see it.
if (showLog)
{
    // debugPrintf() messages on the native side are inconsistent about trailing/leading
    // "\r\n" -- some calls include one (sometimes both), some don't (relying on a later
    // call to continue the same line). Trimming before display and always emitting
    // exactly one WriteLine per event is what keeps this at exactly one line per log
    // event instead of stacking the message's own newline(s) on top of WriteLine's.
    device.Log += (_, e) => Console.WriteLine($"\n[LOG] {e.Message.Trim('\r', '\n')}");
}

device.Start();

List<TcpListener> echoServers = [];
foreach (int port in echoPorts)
{
    StartEchoServer(port, echoServers);
    Console.WriteLine($"Echo server listening on 127.0.0.1:{port} -- try ATDT127.0.0.1:{port}");
}

RunInputLoop(device, dataDir);

foreach (TcpListener listener in echoServers)
    listener.Stop();

// Reads input one keystroke/byte at a time and forwards each one to the modem
// immediately and raw, with zero local echo of any kind -- everything printed to the
// console is either the modem's own output (SerialDataReceived) or our own [SIGNAL]/
// [LOG] annotations, never a copy of what was typed. This is what makes backspace/DEL,
// and any other control byte, actually reach zcommand.ino's own line editor instead of
// being silently consumed by .NET's line-buffered Console.ReadLine() before the app ever
// saw it, and it's also what makes the modem's own echo (doEcho, on by default) the only
// echo in play -- nothing local to double up against it.
//
// Two input sources share one byte-processing path (HandleByte) so the exact same
// forwarding/local-command logic applies whether run interactively or with stdin piped
// from a file/test harness (Console.ReadKey throws if stdin is redirected, so redirected
// input is read as a raw byte stream instead).
static void RunInputLoop(ZiModemDevice device, string dataDir)
{
    if (Console.IsInputRedirected)
    {
        using Stream stdin = Console.OpenStandardInput();
        int b;
        bool lastWasCr = false;
        while ((b = stdin.ReadByte()) >= 0)
        {
            if (lastWasCr && b == '\n')
            {
                lastWasCr = false;
                continue; // swallow the LF half of a CRLF pair -- already handled on the CR
            }
            lastWasCr = b == '\r';
            if (!HandleByte(device, dataDir, (byte)(b == '\n' ? '\r' : b)))
                return;
        }
        return;
    }

    while (true)
    {
        ConsoleKeyInfo key = Console.ReadKey(intercept: true);
        byte? mapped = MapKeyToByte(key);
        if (mapped is byte b && !HandleByte(device, dataDir, b))
            return;
    }
}

// Enter -> CR (matches the Hayes convention, not the OS newline), Backspace -> ASCII BS
// (zcommand.ino's line editor treats S-register BS, which defaults to 8, as delete-last-
// char), Ctrl+<letter> -> the conventional ASCII control code (Ctrl+A=1 .. Ctrl+Z=26).
// Arrows/function keys have no single-byte representation the firmware's simple line
// editor would understand, so they're left unmapped (ignored) rather than guessed at.
static byte? MapKeyToByte(ConsoleKeyInfo key)
{
    if (key.Key == ConsoleKey.Enter)
        return (byte)'\r';
    if (key.Key == ConsoleKey.Backspace)
        return 0x08;
    if (key.Key == ConsoleKey.Tab)
        return 0x09;
    if (key.Key == ConsoleKey.Escape)
        return 0x1B;
    if ((key.Modifiers & ConsoleModifiers.Control) != 0 && key.Key is >= ConsoleKey.A and <= ConsoleKey.Z)
        return (byte)(key.Key - ConsoleKey.A + 1);
    if (key.KeyChar != '\0' && key.KeyChar < 128)
        return (byte)key.KeyChar;
    return null;
}

// Returns false to signal the input loop should stop (Ctrl+Q / EOF).
static bool HandleByte(ZiModemDevice device, string dataDir, byte b)
{
    switch (b)
    {
        case 0x11: // Ctrl+Q
            return false;
        case 0x04: // Ctrl+D
            Console.WriteLine(dataDir);
            return true;
    }

    device.WriteSerial([b]);
    return true;
}

static List<int> ParseEchoPorts(string[] args)
{
    List<int> ports = [];
    for (int i = 0; i < args.Length - 1; i++)
    {
        if (args[i] == "--echo" && int.TryParse(args[i + 1], out int port))
            ports.Add(port);
    }
    return ports;
}

static string? ParseDataDir(string[] args)
{
    for (int i = 0; i < args.Length - 1; i++)
    {
        if (args[i] == "--datadir")
            return args[i + 1];
    }
    return null;
}

// A tiny loopback TCP echo server, purely a convenience for exercising ATDT/data
// passthrough without needing a separate external tool -- see docs/native-wrapper-spec.md
// section 9.1.
static void StartEchoServer(int port, List<TcpListener> registry)
{
    TcpListener listener = new(IPAddress.Loopback, port);
    listener.Start();
    registry.Add(listener);

    _ = Task.Run(async () =>
    {
        try
        {
            while (true)
            {
                TcpClient client = await listener.AcceptTcpClientAsync();
                _ = Task.Run(async () =>
                {
                    using (client)
                    {
                        NetworkStream stream = client.GetStream();
                        byte[] buffer = new byte[4096];
                        int n;
                        try
                        {
                            while ((n = await stream.ReadAsync(buffer)) > 0)
                                await stream.WriteAsync(buffer.AsMemory(0, n));
                        }
                        catch (IOException)
                        {
                            // remote end closed the connection
                        }
                    }
                });
            }
        }
        catch (ObjectDisposedException)
        {
            // listener.Stop() was called
        }
        catch (SocketException)
        {
            // listener.Stop() was called while AcceptTcpClientAsync was pending
        }
    });
}
