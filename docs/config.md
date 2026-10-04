# WatchBar 参数参考

参数位置：mod 自己的 `uimd.ini`，`[WatchBar]` 节，键名为 `WatchBar.<键>`。面板没有
独立的 ini 文件；不写该节时全部使用内置默认值。可直接追加的默认值块见
[uimd-sample.ini](uimd-sample.ini)。

`uimd.ini` 由引擎在启动时读取一次，`rulesmd.ini` 中的素材键同理：改完需重开游戏。

## 写法

| 类型 | 写法 |
|---|---|
| 整数 | 十进制，或 `0x` 十六进制 |
| 布尔 | `1`/`0`，也接受 `yes`/`no`、`true`/`false`、`on`/`off` |
| 颜色 | `R,G,B`（0..255）或 `0xRRGGBB` |
| 路径 | 绝对路径；留空表示使用默认位置 |

- 键名与节名大小写不敏感；`WatchBar.` 前缀可省略（本文档统一写上）。
- 行首 `;` 或 `#` 为注释；`键 = 值` 的等号两侧可有空格。
- 无法识别的键名记入 `WARNING: unrecognised key(s)`；越界的数字与颜色分量被夹到边界
  并记 `out of range`；格式错误的颜色保留默认值并说明原因。
- 面板上边缘与左边缘用屏幕绝对坐标；可视带下边缘按视口底边
  （`DSurface::ViewBounds`）计算，不含侧边栏与黑边。
- 启动时日志记录读到的键数与来源；没有 `[WatchBar]` 节时会说明全部使用默认值。

## 几何

| 键 | 默认 | 范围 | 说明 |
|---|---|---|---|
| `WatchBar.PanelX` | 0 | −4096..4096 | 面板左边缘。0 表示紧贴视口左边 |
| `WatchBar.TopMargin` | 10 | −4096..4096 | 上边缘的固定部分 |
| `WatchBar.TopPercent` | 25 | 0..100 | 上边缘再下移视口高的该百分比。用百分比是为了在不同分辨率下保持相同的画面占比 |
| `WatchBar.BandMargin` | 4 | 0..512 | 可视带下边缘 = 视口底 − 该值。超出的图标行整行不绘制，进入滚动区 |
| `WatchBar.LabelWidth` | 60 | 0..512 | 标签列宽度（国旗 + 玩家名）。默认 60 = 47px 国旗居中 + 名字 |
| `WatchBar.CellWidth` | 62 | 16..512 | 一个 cameo 格子宽（含边框）。应与素材实际宽度一致 |
| `WatchBar.CellHeight` | 50 | 16..512 | 格子高 |
| `WatchBar.RowGap` | 2 | 0..64 | 玩家行之间的竖直间距 |
| `WatchBar.IconsPerLine` | 4 | 1..**16** | 一行（列）放几个图标后换行。硬上限 16 |
| `WatchBar.MaxLinesPerPlayer` | 3 | 1..**8** | 每个玩家最多几行图标（硬上限 8）。整行格数 = 本项 × `IconsPerLine`（默认 12），超出的按排序规则从尾部丢弃。仅 `LinesMode=fixed` 时读取 |
| `WatchBar.LinesMode` | `fixed` | `fixed` / `compact` / `loose` / `ultra` | 每个玩家的行数从哪来，见「每个玩家的行数」 |
| `WatchBar.LinesCap` | 8 | 1..**8** | 自适应模式下每位玩家的行数上限（硬上限 8）。`fixed` 时无效 |
| `WatchBar.MaxRows` | 8 | 1..**8** | 最多几行（几个玩家）。硬上限 8 |
| `WatchBar.CameoWidth` | 60 | 1..512 | cameo 图案宽 |
| `WatchBar.CameoHeight` | 48 | 1..512 | cameo 图案高 |
| `WatchBar.CameoOffsetX` | 1 | −64..256 | cameo 在格子内的横向偏移 |
| `WatchBar.CameoOffsetY` | 1 | −64..256 | cameo 在格子内的纵向偏移 |
| `WatchBar.FlagWidth` | 47 | 0..256 | 国旗宽（仅用于居中计算） |
| `WatchBar.FlagHeight` | 23 | 0..256 | 国旗高 |
| `WatchBar.ToggleWidth` | 10 | 0..256 | 右侧开关条宽 |
| `WatchBar.ToggleHeight` | 50 | 0..256 | 开关条高 |
| `WatchBar.ScrollButtonWidth` | 10 | 2..256 | ▲▼ 翻页按钮宽。与开关条无关，两者各自独立 |
| `WatchBar.ScrollButtonHeight` | 14 | 2..128 | ▲▼ 翻页按钮高。写了 `UpPCX` / `DownPCX` 时素材按此尺寸直接贴图，不做缩放 |
| `WatchBar.ScrollButtonOffsetX` | 0 | −4096..4096 | ▲▼ 一对按钮的水平位置：两键并拢、整体居中在面板实际占用宽度的中间，本项是这个居中位置上的左右平移量。面板比这一对还窄时向左被夹在面板左边缘 |

