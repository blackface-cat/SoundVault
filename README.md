<div align="center">

<img src="src/assets/logo.png" width="88" alt="Sound Vault logo">

# 声库 Sound Vault

**面向声音设计师 / 音频后期的本地素材管理工作站**

Qt6 · C++17 · 资源管理器式直读 · Audition 风格波形/频谱 · UCS 自动分类 · 多维标签 · 内置 AI Agent 接口

</div>

---

## ✨ 功能特性

| 模块 | 说明 |
|------|------|
| 🗂️ 素材库直读 | 资源管理器式浏览，直接挂载本地文件夹，多库管理，无侵入式索引 |
| 📈 波形 / 频谱 | Audition 风格波形显示 + 频谱图（自研 radix-2 FFT，SVP2 缓存加速） |
| 🏷️ UCS 自动分类 | 内置 Universal Category System 分类树（15 组 839 分类），从文件名自动识别分类 |
| 🧩 多维标签 | 9 个维度 / 237 个标签的 facet 标签体系，可自定义增删改 |
| ⭐ 元数据管理 | 自定义标签、评分（0–5）、收藏分组、备注、最近播放/搜索历史 |
| ▶️ 播放控制 | 播放/暂停/停止/定位/音量/倍速(0.5–2.0)/循环 |
| ✂️ 波形框选导出 | 波形上框选片段，直接导出 WAV 或拖入宿主 DAW |
| 🔊 双声道显示 | 单声道/立体声自动识别，L/R 双轨波形 |
| 🤖 AI Agent 接口 | 内置本地 HTTP 接口，任何 AI Agent 均可接管控制（36 个工具，详见下文） |

## 🧰 技术栈

