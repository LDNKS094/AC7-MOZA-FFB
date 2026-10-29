using System;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

sealed class Program {
  readonly object gate = new();
  readonly CancellationTokenSource stopping;
  NativeRawFlight output;
  const int Port = 3476;
  Sample latest = new();
  long updatedMs;

  Program(CancellationTokenSource stopping) {
    this.stopping = stopping;
  }

  [System.Runtime.InteropServices.DllImport("kernel32.dll")] static extern IntPtr GetStdHandle(int h);
  [System.Runtime.InteropServices.DllImport("kernel32.dll")] static extern bool GetConsoleMode(IntPtr h, out uint mode);
  [System.Runtime.InteropServices.DllImport("kernel32.dll")] static extern bool SetConsoleMode(IntPtr h, uint mode);

  [STAThread] static int Main(string[] args) {
    if (args.Length != 0) { Console.Error.WriteLine("[bridge] Double-click DCS.exe without arguments."); return 2; }
    var input = GetStdHandle(-10);
    if (GetConsoleMode(input, out uint mode)) SetConsoleMode(input, (mode & ~0x0040u) | 0x0080u);
    using var stopping = new CancellationTokenSource();
    ConsoleCancelEventHandler cancel = (_, e) => { e.Cancel = true; stopping.Cancel(); };
    Console.CancelKeyPress += cancel;
    try {
      return new Program(stopping).Run();
    } catch (OperationCanceledException) when (stopping.IsCancellationRequested) { return 0; }
    catch (Exception ex) { Console.Error.WriteLine("[bridge] stopped: " + ex.Message); return 1; }
    finally { Console.CancelKeyPress -= cancel; }
  }

  static async Task<Process> WaitForGameAsync(CancellationToken token) {
    Console.WriteLine("[bridge] waiting for AC7...");
    while (true) {
      token.ThrowIfCancellationRequested();
      Process[] games = Process.GetProcessesByName("Ace7Game");
      if (games.Length == 1) return games[0];
      foreach (Process game in games) game.Dispose();
      if (games.Length > 1) throw new InvalidOperationException("More than one AC7 process is running. Close the extra game instance and restart DCS.exe.");
      await Task.Delay(500, token).ConfigureAwait(false);
    }
  }

  int Run() {
    // Cockpit detects DCS.exe before AC7 exists, so its TCP endpoint must already be available.
    Task cockpit = CockpitAsync();
    Task pipe = PipeLoopAsync();
    try {
      while (!stopping.IsCancellationRequested) {
        using var game = WaitForGameAsync(stopping.Token).GetAwaiter().GetResult();
        stopping.Token.ThrowIfCancellationRequested();
        using var session = CancellationTokenSource.CreateLinkedTokenSource(stopping.Token);
        Console.WriteLine($"[bridge] AC7 PID {game.Id}; Cockpit port {Port}");
        lock (gate) { latest = new Sample(); updatedMs = 0; output = new NativeRawFlight(game.Id, session); }
        try {
          game.WaitForExitAsync(stopping.Token).GetAwaiter().GetResult();
          Console.WriteLine($"[bridge] AC7 PID {game.Id} exited.");
        } finally {
          NativeRawFlight old;
          lock (gate) { latest = new Sample(); updatedMs = 0; old = output; output = null; }
          old?.Dispose();
        }
      }
      return 0;
    } catch (OperationCanceledException) when (stopping.IsCancellationRequested) { return 0; }
    finally {
      stopping.Cancel();
      try { Task.WhenAll(cockpit, pipe).GetAwaiter().GetResult(); }
      catch (OperationCanceledException) when (stopping.IsCancellationRequested) { }
    }
  }

  void ClearFlight() {
    lock (gate) { latest = new Sample(); updatedMs = 0; output?.Stop(); }
  }

  async Task PipeLoopAsync() {
    var clock = Stopwatch.StartNew();
    CancellationToken token = stopping.Token;
    try {
      while (!token.IsCancellationRequested) {
        using var pipe = new NamedPipeServerStream("AceTelemetry", PipeDirection.In, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
        Console.WriteLine("[pipe] waiting for game mod...");
        await pipe.WaitForConnectionAsync(token);
        Console.WriteLine("[pipe] game mod connected");
        try {
          using var reader = new StreamReader(pipe, Encoding.ASCII);
          string line;
          while ((line = await reader.ReadLineAsync(token)) != null) {
            Sample sample = FlightTelemetry.Parse(line); sample.t = clock.Elapsed.TotalSeconds;
            lock (gate) {
              latest = sample; updatedMs = Environment.TickCount64;
              if (!sample.valid || sample.paused) output?.Stop();
            }
          }
        } catch (IOException ex) { Console.WriteLine("[pipe] disconnected: " + ex.Message); }
        finally { ClearFlight(); }
      }
    } finally { ClearFlight(); stopping.Cancel(); }
  }

  async Task CockpitAsync() {
    var listener = new TcpListener(IPAddress.Loopback, Port);
    try {
      listener.Start();
      while (!stopping.IsCancellationRequested) {
        Console.WriteLine("[tcp] waiting for MOZA Cockpit automatic DCS connection...");
        using var client = await listener.AcceptTcpClientAsync(stopping.Token);
        client.NoDelay = true; client.SendTimeout = 500;
        Console.WriteLine("[tcp] Cockpit connected");
        try { await ServeAsync(client.GetStream()); }
        catch (IOException) { }
        finally { lock (gate) { output?.Stop(); } }
      }
    } finally { lock (gate) { output?.Stop(); } listener.Stop(); stopping.Cancel(); }
  }

  async Task ServeAsync(NetworkStream stream) {
    var telemetry = new FlightTelemetry();
    CancellationToken token = stopping.Token;
    while (!token.IsCancellationRequested) {
      Sample sample;
      lock (gate) {
        sample = Environment.TickCount64 - updatedMs > 500 ? new Sample() : latest;
        if (!sample.valid || sample.paused) output?.Stop();
      }
      byte[] frame = Encoding.ASCII.GetBytes(telemetry.Frame(sample));
      stream.Write(frame, 0, frame.Length);
      lock (gate) {
        if (ReferenceEquals(latest, sample) && sample.valid && !sample.paused) output?.FlightFrame();
      }
      await Task.Delay(sample.valid && !sample.paused ? 16 : 50, token);
    }
  }
}