`MaxRows` / `MaxLinesPerPlayer` / `IconsPerLine` 决定 DLL 内部数组尺寸（编译期常量），
不能改大：写入更大的值会被夹到上限并记入日志。面板内部按类型记账的数组也是编译期常量
（`kHardMaxTypesPerGroup` = 64），但不是参数，正常对局不会触及。自适应模式算出的行数
同样被夹到硬上限 8。

### 每个玩家的行数

`WatchBar.MaxLinesPerPlayer` 是**截断预算**，不是占位高度：面板高度由内容决定，一个只
显示两种类型的玩家就只占一格高，预算再大也一样。`WatchBar.LinesMode` 决定预算从哪来：

| 值 | 含义 |
|---|---|
| `fixed` | 用 `WatchBar.MaxLinesPerPlayer`。默认值 |
| `compact` | **自适应**：可视带能放下的整行数按当前玩家数分配，保证面板不滚动 |
| `loose` | 同上，总预算再增加"1 行 × 玩家数"（人人内容都多时相当于每人 n+1 行，会滚动） |
| `ultra` | 同上，增加"2 行 × 玩家数"（相当于 n+2） |

```
visLines = (视口高 − BandMargin − TopMargin − 视口高×TopPercent%) / (CellHeight + RowGap)
n        = visLines / 玩家数                  // 人均份额，每人至少 1 行
budget   = visLines + extra × 玩家数           // extra = 0 / 1 / 2（compact / loose / ultra）
```

- **每行至少 1 行**：空玩家也要放国旗与名字，所以 `n = 0` 不是"这一行更矮"而是"这一行
  消失"。可视带比玩家数还矮时（例如 8 人局 `visLines=6`）面板照旧滚动。
- **用不掉的行给最需要的玩家**（max-min 公平）：一个只想显示 2 种类型的玩家用不完自己
  的份额，剩下的行会流给内容多的玩家。因此保证的是**每个想要更多的玩家至少拿到 n 行**，
  而不是"人人正好 n 行"。上限是 `WatchBar.LinesCap`，不是 n；自适应模式下
  `WatchBar.MaxLinesPerPlayer` 不再被读取，日志会说明这一点。`visLines` 除不尽玩家数时，
  多出来的行按玩家顺序给最需要的那个，所以同行可能相差 1 行。
- **内容少不浪费高度**：预算是"最多能画几行"，不是"预留几行"。
- `compact` 下只要玩家数 ≤ `visLines`，面板就不会滚动，▲▼ 自动变暗；`loose` / `ultra`
  允许超出，**只要有任意一位玩家填满，整块面板就滚动**（不是那一行单独滚）。
- **被击败的玩家不再占行**（引擎不会回收其出生位，但被击败方不留下任何部队），所以人少
  之后其余玩家的行数会自动变多。被击败的**本机**玩家是否还能看到面板由
  `WatchBar.ShowWhenDefeated` 决定。

以默认参数（`CellHeight=50`、`RowGap=2`、`TopMargin=10`、`TopPercent=25`、
`BandMargin=4`）为例，视口高 ≈ 窗口高：

| 视口高 | visLines | 2 人 | 4 人 | 8 人 |
|---|---|---|---|---|
| 800×600 | 8 | 4 行（16 格） | 2 行（8 格） | 1 行（4 格） |
| 1024×768 | 10 | 5 行 | 2 行 | 1 行 |
| 1280×960 | 13 | 6 行 | 3 行 | 1 行 |
| 1920×1080 | 15 | 7 行 | 3 行 | 1 行 |

