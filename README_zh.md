# AC7 MOZA Flight Feedback

[English](README.md) | 中文

为《皇牌空战 7》提供 MOZA Cockpit 飞行力反馈桥接，并扩展游戏原生按钮绑定以兼容更多外设按键。

安装方式可选**脚本安装**或**手动安装**。

## 工作原理

- **飞行力反馈**：采集内部飞行数据，将其转换为 Cockpit 的 DCS 遥测格式，由 Cockpit 计算杆力。
- **原生震动**：获取游戏原生震动请求与状态，同步驱动外设产生震动。
- **按钮扩展**：读取每设备最多 128 个按钮的完整状态，接入游戏原生按钮查询。

功能均在运行时生效，不修改游戏 EXE 或资源文件。

## 安装

### 前置依赖

| 依赖 | 说明 |
| --- | --- |
| [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)（x64） | 加载 ASI 模组 |
| [UE4SS](https://www.nexusmods.com/acecombat7skiesunknown/mods/2474)（含 UEHelpers） | 加载飞行遥测脚本 |
| [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/zh-cn/download/dotnet/8.0)（Windows x64） | 运行桥接程序 |
| [MOZA Cockpit](https://www.mozaracing.com/downloads/) 与 AB6 | 计算飞行力及驱动底座 |

依赖需自行安装配置。

在游戏根目录，即包含 `Ace7Game.exe` 的目录，其结构应该是：

```text
ACE COMBAT 7/
  Ace7Game.exe
  dinput8.dll            ASI Loader
  dwmapi.dll             UE4SS 加载器
  ue4ss/
    UE4SS.dll
    UE4SS-settings.ini
    Mods/shared/UEHelpers/UEHelpers.lua
```

### 脚本安装

1. 运行一次 MOZA Cockpit，以生成预设库目录。
2. 连接外设启动一次游戏再退出，让游戏生成外设绑定配置文件。
3. 运行 `Install.cmd`。

脚本会自动查找游戏目录与预设库，复制 ASI、ini、Lua 与 Cockpit 预设，合并模组列表。匹配到 AB6/MTQ 时会自动备份 `Input.ini` 后写入对应绑定；其他设备型号则需要自行编辑，参考[按键绑定](#按键绑定)。

找不到游戏时指定目录：

```powershell
.\Install.cmd -GameDirectory 'D:\Games\ACE COMBAT 7'
```

### 脚本卸载

运行 `Uninstall.cmd` 将自动移除本补丁直接添加的模组文件、震动配置、模组列表条目及 Cockpit 预设；外设绑定配置不会自动移除。

需要指定目录时：

```powershell
.\Uninstall.cmd -GameDirectory 'G:\Games\ACE COMBAT 7'
```

### 手动安装

1. 运行一次 Cockpit，以生成预设库。
2. 连接外设启动一次游戏再退出，让游戏生成外设绑定配置文件。
3. 复制模组文件：

   | 本包文件 | 复制到 |
   | --- | --- |
   | `payload/AC7RumbleTrace.asi` | 游戏根目录 |
   | `payload/AC7ExpandedInput.asi` | 游戏根目录 |
   | `presets/RumbleSettings.ini` | 游戏根目录 |
   | `mod/AC7Telemetry/` | `ue4ss/Mods/AC7Telemetry/` |

4. 在 `ue4ss/Mods/mods.txt` 中添加：

   ```text
   AC7Telemetry : 1
   ```

5. 在 `ue4ss/Mods/mods.json` 中添加：

   ```json
   [
     { "mod_name": "AC7Telemetry", "mod_enabled": true }
   ]
   ```

6. 将 `presets/{71ab7001-0014-4a70-8001-000000000004}.preset` 复制到 `%USERPROFILE%\Documents\MOZA Cockpit\PresetLibrary\`。

7. 在同目录 `PresetIndex.index` 的 `presets` 数组中加入以下记录，**`file_path` 需替换为实际完整路径**：

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

8. 在同目录 `OptimalMatch.vconfig` 的 `Base.AB6.dcs_world` 中加入以下映射：

   ```json
   "AC7 Default": "{71ab7001-0014-4a70-8001-000000000004}"
   ```

9. 写入按键绑定，详见[按键绑定](#按键绑定)。

### 手动卸载

删除以下文件与目录：

- `AC7RumbleTrace.asi`
- `AC7ExpandedInput.asi`
- `RumbleSettings.ini`
- `ue4ss/Mods/AC7Telemetry/`

再从 `ue4ss/Mods/mods.txt` 与 `ue4ss/Mods/mods.json` 中删除 `AC7Telemetry` 条目。

若 Input.ini 使用了扩展按键绑定，应改回游戏原生支持的外设按键编号。若需删除 Cockpit 预设，参考安装过程反向操作。

前置依赖若仍被其他模组使用则无需删除。

## 使用

1. 打开 Cockpit，运行项目根目录的 `DCS.exe`（桥接程序）；Cockpit 会自动识别并建立连接。若安装时 Cockpit 已在运行，需要重启它以读取新写入的预设。
2. 启动游戏。

## 震动设置

参考 `RumbleSettings.ini` 内部注释进行编辑。修改后需重启游戏。

## 按键绑定

配置文件：`%LOCALAPPDATA%\BANDAI NAMCO Entertainment\ACE COMBAT 7\Config\Input.ini`

可参考 `presets/input/Input.ini` 配置绑定。在 Cockpit 中可以查看按键编号或轴名称。配置文件中轴名称后的 `:R` 表示反转输入方向。

外设型号相同时，安装脚本会自动将匹配设备的绑定写入现有配置。

使用其他设备时：

1. 在 `Input.ini` 中找到对应该设备的 `[Joystick-…]` 段，可通过 `ProductName` 定位。
2. 参考样例将需要的 `Flight_*`、`UI_*` 条目抄入该段，右侧改为实际按钮或轴。
3. 在 Cockpit 预设文件中改 `device_type`、`devices`、`product_type`，并同步 `PresetIndex.index` 与 `OptimalMatch.vconfig` 的设备名（未验证）。

## 已知限制

所有飞机共用一套默认预设，没有做分机型预设。

仅测试 AB6 + MHG + MTQ，未测试其他设备组合。

## 从源码构建

环境：.NET 8 SDK、x64 MSVC 与 CMake。

在项目根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build.ps1
```

产物：

```text
DCS.exe
payload/
  AC7RumbleTrace.asi
  AC7ExpandedInput.asi
```

编译后即可按照常规安装方式使用。

## 许可证

本项目以 [GPL-3.0](LICENSE) 发布。随包的第三方组件保留各自的许可证。
