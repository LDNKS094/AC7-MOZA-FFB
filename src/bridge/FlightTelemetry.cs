// Current AC7 pipe data and Cockpit flight calculation; no device or logging dependency.
using System;
using System.Globalization;
using System.Text;

class Sample { public string plane = ""; public double t, spd, vx, vy, vz, pitch, yaw, roll, throttle; public bool valid, paused; }

sealed class FlightTelemetry {
  const string Aircraft = "AC7 Default";
  static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
  Sample prev;
  double gSmooth = 1, axSmooth, azSmooth, pv, yv, rv;

  public string Frame(Sample s) {
    if (!s.valid || s.paused) {
      prev = null;
      gSmooth = 1; axSmooth = azSmooth = pv = yv = rv = 0;
      return PausedFrame();
    }
    if (prev != null && s.t > prev.t + 1e-4) {
      double dt = s.t - prev.t;
      // acceleration in world (cm/s^2), specific force = a - gravity, gravity = (0,0,-981)
      double ax = (s.vx - prev.vx) / dt, ay = (s.vy - prev.vy) / dt, az = (s.vz - prev.vz) / dt + 981.0;
      var up = Up(s.pitch, s.yaw, s.roll); var fw = Fwd(s.pitch, s.yaw); var rt = Right(s.pitch, s.yaw, s.roll);
      double nz = Dot(up, ax, ay, az) / 981.0, nx = Dot(fw, ax, ay, az) / 981.0, ny = Dot(rt, ax, ay, az) / 981.0;
      nz = Math.Clamp(nz, -4, 12); nx = Math.Clamp(nx, -4, 4); ny = Math.Clamp(ny, -4, 4);
      double a = Math.Min(1, dt / 0.12);
      gSmooth += (nz - gSmooth) * a; axSmooth += (nx - axSmooth) * a; azSmooth += (ny - azSmooth) * a;
      double k = Math.Min(1, dt / 0.08);
      pv += (D2R(WrapDeg(s.pitch - prev.pitch)) / dt - pv) * k; yv += (D2R(WrapDeg(s.yaw - prev.yaw)) / dt - yv) * k; rv += (D2R(WrapDeg(s.roll - prev.roll)) / dt - rv) * k;
    }
    if (prev == null || s.t != prev.t) prev = s;

    var u = Up(s.pitch, s.yaw, s.roll); var f = Fwd(s.pitch, s.yaw); var r = Right(s.pitch, s.yaw, s.roll);
    double vF = Dot(f, s.vx, s.vy, s.vz), vU = Dot(u, s.vx, s.vy, s.vz), vR = Dot(r, s.vx, s.vy, s.vz);
    double aoa = vF > 100 ? Math.Atan2(-vU, vF) * 180 / Math.PI : 0;
    double aos = vF > 100 ? Math.Atan2(vR, vF) * 180 / Math.PI : 0;
    double tas = s.spd / 100.0; // cm/s -> m/s
    double ab = s.throttle > 0.9 ? 1 : 0;
    string I(double v) => v.ToString("0.####", Inv);
    var sb = new StringBuilder(512);
    sb.Append("aircraft_name,").Append(Aircraft).Append(';');
    sb.Append("engine_rpm_left,").Append(I(70 + 30 * Math.Clamp(s.throttle, 0, 1))).Append(';');
    sb.Append("engine_rpm_right,").Append(I(70 + 30 * Math.Clamp(s.throttle, 0, 1))).Append(';');
    sb.Append("left_gear,0;nose_gear,0;right_gear,0;");
    sb.Append("acc_x,").Append(I(axSmooth)).Append(';');
    sb.Append("acc_y,").Append(I(gSmooth)).Append(';');
    sb.Append("acc_z,").Append(I(azSmooth)).Append(';');
    sb.Append("wind_x,0;wind_y,0;wind_z,0;");
    sb.Append("vector_velocity_x,").Append(I(s.vx / 100)).Append(';');
    sb.Append("vector_velocity_y,").Append(I(s.vz / 100)).Append(';');
    sb.Append("vector_velocity_z,").Append(I(s.vy / 100)).Append(';');
    sb.Append("tas,").Append(I(tas)).Append(';');
    sb.Append("ias,").Append(I(tas)).Append(';');
    sb.Append("vertical_velocity_speed,").Append(I(s.vz / 100)).Append(';');
    sb.Append("aoa,").Append(I(aoa)).Append(';');
    sb.Append("heading,").Append(I(D2R(s.yaw))).Append(';');
    sb.Append("pitch,").Append(I(D2R(s.pitch))).Append(';');
    sb.Append("bank,").Append(I(D2R(s.roll))).Append(';');
    sb.Append("aos,").Append(I(aos)).Append(';');
    sb.Append("euler_vx,").Append(I(rv)).Append(';');
    sb.Append("euler_vy,").Append(I(yv)).Append(';');
    sb.Append("euler_vz,").Append(I(pv)).Append(';');
    sb.Append("canopy_pos,0;flap_pos,0;gear_value,0;speedbrake_value,0;wheelbrakesPos,0;");
    sb.Append("afterburner_1,").Append(I(ab)).Append(";afterburner_2,").Append(I(ab)).Append(';');
    sb.Append("mach,").Append(I(tas / 340.0)).Append(';');
    sb.Append("h_above_sea_level,3000;");
    sb.Append("weapon,;flare,30;chaff,30;cannon_shells,600;");
    return sb.ToString();
  }