（每格 = 一个 cameo；`IconsPerLine=4` 时"行数 × 4 = 格数"。）

8 人局下 `compact` 只有 4 格，`production` 块（最多 5 格）加 `building` 块就能占满整行：
满员局一般用 `loose` / `ultra`，并调小 `WatchBar.MaxIconsPerGroup`（例如
`production:2,building:2`）。`tools\check_ini.bat` 可离线打印同一张表。

## 素材（rulesmd.ini）

面板不推算素材名（不按 `%d` 拼 `swsideNN` / `cN_flag`），而是读取对应节中已有的那一行；
图片的名称与阵营在 `[Sides]` 中的顺序都不影响读取。

### 边框与开关条：阵营自己的节

写在 `rulesmd.ini` 中该阵营自己的节（`[Sides]` 中的名字，例如 `[GDI]`）：

| 键 | 默认 | 说明 |
|---|---|---|
| `WatchBar.CenterPCX` | 无 | 格子边框（cameo 外的凹槽）。EC 的 `swsideNNcenter.pcx` 即此项 |
| `WatchBar.OnPCX` | 无 | 开关条：面板展开时的样式 |
| `WatchBar.OffPCX` | 无 | 开关条：面板收起时的样式 |
| `WatchBar.UpPCX` | 无 | 底端并拢一对中的左键（向上） |
| `WatchBar.DownPCX` | 无 | 底端并拢一对中的右键（向下） |

```ini
[GDI]
WatchBar.CenterPCX=swside01center.pcx
WatchBar.OnPCX=swside01on.pcx
WatchBar.OffPCX=swside01off.pcx
WatchBar.UpPCX=swside01up.pcx
WatchBar.DownPCX=swside01down.pcx
```

面板只读取这五个键，不读取也不跟随任何其他 mod 的侧边栏配置。`UpPCX` / `DownPCX` 不写
时 ▲▼ 退回代码自绘的三角（`ScrollGlyph*Color` 三色）；`CenterPCX` 不写时**不绘制格子
底色**，cameo 直接画在战场上（不填充任何替代色块）；开关条两个键都不写时退化为自绘的
左/右箭头（◀ / ▶），箭头指向面板所在的一侧：面板展开时指左（开关条贴在面板右缘），
收起时指右（面板将从开关条右侧展开），因此没有素材也能一眼看出当前状态。面板在任何
一种缺失下仍可使用。

翻页按钮的素材按 `WatchBar.ScrollButtonWidth` × `ScrollButtonHeight` 直接贴图，不做
缩放：素材实际尺寸与这两个值不一致时改参数，不要让素材拉伸。素材只有「上」「下」各
一张，因此按钮状态用一层半透明色罩表示——不可滚动时罩上 `CountChipColor`（变暗），按住
时罩白色（变亮）。两个按钮**并拢贴在一起**（中间没有间距，所以没有 `ScrollButtonGap`
这类参数），整体居中在面板**实际占用宽度**的中间，水平位置用
`WatchBar.ScrollButtonOffsetX` 平移；占用宽度变化时这一对是整体滑动补位的。

整板使用本机玩家（观战者）自己阵营的皮肤，不按行切换：行身份由国旗与玩家名表示，边框
只是装饰。素材实际尺寸与 `WatchBar.CellWidth` / `CellHeight` / `ToggleWidth` /
`ToggleHeight` 不一致时改这四项参数，不要让素材拉伸。

### 国旗：国家自己的节

写在 `rulesmd.ini` 中该国家自己的节：

```ini
[British]
File.Flag=C4_FLAG.PCX
```

`File.Flag` 是引擎绘制国旗本来使用的键，面板读取同一行；写裸键 `Flag=` 也接受。文件名
只来自这一行，不按国家索引拼 `c<N>_flag.pcx`，国家在 `[Countries]` 中的位置不影响
读取。未写该键、或素材加载失败的国家不绘制国旗，名字照常显示。国旗图自带的洋红底色
（`255,0,255`）是 PCX 的默认透明色，由引擎扣掉。

### 时钟

