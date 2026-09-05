using System.Runtime.InteropServices;

namespace ZiModem.Net;

// Mirrors native/wrapper/include/zimodem_host.h exactly. Never expose this class
// publicly -- ZiModemDevice is the only supported entry point (see its own file for why:
// the native side enforces a one-instance-per-process constraint that the managed
// wrapper has to respect too).
internal static class NativeMethods
{
    private const string LibName = "zimodem_host";

    // Runs once, before any P/Invoke below can be reached (static constructors run
    // lazily on first use of the type, guaranteed before any of NativeMethods' own
    // static members are touched) -- see NativeLibraryResolver for why this exists
    // instead of relying on the OS loader's default search.
    static NativeMethods()
    {
        NativeLibraryResolver.EnsureRegistered();
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ZimodemHostConfig
    {
        // Required -- zimodem_host_create() fails without one (see zimodem_host.h and
        // ZiModemDevice's constructor).
        [MarshalAs(UnmanagedType.LPStr)]
        public string DataDir;
    }

    // Payload-less: on_serial_out is a "go drain it" notification now, not a data
    // carrier -- see zimodem_host_rx_available/zimodem_host_rx_read below and the
    // comment on zimodem_serial_out_cb in zimodem_host.h.
    //
    // IntPtr (not `string`) for the log message parameter: the callback fires on the
    // native background thread, and marshaling a `const char*` to `string` on every
    // single log line is needless overhead for callbacks that may go unused. Callers
    // that want the text call Marshal.PtrToStringAnsi on it themselves (see ZiModemDevice).
    internal delegate void SerialOutCallback(nint userContext);
    internal delegate void SignalCallback(nint userContext, int pin, int active);
    internal delegate void LogCallback(nint userContext, nint message);

    // Fired on the native background thread whenever the firmware changes baud / data
    // bits / parity / stop bits. Argument layout matches zimodem_host_get_line_config.
    internal delegate void LineConfigCallback(nint userContext, int baud, int dataBits, int parity, int stopBitsX10);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint zimodem_host_create(ref ZimodemHostConfig cfg);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zimodem_host_set_callbacks(
        nint handle,
        SerialOutCallback? onSerialOut,
        SignalCallback? onSignal,
        LogCallback? onLog,
        LineConfigCallback? onLineConfig,
        nint userContext);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zimodem_host_start(nint handle);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zimodem_host_write_serial(nint handle, byte[] data, nuint len);

    // Modem -> host, poll side. Backs ZiModemDevice's reconstruction of
    // SerialDataReceived. Unbounded/untracked for overrun on the native side
    // deliberately -- see zimodem_hal::serial's own header comment: a real UART chip
    // owns its own fixed-depth FIFO and overrun behavior, not this wire.
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zimodem_host_rx_available(nint handle);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zimodem_host_rx_read(nint handle);

    // Host -> modem pin write (the reverse of SignalCallback, which is modem -> host
    // only) -- see zimodem_host_set_pin's own comment in zimodem_host.h for the flow-
    // control use case and the "don't leave CTS deasserted forever" hang warning.
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zimodem_host_set_pin(nint handle, int pin, int value);

    // The modem's current serial line settings (baud / data bits / parity / stop bits),
    // as last applied by the vendored firmware. Backs ZiModemDevice.GetLineConfig, which
    // a UART emulation uses to check its own divisor/framing matches -- a mismatch is
    // what garbles data on real hardware. out_baud is 0 until the firmware's setup() has
    // run. Parity is one of the ZIMODEM_PARITY_* values (0 none / 1 odd / 2 even);
    // stopBitsX10 is stop bits times ten (10, 15, 20).
    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zimodem_host_get_line_config(
        nint handle,
        out int baud,
        out int dataBits,
        out int parity,
        out int stopBitsX10);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zimodem_host_destroy(nint handle);
}
