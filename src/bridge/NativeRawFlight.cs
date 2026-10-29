using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;

// Classified native output; one source protocol, no per-effect normalization.
sealed class NativeRawFlight : IDisposable {
  readonly Vibration vibration;
  readonly uint pid;
  readonly object gate = new();
  readonly CancellationTokenSource stopping;
  readonly Thread worker;
  readonly CancellationTokenRegistration cancel;
  bool flightActive;
  long lastFlightMs;
  bool Done => stopping.IsCancellationRequested;

  public NativeRawFlight(int sourcePid, CancellationTokenSource stopping) {
    this.stopping = stopping;
    pid = (uint)sourcePid;
    vibration = new Vibration();
    cancel = stopping.Token.Register(Stop);
    if (!vibration.Ok) return;
    worker = new Thread(Poll) { IsBackground = true, Name = "AC7 raw rumble" };
    worker.Start();
  }

  public void FlightFrame() {
    lock (gate) { if (!Done) { flightActive = true; lastFlightMs = Environment.TickCount64; } }
  }

  public void Stop() {
    lock (gate) { flightActive = false; vibration.Stop(); }
  }

  void Poll() {
    MemoryMappedFile mapping = null;
    MemoryMappedViewAccessor view = null;
    bool previousFresh = false;
    try {
      while (!Done) {
        lock (gate) {
          if (Done) break;
          if (!vibration.Ok) {
            Console.WriteLine("[raw-flight] no device output left; rumble stopped, Cockpit telemetry remains active");
            break;
          }
          bool fresh = false;
          double raw = 0;
          int type = -1;
          try {
            if (view == null) {
              mapping = MemoryMappedFile.OpenExisting($@"Local\AC7NativeClassifiedRumble-{pid}", MemoryMappedFileRights.Read);
              view = mapping.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read);
            }
            fresh = ClassifiedRumble.Read(view, pid, out raw, out type, out _);
          } catch (IOException) {
            view?.Dispose(); mapping?.Dispose(); view = null; mapping = null;
          }
          if (fresh && !previousFresh) Console.WriteLine("[raw-flight] receiving native source");
          previousFresh = fresh;
          int period = RumbleCarrier.PeriodMs(type);
          if (!fresh || !flightActive || Environment.TickCount64 - lastFlightMs > 500) vibration.Stop();
          else vibration.UpdateRaw(raw, period);
        }
        stopping.Token.WaitHandle.WaitOne(10);
      }
    } catch (Exception ex) {
      Stop();
      Console.WriteLine("[raw-flight] receiver failed; raw rumble stopped, Cockpit telemetry remains active: " + ex.Message);
    } finally {
      Stop();
      view?.Dispose(); mapping?.Dispose();
    }
  }

  public void Dispose() {
    stopping.Cancel(); Stop();
    worker?.Join();
    cancel.Dispose();
    vibration.Close();
    Console.WriteLine("[raw-flight] stopped and devices released");
  }
}