- **语言 / 框架**：C++17 · Qt 6.8（Widgets / Svg / Sql / Concurrent / Network）
- **音频播放**：[miniaudio](https://miniaudio.app/)（单头文件音频库）
- **数据存储**：SQLite（本地元数据库）
- **构建**：CMake ≥ 3.21 · MSVC / MinGW / AppleClang
- **打包（Windows）**：Inno Setup 7

## 🚀 构建

### 依赖

- Qt 6.5+（需要 `Widgets` `Svg` `Sql` `Concurrent` `Network` 五个组件）
- CMake ≥ 3.21
- 支持 C++17 的编译器

### 步骤

```bash
# 1) 配置（Windows / MSVC 示例）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_PREFIX_PATH="<你的Qt路径>/6.8.3/msvc2022_64"

# 2) 编译
cmake --build build --config Release

# 3) 可选：编译播放链路测试（控制台）
cmake -S . -B build -DBUILD_PLAYTEST=ON
cmake --build build --config Release --target PlayTest
```

> Linux / macOS 同理，把 `-G` 与 `CMAKE_PREFIX_PATH` 换成对应工具链即可。

### 打包（Windows）

```bash
# 1) 部署 Qt 运行时依赖
windeployqt --release --no-translations build/Release/ShengKu.exe

# 2) 用 Inno Setup 编译安装包（可选）
ISCC.exe installer.iss
```

## 📁 目录结构

```
.
├── CMakeLists.txt          # CMake 构建脚本
├── LICENSE                 # MIT 许可证
├── readme.md               # 本文件
├── installer.iss           # Inno Setup 打包脚本（Windows）
├── src/
│   ├── main.cpp            # 入口
│   ├── agent/              # AI Agent HTTP 接口（QTcpServer）
│   ├── audio/              # 音频引擎 / 波形 / 频谱分析
│   ├── db/                 # SQLite 元数据存储
│   ├── ui/                 # 主窗口 / 波形 / 文件列表 / 详情 / 播放面板
│   └── assets/             # 图标 / Logo 资源
├── data/
│   ├── ucs_tree.json       # UCS 分类树（15 组 839 分类）
│   ├── facets.json         # 多维标签定义（9 维度）
│   └── ucs/                # UCS 示例 CSV
├── tests/                  # 播放链路测试
└── third_party/miniaudio/  # 第三方音频库（单头文件）
```

## 🤖 AI Agent 接口

把本节内容直接发给任意 AI Agent，它即可通过本地 HTTP 接口接管本软件。

### 连接方式

软件启动后会在本机 `127.0.0.1` 上开一个 HTTP 服务，只监听本机，无需任何密钥。

- 默认端口 **8618**；若被占用会**自动顺延到 8622**（范围 8618–8622）。
- 确定端口：依次请求 `GET http://127.0.0.1:8618/status`、`8619`、`8620`、`8621`、`8622`，第一个返回 JSON 的即为当前端口。下文用 `{BASE}` 代指 `http://127.0.0.1:<端口>`。

### 三个接口

| 方法 | 路径 | 作用 |
|------|------|------|
| GET | `{BASE}/status` | 软件与素材库状态（版本、素材数、库数、维度/标签数、当前输出设备） |
| GET | `{BASE}/tools` | 全部工具清单（含每个工具的参数 schema；`/capabilities` 同义） |
| POST | `{BASE}/call` | 调用任意工具，body 为 JSON：`{"tool":"<工具名>","args":{...},"source":"<可选，记录来源>"}` |

`GET {BASE}/` 会返回一段简短提示（`app` / `hint` / `docs`）。

### 调用示例（curl）

```bash
# 1) 探活
curl http://127.0.0.1:8618/status

# 2) 取工具清单与参数
curl http://127.0.0.1:8618/tools

# 3) 搜索素材
curl -X POST http://127.0.0.1:8618/call \
  -H "Content-Type: application/json" \
  -d '{"tool":"audio.search","args":{"q":"脚步","limit":20}}'

# 4) 播放指定素材
curl -X POST http://127.0.0.1:8618/call \
  -H "Content-Type: application/json" \
  -d '{"tool":"player.play","args":{"path":"D:/素材/脚步_草地.wav"}}'
```

响应统一为 JSON，形如 `{"ok":true,"result":{...}}` 或 `{"ok":false,"error":"..."}`。

### 工具清单（36 个）

**能力 / 状态**：`agent.capabilities`、`agent.status`

**素材库**：`library.list`、`library.add`、`library.remove`、`library.browse`

**搜索 / 详情**：`audio.search`、`audio.get_details`、`audio.get_current`

**标签 / 多维 facet / UCS / 大类**：`tag.list`、`tag.add_to_files`、`tag.remove_from_files`、`facet.list`、`facet.set`、`ucs.list`、`ucs.set`、`ucs.detect`、`kind.set`

**收藏 / 评分 / 备注 / 历史**：`favorite.set`、`rating.set`、`notes.set`、`fav_group.list`、`fav_group.create`、`fav_group.add`、`fav_group.remove`、`history.recent`

**播放控制**：`player.get_state`、`player.play`、`player.pause`、`player.stop`、`player.seek`、`player.set_volume`、`player.set_speed`、`player.set_loop`

**高风险操作（需两步确认）**：`action.preview`（签发一次性确认令牌）、`action.execute`

### Agent 接管流程

1. 请求 `GET {BASE}/status` 探活、确认端口。
2. 请求 `GET {BASE}/tools` 拉取完整工具与参数 schema（schema 为准）。
3. 素材一律以「绝对路径」为标识（文件夹直读模式，无稳定数字 id）。
4. 读类直接返回结果；写类（打标/评分/收藏/备注/facet/UCS/大类）写审计日志并返回 `ok`；高风险操作走 `action.preview` → `action.execute` 两步确认。
5. 每次请求「一连接一请求」，响应读完即断开，可放心并发调用。

> 接口协议版本：`shengku.agent.v1`；应用标识：`Sound Vault`。

## 📄 第三方库

- **[miniaudio](https://miniaudio.app/)** — 音频播放引擎，单头文件，许可为 Unlicense 或 MIT-0（二选一），位于 `third_party/miniaudio/`。

## 📝 许可证

本项目采用 [MIT License](LICENSE)。

Copyright © 2026 [blackface-cat](https://github.com/blackface-cat)
