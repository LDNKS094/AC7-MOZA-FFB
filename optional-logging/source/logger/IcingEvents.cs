using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;

// Own collector channel; has no reference to the main playback mapping or ASI.
sealed class IcingEvents : IDisposable {
  const int Capacity = 4096, EntryBytes = 48, HeaderBytes = 64;
  readonly MemoryMappedFile mapping;
  readonly MemoryMappedViewAccessor view;
  long cursor;
  public long Frequency => view.ReadInt64(24);

  public IcingEvents(int pid) {
    mapping = MemoryMappedFile.OpenExisting($@"Local\AC7IcingEvents-{pid}", MemoryMappedFileRights.ReadWrite);
    try {
      view = mapping.CreateViewAccessor(0, 0, MemoryMappedFileAccess.ReadWrite);
      if (view.Capacity < HeaderBytes + Capacity * EntryBytes || view.ReadUInt32(0) != 0x474c3741 || view.ReadUInt32(4) != 1 || view.ReadUInt32(8) != (uint)pid)
        throw new InvalidDataException("Invalid independent icing collector channel.");
      cursor = view.ReadInt64(16);
    } catch { view?.Dispose(); mapping.Dispose(); throw; }
  }

  public bool Ready {
    get {
      int status = view.ReadInt32(12);
      if (status < 0) throw new InvalidDataException("Independent icing collector rejected this game build/hook; status " + status);
      return status == 1;
    }
  }

  public List<string> Read() {
    view.Write(32, Environment.TickCount64);
    long end = view.ReadInt64(16);
    if (end - cursor > Capacity) throw new IOException("Icing event buffer overrun; recording stopped rather than silently dropping events.");
    var lines = new List<string>();
    while (cursor < end) {
      long ordinal = cursor + 1;
      long offset = HeaderBytes + (cursor % Capacity) * EntryBytes;
      long before = view.ReadInt64(offset);
      Thread.MemoryBarrier();
      long qpc = view.ReadInt64(offset + 8);
      ulong obj = view.ReadUInt64(offset + 16);
      double value = view.ReadDouble(offset + 24);
      int type = view.ReadInt32(offset + 32), kind = view.ReadInt32(offset + 36);
      uint thread = view.ReadUInt32(offset + 40);
      int active = view.ReadInt32(offset + 44);
      Thread.MemoryBarrier();
      if (before != ordinal * 2 || view.ReadInt64(offset) != before)
        throw new IOException("Icing event buffer overwritten while reading.");
      string name = kind switch { 1 => "request", 2 => "state_request", 3 => "native_eval_delta", 4 => "native_evaluated_value", _ => throw new InvalidDataException("Invalid icing event kind.") };
      lines.Add($"{qpc},{thread},{name},{obj:X},{type},{value.ToString("R", CultureInfo.InvariantCulture)},{active}");
      cursor = ordinal;
    }
    return lines;
  }

  public void Dispose() {
    view.Write(32, 0L);
    view.Dispose(); mapping.Dispose();
  }
}