| 键 | 默认 | 说明 |
|---|---|---|
| `WatchBar.ClockEnabled` | 1 | `1` = 绘制 gclock2 建造时钟（在造进度的唯一显示）；`0` = 在 cameo 中央显示百分比数字。建筑造好待放置的格子不画时钟，只显示 `WatchBar.DoneText`；单位造完不显示文字。素材缺失时自动退回数字显示 |

## 颜色

| 键 | 默认 | 说明 |
|---|---|---|
| `WatchBar.DoneColor` | 120,255,140 | 建筑造好、等待放置时的文字颜色 |
| `WatchBar.ProgressTextColor` | 255,255,255 | 无时钟时显示的进度数字颜色 |
| `WatchBar.QueueTextColor` | 255,255,255 | 在造格右上角 `+N` 角标的文字颜色（底板与数量角标同为 `CountChipColor`；N = 同类型还有几个在排队） |
| `WatchBar.CountChipColor` | 36,36,36 | 单位数量角标的底板（也是 ▲▼ 不可滚动时的色罩） |
| `WatchBar.CountTextColor` | 255,255,255 | 单位数量文字 |
| `WatchBar.IdleTextColor` | 130,130,130 | 无对局数据时的占位文字（默认取 CSF `TXT_WAITING`） |
| `WatchBar.ScrollTrackColor` | 40,40,40 | 面板左缘 2px 滚动指示条的轨道 |
| `WatchBar.ScrollThumbColor` | 185,185,185 | 滚动指示条的滑块 |
| `WatchBar.ScrollGlyphActiveColor` | 200,200,200 | ▲▼ 三角：可滚动 |
| `WatchBar.ScrollGlyphIdleColor` | 110,110,110 | ▲▼ 三角：已到顶端或底端 |
| `WatchBar.ScrollGlyphHeldColor` | 255,255,255 | ▲▼ 三角：按住中 |
| `WatchBar.ToggleGlyphOnColor` | 210,210,210 | 开关条自绘箭头（◀/▶）的颜色，仅在没写 `OnPCX` / `OffPCX` 时使用 |
| `WatchBar.ToggleGlyphOffColor` | 70,70,70 | 开关条自绘底板的颜色，仅在没写 `OnPCX` / `OffPCX` 时使用 |

玩家名使用该玩家自己的阵营颜色，不可配置。

## 界面文字

| 键 | 默认 | 上限 | 说明 |
|---|---|---|---|
| `WatchBar.DoneText` | `TXT_READY` | 23 字符 | 建筑造好、等待放置时覆盖在图标上的文字（`WatchBar.DoneColor` 色，居中）。单位在造格不显示 |
| `WatchBar.IdleText` | `TXT_WAITING` | 47 字符 | 尚无对局数据时的占位文字（`WatchBar.IdleTextColor` 色） |

两个值都**先当作 CSF 标签查游戏字符串表**，查到就用表里的文本，因此默认值会跟随游戏
语言（`TXT_READY` = 就绪，`TXT_WAITING` = 等待）。查不到时按字面文本绘制，所以
`WatchBar.DoneText=完成` 这类直接写文字也照常工作。只有纯 ASCII 的值会去查表（CSF 标签
都是 ASCII）；含非 ASCII 字符的值一律按字面文本处理。默认标签在本机 CSF 中不存在时
（非原版或被裁剪的字符串表），退回内置字面量 `Done` / `waiting for match...`；自己写错
的标签不会被兜底，屏幕上会直接显示该标签，以便发现拼写错误。

值按 UTF-8 解码，失败再按系统 ANSI 代码页解码，两种编码保存都能读到；可以写中文（游戏
会自动使用中文字体）。含控制字符或超长的值会被拒绝或截断并记入日志。含 CJK 的字符串由
游戏使用中文字体渲染，该字体没有 `%` 字形，因此这两个值中不要使用百分号。

字体不可配置：`GAME.FNT` 未逆向，`Point8` 是已验证可正常渲染的最大字号，面板固定使用
`Point8`（数量角标）与 `Point6Grad`（其余文字）。

## 动画与滚动

