# Optional native icing log

独立调查工具，问题解决后计划整套移除。本目录包含日志exe、采集ASI以及`source/`下的独立源码和构建入口，不属于主功能src。

## 使用

正常退出游戏，将本目录的`AC7IcingObserver.asi`手动复制到Ace7Game.exe旁，再正常启动游戏。日志的安装和移除完全手动，主安装/卸载脚本不处理本组件。ASI由已有ASI Loader加载，不依赖主震动ASI、输入ASI或DCS.exe。

双击本目录`AC7RumbleLog.exe`，无需参数。工具自动等待唯一游戏及自己的采集通道；只有日志工具在接收时采集器才发布事件。原生26请求/求值触发后，才在exe旁创建`rumblelog-时间戳.csv`；未命中时不创建日志。屏幕HUD的ICING提示不等于原生26事件。

触发前在内存保留最多30秒/8MiB上下文，10秒没有新26后收尾并继续等待。Ctrl+C或游戏退出时收尾。停止日志不控制主桥接；主桥接退出也不替日志退出。

## 移除

结束日志，正常退出游戏，删除游戏根目录的`AC7IcingObserver.asi`，即可删除本项目中的整个`optional-logging/`。主功能不受影响，已有采样文件自行保留。

## 构建

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\optional-logging\source\Build.ps1
```

独立构建本目录exe/ASI，不构建主功能；使用根目录`third_party/minhook`，许可在根目录`licenses/MinHook-LICENSE.txt`。需要.NET 8 SDK、x64 MSVC和CMake。构建不安装或输出设备。

采集器的三个入口与主震动接入点不重叠。上游request不证明游戏gate准入，已知恢复重提入口没有单独观察，因此日志不声称覆盖所有请求路径。真实Icing26仍未捕获。
