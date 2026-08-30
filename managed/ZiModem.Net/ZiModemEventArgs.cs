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