| 键 | 默认 | 范围 | 说明 |
|---|---|---|---|
| `WatchBar.FadeMs` | 100 | 0..5000 | 图标淡入/淡出时长（毫秒）。`0` = 关闭淡入淡出，图标立即出现/消失 |
| `WatchBar.MoveMs` | 90 | 0..5000 | 补位滑动时长（毫秒）。`0` = 关闭滑动补位，图标直接出现在新槽位。大于 0 时必须小于 `FadeMs`，否则补位图标尚未到位时幽灵已消失；写错会被自动改为 `FadeMs-1` 并记入日志（`FadeMs=0` 时不存在这个竞态，不再约束） |
| `WatchBar.ScrollStep` | 1 | 1..64 或 `page` | ▲▼ 每次滚动几行。数字为固定行数；`page` 表示一整屏，按当前能完整显示的行数滚动（能显示 6 行则滚 6 行），因此不同分辨率下都是一屏。也可写 `max` / `visible` / `0`。按住连滚的每一步使用同一值 |
| `WatchBar.ScrollRepeatDelayMs` | 350 | 0..10000 | 按住多久后开始连滚 |
| `WatchBar.ScrollRepeatRateMs` | 120 | 0..10000 | 连滚时每步间隔（毫秒） |
| `WatchBar.ScanIntervalMs` | 0 | 0..10000 | 重新扫描战场数据的间隔。`0` = 每帧扫描。单位数量很大的 mod 可设为 `100` 以减少开销；动画与绘制仍为每帧，只是数据刷新变慢 |

`ScrollStep=page` 时最后一屏可能不足一页（受滚动上限约束），这是有意为之：宁可最后一次
少滚，也不越过内容末尾。

## 显示内容

图标从左到右分为五块，默认顺序为：

```
建造中物件(production) → 现存建筑(building) → 现存载具(vehicle) → 现存战机(aircraft) → 现存步兵(infantry)
```

`production` 块内顺序默认为：建筑 → 军械 → 载具 → 战机 → 步兵（与块的顺序一致，不参与
`SortMode`，`WatchBar.ProductionOrder` 可调），每类最多一格、只在造时占格。舰船并入
载具格：船厂与战车工厂同时在造时，该格显示进度靠前的一条。

其余每块内部按 `SortMode` 排序，块与块的先后由 `GroupOrder` 决定。

| 键 | 默认 | 说明 |
|---|---|---|
| `WatchBar.ShowStructures` | 1 | 显示正在建造的建筑与防御建筑（`production` 块）。与 `CountBuilding`（现存建筑）相互独立 |
| `WatchBar.ShowUnitProduction` | 0 | 显示正在生产的载具/战机/步兵（`production` 块）。与 `ShowUnits`、`Count*` 相互独立：只看在造可写 `ShowUnits=0` + 本项 1。单位造完不显示 `DoneText` |
| `WatchBar.ShowUnits` | 1 | 单位计数块的总开关：控制载具、战机、步兵三块。单位在造格由 `ShowUnitProduction` 控制 |
| `WatchBar.CountBuilding` | 0 | `1` = 计数现存建筑（`building` 块，每种一个图标与数量）。独立于 `ShowUnits`。围墙、激光墙、火风暴墙不计，工厂内尚未放置的不计，防御建筑照常计数 |
| `WatchBar.CountInfantry` | 1 | 步兵块计数，仅在 `ShowUnits=1` 时生效 |
| `WatchBar.CountVehicle` | 1 | 载具块计数（地面与水面），条件同上 |
| `WatchBar.CountAircraft` | 1 | 战机块计数，条件同上 |
| `WatchBar.MaxIconsPerGroup` | `building:3` | 每类别最多占几格，逗号列表 `<组名>:<格数>`，未列出的组不封顶。默认只给建筑封顶（中后期一个基地 8~15 种）。例：`building:2,infantry:4`。`production` 默认不封顶（最多 5 格），`production:2` 可还原 1.3 的两格观感 |
| `WatchBar.GroupOrder` | `production,building,vehicle,aircraft,infantry` | 每个类别的排列顺序，五个组名必须各写一次；写错、重复或漏写时保留默认顺序并记一行日志。排在最后的块最先被整行格数截断 |
| `WatchBar.ProductionOrder` | `building,ordnance,vehicle,aircraft,infantry` | production 块内顺序，五个名称必须各写一次；写错、重复或漏写时保留默认顺序并记一行日志。排在最后的格最先被 production 上限截断 |
| `WatchBar.ShowCountChip` | 1 | 显示数量角标。关闭后只有图标，不显示数量（在造格的 `+N` 角标不受影响） |
| `WatchBar.SortMode` | `tech` | `tech` = 科技等级高到低；`count` = 数量多到少；`name` = 规则名 A 到 Z。每块各自排序，在造块不参与排序。键值相同时按规则顺序排列，保证顺序稳定 |

