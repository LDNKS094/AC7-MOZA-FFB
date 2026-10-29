# AC7 MOZA Flight Feedback

English | [中文](README_zh.md)

Provides a MOZA Cockpit force-feedback bridge for *Ace Combat 7* and extends the game's native button bindings to support more peripherals.

Install using either the **installation script** or the **manual steps**.

## How it works

- **Flight force feedback**: the bridge collects flight data and converts it to Cockpit's DCS telemetry format, which Cockpit uses to calculate stick forces.
- **Native rumble**: the game's rumble requests and state drive rumble on connected peripherals.
- **Button extension**: the bridge reads up to 128 buttons per device and makes them available to the game's native button queries.

These features work at runtime without modifying the game executable or assets.

## Install

### Requirements

| Dependency | Purpose |
| --- | --- |
| [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) (x64) | Loads the ASI plugins |
| [UE4SS](https://www.nexusmods.com/acecombat7skiesunknown/mods/2474) (with UEHelpers) | Loads the flight telemetry script |
| [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/8.0) (Windows x64) | Runs the bridge |
| [MOZA Cockpit](https://www.mozaracing.com/downloads/) and AB6 | Computes the forces and drives the base |

Install and configure these dependencies before proceeding.

The game root folder, i.e. the folder containing `Ace7Game.exe`, should look like this:

```text
ACE COMBAT 7/
  Ace7Game.exe
  dinput8.dll            ASI Loader
  dwmapi.dll             UE4SS loader
  ue4ss/
    UE4SS.dll
    UE4SS-settings.ini
    Mods/shared/UEHelpers/UEHelpers.lua
```

### Script install

1. Run MOZA Cockpit once to create its preset library.
2. Connect your peripherals, start the game once, then exit to let it create the input configuration.
3. Run `Install.cmd`.

The script locates the game folder and preset library, copies the ASI plugins, Lua script and Cockpit preset, and updates the mod lists. If it finds an AB6 or MTQ, it backs up `Input.ini` and applies the matching bindings. For other devices, edit the bindings yourself; see [Key bindings](#key-bindings).

If the game is not found, pass the folder:

```powershell
.\Install.cmd -GameDirectory 'D:\Games\ACE COMBAT 7'
```

### Script uninstall

`Uninstall.cmd` removes the files added by this patch, its mod-list entries and the Cockpit preset; the input configuration is left untouched.

To specify the folder:

```powershell
.\Uninstall.cmd -GameDirectory 'G:\Games\ACE COMBAT 7'
```

### Manual install

1. Run Cockpit once to create its preset library.
2. Connect your peripherals, start the game once, then exit to let it create the input configuration.
3. Copy the mod files:

   | File in this package | Copy to |
   | --- | --- |
   | `payload/AC7RumbleTrace.asi` | Game root |
   | `payload/AC7NativeInput.asi` | Game root |
   | `mod/AC7Telemetry/` | `ue4ss/Mods/AC7Telemetry/` |

4. Add this to `ue4ss/Mods/mods.txt`:

   ```text
   AC7Telemetry : 1
   ```

5. Add this to `ue4ss/Mods/mods.json`:

   ```json
   [
     { "mod_name": "AC7Telemetry", "mod_enabled": true }
   ]
   ```

6. Copy `presets/{71ab7001-0014-4a70-8001-000000000004}.preset` into `%USERPROFILE%\Documents\MOZA Cockpit\PresetLibrary\`.

7. Add the following record to the `presets` array in `PresetIndex.index` in the same folder; **replace `file_path` with the actual full path**:

   ```json
   {
     "date": "1791976000",
     "device_type": "Base",
     "devices": ["AB6"],
     "file_path": "C:/Users/YOUR_USER_NAME/Documents/MOZA Cockpit/PresetLibrary/{71ab7001-0014-4a70-8001-000000000004}.preset",
     "games": ["dcs_world"],
     "name": "AC7 Default",
     "product_type": "AS23",
     "uuid": "{71ab7001-0014-4a70-8001-000000000004}",
     "vehicles": "AC7 Default",
     "version": "1.0.1"
   }
   ```

8. Add this mapping to `Base.AB6.dcs_world` in `OptimalMatch.vconfig` in the same folder:

   ```json
   "AC7 Default": "{71ab7001-0014-4a70-8001-000000000004}"
   ```

9. Set up the button bindings as described in [Key bindings](#key-bindings).

### Manual uninstall

Delete these files and folders:

- `AC7RumbleTrace.asi`
- `AC7NativeInput.asi`
- `ue4ss/Mods/AC7Telemetry/`

Then remove the `AC7Telemetry` entries from `ue4ss/Mods/mods.txt` and `ue4ss/Mods/mods.json`.

If `Input.ini` uses extended button bindings, change them back to button numbers supported by the base game. To remove the Cockpit preset, reverse the installation steps.

The dependencies do not need to be removed if other mods still use them.

## Usage

1. Open Cockpit and run `DCS.exe` in the project folder (the bridge); Cockpit automatically detects the DCS process and connects. If Cockpit was already running during installation, restart it to load the new preset.
2. Start the game.

## Key bindings

Configuration file: `%LOCALAPPDATA%\BANDAI NAMCO Entertainment\ACE COMBAT 7\Config\Input.ini`

Use `presets/input/Input.ini` as a reference. Cockpit shows the button numbers and axis names. A `:R` after an axis name in the file means the input direction is inverted.

For matching peripheral models, the installation script writes the corresponding bindings into the existing configuration.

For other devices:

1. Find that device's `[Joystick-…]` section in `Input.ini`; locate it by `ProductName`.
2. Copy the `Flight_*` and `UI_*` entries you need into that section, replacing the right-hand side with your actual button or axis.
3. Change `device_type`, `devices` and `product_type` in the Cockpit preset file, and update the device names in `PresetIndex.index` and `OptimalMatch.vconfig` accordingly (unverified).

## Known limitations

All aircraft share one default preset; there are no per-aircraft presets.

Only AB6 + MHG + MTQ have been tested; other device combinations are untested.

## Building from source

Requirements: .NET 8 SDK, x64 MSVC and CMake.

Run from the project folder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build.ps1
```

Output:

```text
DCS.exe
payload/
  AC7RumbleTrace.asi
  AC7NativeInput.asi
```

Once built, install the patch as usual.

## License

Released under [GPL-3.0](LICENSE). Bundled third-party components keep their own licenses; their texts are in [licenses/](licenses/).
