using System.Runtime.InteropServices;

namespace ZiModem.Net;

/// <summary>
/// Managed wrapper around the native zimodem_host C ABI (native/wrapper/include/zimodem_host.h).
/// </summary>
/// <remarks>
/// <para>
/// Only one <see cref="ZiModemDevice"/> may exist per process at a time -- the native
/// HAL underneath is process-global state (matching the vendored firmware's own
/// assumption that it's the only thing running on the chip). The constructor throws if
/// an instance already exists; see docs/native-wrapper-spec.md section 8 for the full
/// rationale.
/// </para>
/// <para>
/// <see cref="SerialDataReceived"/>, <see cref="SignalChanged"/>, and <see cref="Log"/>
/// all fire synchronously on the native background thread the instance owns internally
/// -- not the thread that constructed this object, and not a captured
/// <see cref="System.Threading.SynchronizationContext"/>. If a caller needs these on a
/// specific thread (a UI thread, for example), it must marshal there itself. This is a
/// deliberate simplification versus queueing everything through a
/// <see cref="System.Collections.Concurrent.ConcurrentQueue{T}"/> for the caller to
/// drain: correct, but revisit if a consumer needs push-to-UI-thread semantics.
/// </para>
/// </remarks>
public sealed class ZiModemDevice : IDisposable
{
    private readonly nint _handle;

    // Kept as fields, not locals: the native side holds these as raw function pointers
    // for the instance's entire lifetime, so they must stay reachable and un-collected
    // for at least that long.
    private readonly NativeMethods.SerialOutCallback _onSerialOutNative;
    private readonly NativeMethods.SignalCallback _onSignalNative;
    private readonly NativeMethods.LogCallback _onLogNative;

    private bool _started;
    private bool _disposed;

    public event EventHandler<SerialDataEventArgs>? SerialDataReceived;
    public event EventHandler<SignalChangedEventArgs>? SignalChanged;
    public event EventHandler<ZiModemLogEventArgs>? Log;

    /// <param name="dataDir">
    /// Host directory the emulated SPIFFS filesystem (config, phonebook, logs) is rooted
    /// under. Required: a library has no business silently picking a filesystem location
    /// on your behalf (an earlier version of this constructor defaulted to a fresh OS
    /// temp directory when omitted -- removed deliberately; that meant config/phonebook
    /// silently failed to persist across runs unless you happened to know to pass a
    /// path). If you want a throwaway directory, compute one yourself and pass it
    /// explicitly, e.g. <c>Path.Combine(Path.GetTempPath(), "zimodem-" + Guid.NewGuid())</c>.
    /// </param>
    public ZiModemDevice(string dataDir)
    {
        ArgumentException.ThrowIfNullOrEmpty(dataDir);
        Directory.CreateDirectory(dataDir);

        var cfg = new NativeMethods.ZimodemHostConfig { DataDir = dataDir };
        _handle = NativeMethods.zimodem_host_create(ref cfg);
        if (_handle == 0)
        {
            throw new InvalidOperationException(
                "zimodem_host_create failed -- an instance may already exist in this process " +
                "(only one zimodem_handle is allowed per process; see docs/native-wrapper-spec.md section 8).");
        }

        _onSerialOutNative = OnSerialOutNative;
        _onSignalNative = OnSignalNative;
        _onLogNative = OnLogNative;
        NativeMethods.zimodem_host_set_callbacks(_handle, _onSerialOutNative, _onSignalNative, _onLogNative, 0);
    }

    /// <summary>Starts the background thread: runs the vendored sketch's setup() once, then loop() repeatedly.</summary>
    public void Start()
    {
        ThrowIfDisposed();
        if (_started)
            throw new InvalidOperationException("Already started.");
        if (NativeMethods.zimodem_host_start(_handle) != 0)
            throw new InvalidOperationException("zimodem_host_start failed.");
        _started = true;
    }

    /// <summary>Host -> modem. Thread-safe; safe to call from any thread, including from within an event handler.</summary>
    public void WriteSerial(ReadOnlySpan<byte> data)
    {
        ThrowIfDisposed();
        NativeMethods.zimodem_host_write_serial(_handle, data.ToArray(), (nuint)data.Length);
    }

    private void OnSerialOutNative(nint userContext, nint data, nuint len)
    {
        var buffer = new byte[(int)len];
        if (len > 0)
            Marshal.Copy(data, buffer, 0, (int)len);
        SerialDataReceived?.Invoke(this, new SerialDataEventArgs(buffer));
    }

    private void OnSignalNative(nint userContext, int pin, int active)
    {
        SignalChanged?.Invoke(this, new SignalChangedEventArgs(pin, active != 0));
    }

    private void OnLogNative(nint userContext, nint message)
    {
        string text = Marshal.PtrToStringAnsi(message) ?? string.Empty;
        Log?.Invoke(this, new ZiModemLogEventArgs(text));
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(ZiModemDevice));
    }

    public void Dispose()
    {
        if (_disposed)
            return;
        _disposed = true;
        NativeMethods.zimodem_host_destroy(_handle);
    }
}