  public static Sample Parse(string line) {
    var s = new Sample();
    int fields = 0;
    if (line == "none") return s;
    foreach (var kv in line.Split(';')) {
      var p = kv.Split('='); if (p.Length != 2) continue;
      if (p[0] == "pl") { s.plane = p[1]; continue; }
      if (!double.TryParse(p[1], NumberStyles.Float, Inv, out var v)) continue;
      if (!double.IsFinite(v)) return new Sample();
      switch (p[0]) {
        case "paused": s.paused = v != 0; break;
        case "spd": s.spd = v; fields |= 1; break;
        case "vx": s.vx = v; fields |= 2; break;
        case "vy": s.vy = v; fields |= 4; break;
        case "vz": s.vz = v; fields |= 8; break;
        case "p": s.pitch = v; fields |= 16; break;
        case "y": s.yaw = v; fields |= 32; break;
        case "r": s.roll = v; fields |= 64; break;
        case "thr": s.throttle = v; fields |= 128; break;
      }
    }
    // A partial pipe line must not become a valid zero-filled flight sample.
    s.valid = fields == 255 && !string.IsNullOrWhiteSpace(s.plane); return s;
  }

  static double D2R(double d) => d * Math.PI / 180.0;
  static (double x, double y, double z) Fwd(double p, double y) { p = D2R(p); y = D2R(y); return (Math.Cos(p) * Math.Cos(y), Math.Cos(p) * Math.Sin(y), Math.Sin(p)); }
  static (double x, double y, double z) Up(double p, double y, double r) {
    p = D2R(p); y = D2R(y); r = D2R(r);
    double cp = Math.Cos(p), sp = Math.Sin(p), cy = Math.Cos(y), sy = Math.Sin(y), cr = Math.Cos(r), sr = Math.Sin(r);
    return (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp); // UE FRotationMatrix Z axis
  }
  static (double x, double y, double z) Right(double p, double y, double r) {
    p = D2R(p); y = D2R(y); r = D2R(r);
    double cp = Math.Cos(p), sp = Math.Sin(p), cy = Math.Cos(y), sy = Math.Sin(y), cr = Math.Cos(r), sr = Math.Sin(r);
    return (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp); // UE FRotationMatrix Y axis
  }
  static double Dot((double x, double y, double z) a, double bx, double by, double bz) => a.x * bx + a.y * by + a.z * bz;
  static double WrapDeg(double d) { while (d > 180) d -= 360; while (d < -180) d += 360; return d; }

  public static string PausedFrame() =>
    $"aircraft_name,{Aircraft};engine_rpm_left,0;engine_rpm_right,0;acc_x,0;acc_y,1;acc_z,0;" +
    $"vector_velocity_x,0;vector_velocity_y,0;vector_velocity_z,0;tas,0;ias,0;aoa,0;mach,0;" +
    $"pitch,0;bank,0;heading,0;euler_vx,0;euler_vy,0;euler_vz,0;afterburner_1,0;afterburner_2,0;cannon_shells,600;";
}
