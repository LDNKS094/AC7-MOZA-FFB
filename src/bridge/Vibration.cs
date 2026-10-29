// Broadcasts classified rumble to capable devices; NativeRawFlight serializes access.
using System; using System.Linq; using System.Collections.Generic; using System.Windows.Forms; using SharpDX.DirectInput;

sealed class Vibration {
  const double Gain = 1;
  DirectInput di;
  Form form;
  readonly List<Output> outputs = new();
  int lastMag = -1, lastPeriod = -1;
  public bool Ok => outputs.Count != 0;

  sealed class Output {
    public string Name;
    public Joystick Joy;
    public Effect Effect;
    public int[] Axes;

    public EffectParameters Params(int mag, int periodMs) {
      var p = new EffectParameters { Flags = EffectFlags.Cartesian | EffectFlags.ObjectIds, Duration = -1, Gain = 10000, SamplePeriod = 0,
        StartDelay = 0, TriggerButton = -1, TriggerRepeatInterval = 0, Parameters = new PeriodicForce { Magnitude = mag, Period = periodMs * 1000, Offset = 0, Phase = 0 } };
      p.SetAxes(Axes, Axes.Length == 1 ? new[] { 1 } : new[] { 7000, 7000 });
      return p;
    }

    public void Close() {
      try { Effect?.Stop(); } catch (Exception ex) { Console.WriteLine($"[vib] stop failed on {Name}: {ex.Message}"); }
      try { Effect?.Dispose(); } catch (Exception ex) { Console.WriteLine($"[vib] effect release on {Name}: {ex.Message}"); }
      try { Joy?.Unacquire(); } catch (Exception ex) { Console.WriteLine($"[vib] unacquire on {Name}: {ex.Message}"); }
      try { Joy?.Dispose(); } catch (Exception ex) { Console.WriteLine($"[vib] device release on {Name}: {ex.Message}"); }
      Effect = null; Joy = null;
    }
  }

  public Vibration() {
    try {
      di = new DirectInput();
      form = new Form();
      foreach (var device in di.GetDevices(DeviceClass.GameControl, DeviceEnumerationFlags.ForceFeedback | DeviceEnumerationFlags.AttachedOnly)) {
        var output = new Output { Name = device.ProductName };
        try {
          output.Joy = new Joystick(di, device.InstanceGuid);
          output.Axes = output.Joy.GetObjects().Where(o => (o.ObjectId.Flags & DeviceObjectTypeFlags.ForceFeedbackActuator) != 0)
            .Select(o => (int)o.ObjectId).Take(2).ToArray();
          if (output.Axes.Length == 0 || !output.Joy.GetEffects(EffectType.All).Any(effect => effect.Guid == EffectGuid.Sine)) continue;
          output.Joy.SetCooperativeLevel(form.Handle, CooperativeLevel.Exclusive | CooperativeLevel.Background);
          output.Joy.Acquire();
          output.Effect = new Effect(output.Joy, EffectGuid.Sine, output.Params(0, 50));
          output.Effect.Start(1);
          outputs.Add(output);
          Console.WriteLine($"[vib] ready on {output.Name} ({output.Axes.Length} axes), gain {Gain}");
        } catch (Exception ex) { Console.WriteLine($"[vib] init failed on {output.Name}: {ex.Message}"); }
        finally { if (!outputs.Contains(output)) output.Close(); }
      }
    } catch (Exception ex) { Console.WriteLine("[vib] initialization failed: " + ex.Message); }
    if (!Ok) {
      Console.WriteLine("[vib] no available sine force-feedback output; Cockpit telemetry remains active");
      Close();
    }
  }

  public void UpdateRaw(double raw, int periodMs) {
    if (!Ok) return;
    // Same native magnitude for every device; clip to the DirectInput range before gain1.
    int mag = (int)(Math.Clamp(raw, 0, 1) * Gain * 10000);
    if (mag == 0) { Stop(); return; }
    if (mag == lastMag && periodMs == lastPeriod) return;
    bool restart = lastMag <= 0;
    for (int i = outputs.Count - 1; i >= 0; --i) {
      var output = outputs[i];
      try {
        output.Effect.SetParameters(output.Params(mag, periodMs), EffectParameterFlags.TypeSpecificParameters | EffectParameterFlags.NoRestart);
        if (restart) output.Effect.Start(1);
      } catch (Exception ex) {
        Console.WriteLine($"[vib] output failed on {output.Name}; removing this output: {ex.Message}");
        outputs.RemoveAt(i); output.Close();
      }
    }
    lastMag = mag; lastPeriod = periodMs;
  }

  public void Stop() {
    if (lastMag == 0) return;
    for (int i = outputs.Count - 1; i >= 0; --i) {
      var output = outputs[i];
      try { output.Effect.Stop(); }
      catch (Exception ex) {
        Console.WriteLine($"[vib] stop failed on {output.Name}; removing this output: {ex.Message}");
        outputs.RemoveAt(i); output.Close();
      }
    }
    lastMag = 0; lastPeriod = -1;
  }

  public void Close() {
    foreach (var output in outputs) output.Close();
    outputs.Clear();
    di?.Dispose(); form?.Dispose();
    di = null; form = null;
  }
}
