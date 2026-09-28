# WatchBar

尤里的复仇游戏内观战面板。Syringe 注入式 DLL，在游戏画面中为显示各玩家的生产与兵力概览。

## 功能

- **自定义参展方能否使用**：还可参战方显示的统计阵营（自己、盟友、全部）。
- **自定义图标背景、按钮**：使用PCX自定义面板样式。
- **自定义图标显示行数及列数（有硬编码上限）**：还可根据玩家数自适应行数。
- **自定义统计信息类型**：自定义是否统计（建筑、步兵、载具、飞行器）。
- **自定义支持统计在建建筑**：可统计在建的建筑，并且显示建筑进度百分比。
- **自适应行数**：按总玩家数自动计算行数分配。
- **自定义类别**：可以忽略类别，或合并类别。
- **自定义动画效果**：图标淡入淡出等效果。
- **自定义滚动模式**：超过行数支持不同的滚动模式。

## 安装

| 启动方式 | 操作 |
|---|---|
| CnCNet 客户端（存在 `Resources\ClientDefinitions.ini`） | 执行 `deploy.bat --apply`；或手工在 `ExtraCommandLineParams` 中追加 `-i=WatchBar.dll` |
| Syringe 快捷方式 | 将 `WatchBar.dll` 放入游戏根目录，命令行加 `-i=WatchBar.dll` |
| 其他启动器 | 同上：让 Syringe 加载该 DLL |

## 参数

参数位置为 `uimd.ini` 的 `[WatchBar]` 节，键名为 `WatchBar.<键>`。

```ini
[WatchBar]
WatchBar.TopPercent=25          ; 面板上边缘 = 10 + 视口高 × 25%
WatchBar.CellWidth=62           ; 一个 cameo 格子宽（含边框）
WatchBar.IconsPerLine=4         ; 一行（列）放几个图标（硬上限 16）
WatchBar.MaxLinesPerPlayer=3    ; 每个玩家最多几行图标（硬上限 8）
WatchBar.LinesMode=fixed        ; fixed / compact / loose / ultra：行数是否自适应
WatchBar.LinesCap=8             ; 自适应模式下每位玩家的行数上限（硬上限 8）
WatchBar.CountChipColor=36,36,36
WatchBar.DoneColor=120,255,140
WatchBar.DoneText=TXT_READY     ; 造好待放置时覆盖的文字，默认取该 CSF 标签
WatchBar.ScrollStep=1           ; ▲▼ 每次滚动几行；page = 一屏，也可写 2/3
WatchBar.SpectatorOnly=1        ; 1 = 只有观战者能看到面板
WatchBar.ParticipantRows=own    ; 参战者看谁：own / allies / all（上一项为 0 时才读取）
```

完整键表见 [docs/config.md](docs/config.md)；可直接追加到 `uimd.ini` 的参数块
（逐行注释，全部为默认值）见 [docs/uimd-sample.ini](docs/uimd-sample.ini)。


可使用以下工具，在不启动游戏的情况下校验参数：
```bat
tools\check_ini.bat "D:\Game\Extracted\uimd.ini"
```

## 素材

格子边框、开关条与 ▲▼ 翻页按钮读取 `rulesmd.ini` 中该阵营自己的节：

```ini
; 阵营节：格子边框、开关条与翻页按钮
[GDI]
WatchBar.CenterPCX=swside01center.pcx
WatchBar.OnPCX=swside01on.pcx           ; 面板展开
WatchBar.OffPCX=swside01off.pcx         ; 面板收起
WatchBar.UpPCX=swside01up.pcx           ; 翻页按钮：并拢一对中的左键（向上）
WatchBar.DownPCX=swside01down.pcx       ; 翻页按钮：并拢一对中的右键（向下）
```

五个 `WatchBar.*PCX` 都可不写，而且只读取这五个键——面板不读取也不跟随任何其他 mod
的侧边栏配置。不写时，格子不绘制底色（cameo 直接画在战场上，不填充任何替代色块），
开关条退化为一条横线，翻页按钮退回代码自绘的三角，面板仍可使用。

## 统计口径

- **单位** = 地图上现存的（出厂 +1、阵亡 −1）。运输舱乘客（`InLimbo`）不计；正在
  下沉的船不计；V3/无畏的导弹、鲍里斯的支援机、侦察机、伞兵等支援单位不计。
- **建筑** = `WatchBar.CountBuilding=1` 时按类型计数（默认关闭）。只统计站在
  地图上的：围墙、激光墙、火风暴墙不计，工厂内尚未放置的不计。