### 显示上限

| 键 | 作用范围 | 超出时 |
|---|---|---|
| `WatchBar.LinesMode`（或 `WatchBar.MaxLinesPerPlayer`）× `WatchBar.IconsPerLine` | 单个玩家的行数／整行格数 | 从尾部截断（`GroupOrder` 中排最后的块先消失） |
| `WatchBar.MaxIconsPerGroup` | 单块格数 | 该帧不绘制，数量照常统计（角标不变小） |

面板内部按类型记账：一种类型一个数字，`SortMode` 决定块内顺序。除上述两项外没有统计
上限参数，正常对局不会触及内部容量。

`ShowUnits=0` 时 `CountInfantry` / `CountVehicle` / `CountAircraft` 全部无效（单位的
在造格不受影响，由 `ShowUnitProduction` 控制）；`CountBuilding=0` 时建筑块的
`MaxIconsPerGroup` 无效。这两种"已写但不生效"的组合，解析器会在 `WatchBar.log` 与
`tools\check_ini.bat` 中各记一行。只想显示某一类（例如只显示对方防线）可写
`CountBuilding=1` + `ShowUnits=0`；只看正在生产什么、不要兵力普查可写
`ShowStructures=0` + `ShowUnitProduction=1` + `ShowUnits=0`。

同一种建筑类型在一行中只占一格：建筑在造、地图上又有同类建筑时，只显示在造格（带进度
时钟），数量角标等它造好放下后恢复。单位不受此限：同种单位"在造 + 现存"两格同时显示
（动画表按 玩家 × 类型 × 是否在造格 记录）。

## rules 中的按类型键

写在 `rulesmd.ini` 的类型段中（不是 `[WatchBar]` 节）。面板读取游戏自己加载的 rules
（`CCINIClass::INI_Rules`），键名大小写不敏感，写法与其他 rules 键一致。两个键都不写时
行为不变。每个类型在换地图时解析一次并缓存；rules 由引擎加载后常驻内存，因此修改后需
重开游戏。

| 键 | 默认 | 说明 |
|---|---|---|
| `CountAs` | 空 | 把该类型的现存数量并入另一类型的计数：绘制目标的 cameo、数量相加、按目标的科技等级排序。用于部署/变形对（`SCHP`/`SCHD`、`PELI`/`PELID`、`NETH`/`NETHD`、`RETK`/`RETKD`、`FTTNK`/`FTTNK2`）与预装乘员变体（`BFRTAA`/`BFRTAG`/`BFRTAT`）这类"一个单位、几个图标"的情况。上限 512 个类型 |
| `IgnoreCount` | `no` | `yes` = 该类型完全不参与计数，也不在面板上出现。用于子机、奴隶、部署残留等类型。引擎自身能标记的支援单位（`SpawnOwner` / `Airstrike` 飞机、侦察机与伞兵任务、运输舱乘客、下沉中的船）本来就不计，此项用于 mod 自己的子机 |

`CountAs` 的解析规则：

- 只能在同类别内转换；跨类别（步兵 ↔ 载具）被拒绝，按自身计数并记一行日志。
- 链条会跟随（`A→B→C`）。成环时停在最后一个有效链接并记日志。
- 某一跳的目标不存在时，停在该跳的**上一个有效目标**并记一行日志：`A CountAs=B`、
  `B CountAs=<不存在>` 时，`A` 并进 `B`，不是并进 `A` 自己。
- 直接写了不存在的目标（`A CountAs=<不存在>`）时按自身计数。

