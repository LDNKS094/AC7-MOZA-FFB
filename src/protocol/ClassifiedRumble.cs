using System;
using System.IO.MemoryMappedFiles;
using System.Threading;

// Sole current40-byte protocol, shared by the device consumer and log tool.
static class ClassifiedRumble {
  public static bool Read(MemoryMappedViewAccessor view, uint expectedPid, out double amplitude, out int type, out bool paused) {
    amplitude = 0; type = -1; paused = false;
    if (view.Capacity < 40) return false;
    int before = view.ReadInt32(0);
    if ((before & 1) != 0) return false;
    Thread.MemoryBarrier();
    uint magic = view.ReadUInt32(4);
    ulong timestamp = view.ReadUInt64(8);
    double value = view.ReadDouble(16);
    uint pid = view.ReadUInt32(24), version = view.ReadUInt32(28), flags = view.ReadUInt32(36);
    int winner = view.ReadInt32(32);
    Thread.MemoryBarrier();
    int after = view.ReadInt32(0);
    ulong now = (ulong)Environment.TickCount64;
    if (before != after || (after & 1) != 0 || magic != 0x37435241 || version != 1 || pid != expectedPid ||
        timestamp > now || now - timestamp > 200 || !double.IsFinite(value) || value < 0 || flags > 1 ||
        (value == 0 ? winner != -1 : winner < 0 || winner >= 46 || winner == 8) ||
        ((flags & 1) != 0 && value != 0)) return false;
    amplitude = value; type = winner; paused = (flags & 1) != 0;
    return true;
  }

}
