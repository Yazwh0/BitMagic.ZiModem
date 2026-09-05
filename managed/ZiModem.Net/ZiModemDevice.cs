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
    private readonly NativeMethods.LineConfigCallback _onLineConfigNative;

    private bool _started;
    private bool _disposed;

    public event EventHandler<SerialDataEventArgs>? SerialDataReceived;
    public event EventHandler<SignalChangedEventArgs>? SignalChanged;
    public event EventHandler<ZiModemLogEventArgs>? Log;

    /// <summary>
    /// Raised when the firmware changes the serial line settings (baud / data bits /
    /// parity / stop bits) -- an AT config, an ATSxx write, or the power-on default.
    /// Fires on the native background thread; see the type-level remarks.
    /// </summary>
    public event EventHandler<ZiModemLineConfigChangedEventArgs>? LineConfigChanged;

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
        _onLineConfigNative = OnLineConfigNative;
        NativeMethods.zimodem_host_set_callbacks(
            _handle, _onSerialOutNative, _onSignalNative, _onLogNative, _onLineConfigNative, 0);
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

    /// <summary>
    /// Host -> modem pin write (the reverse of <see cref="SignalChanged"/>, which is
    /// modem -> host only) -- e.g. drive <see cref="ZimodemPin.Cts"/> to
    /// <see cref="ZimodemPinState.Inactive"/> to tell the modem to hold off sending,
    /// matching real ESP32 hardware flow control. The vendored firmware checks this
    /// exact pin already (serout.ino's digitalRead(pinCTS)). Thread-safe; safe to call
    /// from any thread, including from within an event handler.
    /// </summary>
    /// <remarks>
    /// Don't leave CTS inactive indefinitely with nothing planning to reassert it --
    /// the firmware's background thread busy-waits (no timeout) once its own internal
    /// buffer fills, until CTS goes active again.
    /// </remarks>
    public void SetPin(ZimodemPin pin, ZimodemPinState state)
    {
        ThrowIfDisposed();
        NativeMethods.zimodem_host_set_pin(_handle, (int)pin, (int)state);
    }

    /// <summary>
    /// The modem's current serial line settings (baud rate, data bits, parity, stop
    /// bits), as last applied by the vendored firmware -- its power-on default or a
    /// later AT / ATSxx change. Intended for a UART emulation to verify its own divisor
    /// and framing match the modem's; a mismatch is what garbles data on real hardware.
    /// <see cref="ZiModemLineConfig.IsConfigured"/> is false until the firmware's
    /// background setup() has run. Thread-safe.
    /// </summary>
    public ZiModemLineConfig GetLineConfig()
    {
        ThrowIfDisposed();
        NativeMethods.zimodem_host_get_line_config(
            _handle, out int baud, out int dataBits, out int parity, out int stopBitsX10);
        return new ZiModemLineConfig(baud, dataBits, (ZiModemParity)parity, stopBitsX10);
    }

    private void OnSerialOutNative(nint userContext)
    {
        // Payload-less notification -- drain the native RX queue ourselves via the poll
        // functions to reconstruct the same batch-of-bytes event this raised when the
        // native callback still carried data/len directly.
        List<byte>? buffer = null;
        while (NativeMethods.zimodem_host_rx_available(_handle) != 0)
        {
            int b = NativeMethods.zimodem_host_rx_read(_handle);
            if (b < 0)
                break;
            (buffer ??= new List<byte>()).Add((byte)b);
        }
        if (buffer is { Count: > 0 })
            SerialDataReceived?.Invoke(this, new SerialDataEventArgs(buffer.ToArray()));
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

    private void OnLineConfigNative(nint userContext, int baud, int dataBits, int parity, int stopBitsX10)
    {
        LineConfigChanged?.Invoke(
            this, new ZiModemLineConfigChangedEventArgs(
                new ZiModemLineConfig(baud, dataBits, (ZiModemParity)parity, stopBitsX10)));
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