`IgnoreCount=yes` 在三个环节生效：自身单位不进入计数桶（在 `CountAs` 合并之前过滤，因此
被忽略的类型不会计入它转向的目标）；作为 `CountAs` 的目标时也不计数（合并之后再过滤
一次）：`A` 写 `CountAs=B`、`B` 写 `IgnoreCount=yes` 时，`A` 同样不显示；正在建造的图标
也不绘制（`CollectProduction` 同样跳过，在造格一并消失）。

只想屏蔽某一类（例如不要建筑）时使用 `WatchBar.Count*` 开关；`IgnoreCount` 是按类型
屏蔽整个类型。

```ini
; rulesmd.ini
[SCHD]   CountAs=SCHP       ; 部署形态并进飞行形态，显示为 "SCHP x4"
[DRONE]  IgnoreCount=yes    ; 子机：不计数、不显示、建造图标也不绘制
```

## 显示条件

| 键 | 默认 | 说明 |
|---|---|---|
| `WatchBar.SpectatorOnly` | 1 | `1` = 只有观战者能看到面板。`0` = 参战者也显示，用于在真实对局中调整布局。发布版本应保持为 1，否则参战玩家会看到一块压在战场上、且开关条与 ▲▼ 命中框会吃掉点击的面板 |
| `WatchBar.ParticipantRows` | `own` | 参战者能看到谁的行，取值见「参战者看到谁」。观战者不受本项影响 |
| `WatchBar.ShowWhenDefeated` | 1 | `1` = 被打败或认输的玩家也视为观战者，能看到面板。被击败的玩家本身不再占行（无部队可显示），与自适应行数无关 |

判定为观战者的情况有三种：引擎的 Observer 标志、`CurrentPlayer->Defeated`（判负后引擎
不会重新分配 spawn 槽，只能靠该标志识别）、人类玩家但没有 spawn 槽。

"面板显不显示"和"看到谁的行"是两件事：`SpectatorOnly` 决定前一件，`ParticipantRows` 决定
后一件，而后者只对参战者生效——观战者看到的是全部参战者，没有键能让观战者只看友军。

### 参战者看到谁

`WatchBar.ParticipantRows` 只在 `SpectatorOnly=0`、本机玩家正在参战时读取：

| 值 | 含义 |
|---|---|
| `own` | 只有自己的行（含同一台电脑上由自己控制的房子）。默认值 |
| `allies` | 加上与自己结盟的房子，取自引擎自己的盟友表（和团队颜色、小地图用的是同一份判断），AI 盟友同样算 |
| `all` | 全部参战者，与观战者所见一致。调布局时想看全板就用它 |

参战者自己的行在三档里都在。行的顺序始终是 `HouseClass::Array` 的顺序（观战者与参战者
一致），`WatchBar.MaxRows` 从尾部截断——所以把 `MaxRows` 调到小于可见行数时，被截掉的
可能是自己那一行。

## 日志

| 键 | 默认 | 说明 |
|---|---|---|
| `WatchBar.LogEnabled` | 1 | 是否写日志。发布版本可设为 0 |
| `WatchBar.LogLevel` | 2 | `0` 不写；`1` 只写 `WARNING` / `ERROR`；`2` 加上状态变化（行数、cameo 解析、门控翻转、滚动状态）；`3` 全部（当前与 2 相同） |
| `WatchBar.LogMaxKB` | 1024 | 日志体积上限，超过后停止写入 |
| `WatchBar.LogPath` | 空 | 留空 = `<游戏根目录>\WatchBar.log` |

无法识别的键名、越界的数字、格式错误的颜色都记录在日志中。日志第一行是版本横幅
（`==== WatchBar 1.4.0 (编译日期 时间) ====`），随后一行记录读到的键数与来源；版本号固定
内置在程序里，不是参数。

## 兼容性自检

宿主 exe 的时间戳固定按 YR 1.001 的 `gamemd.exe`（`0x3BDF544E`）比对，不是参数。面板的
两个钩子点（`0x4F4583` / `0x533066`）是该版本的绝对地址，修改或加壳过 exe 的 mod 可能
不再是原指令。启动时比对一次，不一致只写 WARNING，不阻止运行。

## 记录用（读取但忽略）

`WatchBar.Version` 与 `WatchBar.Notes` 会被接受并忽略，程序不读取，可用于记录改动备注。
程序自身的版本号内置在程序里并写在日志第一行，与本项无关。
