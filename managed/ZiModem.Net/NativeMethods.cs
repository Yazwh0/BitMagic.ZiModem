using System.Runtime.InteropServices;

namespace ZiModem.Net;

// Mirrors native/wrapper/include/zimodem_host.h exactly. Never expose this class
// publicly -- ZiModemDevice is the only supported entry point (see its own file for why:
// the native side enforces a one-instance-per-process constraint that the managed
// wrapper has to respect too).
internal static class NativeMethods
{
    private const string LibName = "zimodem_host";

    [StructLayout(LayoutKind.Sequential)]
    internal struct ZimodemHostConfig
    {
        [MarshalAs(UnmanagedType.LPStr)]
        public string? DataDir;
    }

    // IntPtr (not `string`) for the message parameter: the callback fires on the native
    // background thread, and marshaling a `const char*` to `string` on every single log
    // line is needless overhead for callbacks that may go unused. Callers that want the
    // text call Marshal.PtrToStringAnsi on it themselves (see ZiModemDevice).
    internal delegate void SerialOutCallback(nint userContext, nint data, nuint len);
    internal delegate void SignalCallback(nint userContext, int pin, int active);
    internal delegate void LogCallback(nint userContext, nint message);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint zimodem_host_create(ref ZimodemHostConfig cfg);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zimodem_host_set_callbacks(
        nint handle,
        SerialOutCallback? onSerialOut,
        SignalCallback? onSignal,
        LogCallback? onLog,
        nint userContext);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zimodem_host_start(nint handle);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zimodem_host_write_serial(nint handle, byte[] data, nuint len);

    [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zimodem_host_destroy(nint handle);
}
