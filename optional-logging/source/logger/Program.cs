using System;
using System.Diagnostics;
using System.IO;
using System.Threading;

static class Program {
  static int Main(string[] args) {
    if (args.Length != 0) { Console.Error.WriteLine("Double-click AC7RumbleLog.exe without arguments."); return 2; }
    using var stopping = new CancellationTokenSource();
    ConsoleCancelEventHandler cancel = (_, e) => { e.Cancel = true; stopping.Cancel(); };
    Console.CancelKeyPress += cancel;
    try { Run(stopping.Token); return 0; }
    catch (Exception ex) { Console.Error.WriteLine("[icing] stopped: " + ex.Message); return 1; }
    finally { Console.CancelKeyPress -= cancel; }
  }

  static void Run(CancellationToken token) {
    using var capture = new IcingCapture(AppContext.BaseDirectory);
    Console.WriteLine("[icing] waiting for AC7 + optional AC7IcingObserver.asi; no output file until native Icing26; Ctrl+C exits.");
    while (!token.IsCancellationRequested) {
      Process[] games = Process.GetProcessesByName("Ace7Game");
      try {
        if (games.Length == 1) {
          Process game = games[0];
          IcingEvents events;
          try { events = new IcingEvents(game.Id); }
          catch (FileNotFoundException) { token.WaitHandle.WaitOne(500); continue; }
          using (events) {
            try {
              Console.WriteLine($"[icing] independent source PID {game.Id}");
              while (!token.IsCancellationRequested && !game.HasExited) {
                if (events.Ready) {
                  capture.Frequency = events.Frequency;
                  foreach (string line in events.Read()) capture.Consume(line, Environment.TickCount64);
                }
                capture.Advance(Environment.TickCount64);
                token.WaitHandle.WaitOne(20);
              }
            } finally { capture.Reset(); }
          }
        }
      } finally { foreach (Process game in games) game.Dispose(); }
      token.WaitHandle.WaitOne(500);
    }
  }
}
