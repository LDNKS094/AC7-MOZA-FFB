// AC8 reference periods and explicit analogies; device policy, not native mechanical Hz.
static class RumbleCarrier {
  public static int PeriodMs(int type) => type switch {
    5 or 11 => 28,                                 // Gun reference; laser analogy.
    12 or 18 or 29 or 34 => 35,                     // Light hit; flare/equipped/small analogy.
    6 or 7 or 9 or 10 or 15 or 16 or 17 or 19 or 35 or 37 or 40 or 41 or 42 => 60,
    20 or 21 or 22 or 31 or 32 or 33 or 36 or 38 or 43 or 44 => 80,
    26 => 90,                                     // Icing reference, only if native type occurs.
    45 => 100,                                    // Search pulse: boom analogy, unvalidated.
    0 or 1 or 2 or 3 or 4 or 13 or 14 or 28 => 110, // Airframe effects: stall analogy.
    23 or 24 or 25 or 27 or 30 or 39 => 70,         // Cloud and airflow family.
    _ => 0                                        // Empty BombFire8 and invalid types never drive the device.
  };
}
