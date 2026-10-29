using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

// Passive event windows: no file exists until a new native Icing26 request/evaluation.
sealed class IcingCapture : IDisposable {
  const string Header = "qpc,thread,event,object,type,value,active";
  readonly string directory;
  readonly Queue<(long at, string line)> history = new();
  StreamWriter writer;
  long historyBytes, lastTrigger;
  public long Frequency { get; set; }

  public IcingCapture(string directory) { this.directory = directory; }

  public void Consume(string line, long now) {
    string[] fields = line.Split(',');
    if (fields.Length != 7 || !int.TryParse(fields[4], out int type)) return;
    bool trigger = type == 26 && fields[2] is "request" or "state_request" or "native_eval_delta" or "native_evaluated_value";
    Advance(now);
    history.Enqueue((now, line)); historyBytes += line.Length + 2;
    while (history.Count > 0 && (now - history.Peek().at > 30000 || historyBytes > 8 * 1024 * 1024))
      historyBytes -= history.Dequeue().line.Length + 2;
    if (writer == null && trigger) {
      string file = Path.Combine(directory, $"rumblelog-{DateTime.Now:yyyyMMdd-HHmmss-fffffff}.csv");
      writer = new StreamWriter(new FileStream(file, FileMode.CreateNew, FileAccess.Write, FileShare.Read), Encoding.ASCII);
      writer.AutoFlush = true;
      writer.WriteLine(Header);
      writer.WriteLine($"0,0,qpc_frequency,0,-1,{Frequency},-1");
      lastTrigger = now;
      foreach (var entry in history) writer.WriteLine(entry.line);
      Console.WriteLine("[icing] recording: " + file);
    } else if (writer != null) {
      writer.WriteLine(line);
      if (trigger) lastTrigger = now;
    }
  }

  public void Advance(long now) {
    if (writer != null && now - lastTrigger >= 10000) Finish();
  }

  void Finish() {
    writer?.Dispose();
    if (writer != null) Console.WriteLine("[icing] window saved; waiting for the next signal.");
    writer = null;
  }

  public void Reset() { Finish(); history.Clear(); historyBytes = 0; }
  public void Dispose() => Finish();
}