- 类别的开关、块的先后、每块的格数分别由 `WatchBar.Count*`、
  `WatchBar.GroupOrder`、`WatchBar.MaxIconsPerGroup` 控制。后两项只是显示上限：
  被挤掉的类型仍按真实数量统计，只是不绘制。
- `rulesmd.ini` 中有两个可选的按类型键：
  `CountAs=` 把变形形态的数量并入另一类型（例 `[SCHD] CountAs=SCHP`）；
  `IgnoreCount=yes` 使该类型完全不计数、不显示（例 `[DRONE] IgnoreCount=yes`）。
  两个键都不写时行为不变。跨类别的 `CountAs`（步兵 ↔ 载具）会被拒绝。
  这两个键在换地图时读取一次。

## 硬上限

`WatchBar.MaxRows` / `WatchBar.MaxLinesPerPlayer` / `WatchBar.IconsPerLine`
决定 DLL 内部数组尺寸（编译期常量），不能改大：写入更大的值会被夹到上限并记入日志。
一个玩家的图标格数 = 行数 × `IconsPerLine`（固定模式为 `MaxLinesPerPlayer` ×
`IconsPerLine`，默认 3 × 4 = 12；自适应模式算出的行数同样被夹到硬上限 8）。
面板内部按类型记账，每类有 64 个类型的编译期上限（不是参数）；正常对局不会触及。

## 从源码构建

```bat
git clone --recurse-submodules <repo>
cd WatchBar
build.bat
deploy.bat            :: 预演
deploy.bat --apply    :: 写入
```

- 需要 VS2022 C++ 工具链（Build Tools 即可）。`build.bat` 会自行查找
  `vcvars32.bat`，也可用 `build.bat --vcvars "C:\path\to\vcvars32.bat"` 指定。
- 依赖 [YRpp](https://github.com/Phobos-developers/YRpp) 头文件（`YRpp/`）。
- 编译参数中的 `/std:c++20`、`/DNOMINMAX`、`/DSYR_VER=2` 不可删除。缺少
  `/DSYR_VER=2` 会生成能加载但钩子不生效的 DLL，`build.bat` 会在编译后检查
  `.syhks00` 节并在缺失时判定失败。

```
src/WatchBar.cpp       面板实现（绘制、收集、门控、gadget、钩子）
src/Config.h           参数结构、硬上限、契约说明
src/Config.cpp         uimd.ini [WatchBar] 解析与校验
docs/config.md         参数参考
docs/uimd-sample.ini   可整段追加到 uimd.ini 的参数块
tools/check_ini.bat    离线参数校验
```

## 兼容性

| 环境 | 状态 |
|---|---|
| EC（Ares + Phobos，CnCNet 客户端） | 已实机验证 |
| 原生 YR 1.001 + Ares | 未测试（cameo 走 SHP 路径） |
| 原生 YR 1.001，无 Ares | 未测试（使用 `Cameo=`，`CameoPCX=` 不生效） |
| 无 swside 素材的 mod | 未测试（格子不绘制底色，cameo 直接画在战场上） |
| 修改或加壳过 `gamemd.exe` 的 mod | 不支持：钩子点是 YR 1.001 的绝对地址，启动时会比对 exe 时间戳并写 WARNING |

`CameoPCX=` 由 Ares 定义，无 Ares 时 art 里不会有这个键，面板退回引擎的 `Cameo=`
SHP 路径；该键存在时由面板自己加载 PCX，不调用 Ares。

运行时不依赖 Ares 或 Phobos：该 DLL 只读取游戏自身的 `HouseClass` /
`FactoryClass` / 单位数组，不使用 `ReadProcessMemory`，不需要管理员权限；素材也只从
`WatchBar.*PCX` 这几个键读取，不读取任何其他 mod 的侧边栏配置。

## 已知限制

- **点击穿透**：面板绘制在战场上，落在面板上的鼠标事件会传递给下方地图。
- **鼠标滚轮不可用**：引擎限制，不支持滚轮。
- **参数不热重载**：所有 INI 配置由引擎在启动时读取一次，修改后需重开游戏。

## 致谢

机制参考 [Phobos](https://github.com/Phobos-developers/Phobos) 的超武侧边栏
（`GadgetClass` + `GScreenClass::AddButton`）与游戏内调试面板（`ObjectInfo.cpp`，
同一绘制钩子点）；结构定义来自
[YRpp](https://github.com/Phobos-developers/YRpp)；注入框架为
[Syringe](https://github.com/Phobos-developers/SyringeEx)。

PCX 素材不在本仓库内，由各 mod 自行提供。
