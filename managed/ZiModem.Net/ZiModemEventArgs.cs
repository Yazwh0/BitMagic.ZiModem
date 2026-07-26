namespace ZiModem.Net;

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
