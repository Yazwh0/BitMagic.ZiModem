namespace ZiModem.Net;

/// <summary>
/// Modem control-line pin numbers for <see cref="ZiModemDevice.SetPin"/> and
/// <see cref="SignalChangedEventArgs.Pin"/> -- mirrors the DEFAULT_PIN_* constants for
/// the ZIMODEM_HOST platform branch (patches/zimodem/0001-add-zimodem-host-platform-branch.patch
/// in the BitMagic.ZiModem submodule). OTH is intentionally absent: it's -1 (no pin) for
/// this platform branch, not a real GPIO.
/// </summary>
public enum ZimodemPin
{
    Dsr = 5,
    Dtr = 6,
    Ri = 7,
    Rts = 8,
    Cts = 9,
    Dcd = 10,
}

/// <summary>
/// Active-low, like the rest of this firmware's control lines -- <see cref="Active"/>
/// means asserted (e.g. CTS = Active means "clear to send"), regardless of which pin.
/// </summary>
public enum ZimodemPinState
{
    Active = 0,   // LOW
    Inactive = 1, // HIGH
}

public sealed class SerialDataEventArgs : EventArgs
{
    public byte[] Data { get; }
    internal SerialDataEventArgs(byte[] data) => Data = data;
}

public sealed class SignalChangedEventArgs : EventArgs
{
    /// <summary>GPIO/signal number on the native side (e.g. DCD, RI) -- see patches/zimodem/0001 for the current pin assignments.</summary>
    public int Pin { get; }
    public bool Active { get; }
    internal SignalChangedEventArgs(int pin, bool active)
    {
        Pin = pin;
        Active = active;
    }
}

public sealed class ZiModemLogEventArgs : EventArgs
{
    public string Message { get; }
    internal ZiModemLogEventArgs(string message) => Message = message;
}

/// <summary>Parity setting reported by <see cref="ZiModemDevice.GetLineConfig"/>.</summary>
public enum ZiModemParity
{
    None = 0,
    Odd = 1,
    Even = 2,
}

/// <summary>
/// The modem's current serial line settings, as last applied by the vendored firmware
/// (its power-on default, or a later AT / ATSxx change). A UART emulation on the host
/// side can compare these against its own divisor and framing -- a mismatch is exactly
/// what produces garbage on real hardware.
/// </summary>
/// <param name="BaudRate">Bits per second, e.g. 115200. <c>0</c> until the firmware's setup() has run.</param>
/// <param name="DataBits">5..8.</param>
/// <param name="Parity">None / Odd / Even.</param>
/// <param name="StopBitsX10">Stop bits times ten: 10 (one), 15 (one and a half), or 20 (two).</param>
public readonly record struct ZiModemLineConfig(int BaudRate, int DataBits, ZiModemParity Parity, int StopBitsX10)
{
    /// <summary>True once the firmware has configured the line (<see cref="BaudRate"/> is non-zero).</summary>
    public bool IsConfigured => BaudRate != 0;

    /// <summary>Stop bits as a fractional count (1, 1.5, or 2).</summary>
    public double StopBits => StopBitsX10 / 10.0;
}

/// <summary>
/// Raised by <see cref="ZiModemDevice.LineConfigChanged"/> when the firmware changes
/// baud rate, data bits, parity, or stop bits. Fires on the native background thread --
/// see the remarks on <see cref="ZiModemDevice"/>.
/// </summary>
public sealed class ZiModemLineConfigChangedEventArgs : EventArgs
{
    public ZiModemLineConfig Config { get; }
    internal ZiModemLineConfigChangedEventArgs(ZiModemLineConfig config) => Config = config;
}
