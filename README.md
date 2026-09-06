# 音乐转 MP3 🎵

一个轻量、快速、完全本地运行的 Windows 音频转换工具。它使用原生 C++ 和 Win32 界面，支持批量导入、拖拽转换、NCM 解码以及多种输出码率选择。

> 📦 下载即用：仓库中的 `MusicToMp3Native-Single.exe` 是不依赖 FFmpeg、MinGW 或 .NET 的单文件版本，复制到其他 Windows 电脑即可运行。

## ✨ 功能特性

- 📁 添加单个文件、整个文件夹或直接拖拽导入
- 🗂️ 输出目录支持直接拖入文件夹，自动填入目标路径
- ⚡ 后台扫描和批量转换，避免转换期间窗口卡死
- 📊 当前文件进度、总体进度和转换状态实时显示
- 🎧 支持 NCM AES/RC4 解码
- 🔊 通过 FFmpeg API 在进程内完成解码和 MP3 编码
- 🎚️ 支持 128、192、256、320 kbps 等输出码率
- 💾 可自定义输出目录，默认保存到用户音乐目录
- 🧾 保留标题、艺术家、专辑等音频元数据
- 🛠️ 失败文件单独列出并显示原因，不影响其他文件继续转换
- 🖥️ 原生 Windows 界面、高清 DPI 适配和应用图标
- 🔒 同一台电脑只允许运行一个程序实例，重复启动会激活已有窗口
- 🔒 全程本地处理，不上传音乐文件和转换记录

## 🎼 支持格式

可导入常见音频格式，包括：

`.aac` · `.ape` · `.aiff` · `.flac` · `.m4a` · `.mp4` · `.ncm` · `.ogg` · `.opus` · `.wav` · `.wma` · `.mp3`

所有输入最终输出为 `.mp3`。已有 MP3 文件在选择新的码率时会重新编码，以确保码率选择真正生效。

## 🚀 快速使用

1. 从 [Releases](https://github.com/crazyzhang277/MusicMP3/releases) 下载最新的 `MusicToMp3Native-Single.exe`。
2. 双击运行，无需安装运行库。
3. 点击“添加文件”或“添加文件夹”，也可以把音乐拖到窗口中。
4. 选择输出目录和目标码率。
5. 点击“开始转换”，等待任务完成。

转换失败时，程序会保留其他成功结果，并在界面中列出失败文件和错误详情。

输出目录可以手动输入、点击“选择目录”，也可以直接把文件夹拖到输出目录输入框。

## 🧱 技术实现

- **界面**：C++20、Win32 API、Common Controls
- **音频处理**：裁剪版静态 FFmpeg API
- **NCM 解码**：AES-128-ECB 和 RC4 兼容实现
- **并发模型**：后台工作线程 + UI 消息回调
- **发布方式**：静态链接 FFmpeg、MinGW 运行库和 MP3 编码器，生成单 exe

## 🛠️ 从源码构建

### 环境要求

- Windows 10/11 x64
- MSYS2 UCRT64
- CMake 3.25 或更高版本
- Ninja
- 裁剪版 FFmpeg 开发库

### 构建单文件版本

在仓库根目录执行：

```powershell
cmake -S MusicToMp3Native -B MusicToMp3Native/build-single -G Ninja `
  -DMUSICMP3_STATIC_FFMPEG=ON `
  -DMUSICMP3_FFMPEG_ROOT=D:/msys64/usr/local
cmake --build MusicToMp3Native/build-single --config Release
```

构建结果位于 `MusicToMp3Native/build-single/`，复制并重命名为仓库根目录的 `MusicToMp3Native-Single.exe` 即可发布。

### 动态 FFmpeg 调试版本

如果只需要快速调试 UI 或业务逻辑，可以使用动态库配置：

```powershell
cmake -S MusicToMp3Native -B MusicToMp3Native/build-dynamic -G Ninja `
  -DMUSICMP3_STATIC_FFMPEG=OFF
cmake --build MusicToMp3Native/build-dynamic
```

该版本运行时需要本机 FFmpeg DLL，不适合作为最终分发包。

## 📂 项目结构

```text
MusicToMp3Native/
├─ src/main.cpp             # Win32 界面、文件队列、转换流程
├─ resources/app.rc         # Windows 应用图标资源
├─ resources/AppIcon.ico   # 应用图标
├─ CMakeLists.txt           # CMake 构建配置
└─ README.md                # 原生项目构建说明
MusicToMp3Native-Single.exe # 可直接分发的单文件版本
```

## 🔐 隐私与许可

本工具只访问用户主动选择的本地文件，不联网上传音乐内容，不收集个人信息。项目以 [MIT License](LICENSE) 开源，FFmpeg 及其相关组件遵循各自的开源许可。

## 📌 版本说明

- `v2.0.0`：原生单文件版首次发布。
- `v2.1.0`：修复转换异常闪退，保留音频元数据，增加输出目录拖拽、单实例运行和标题栏图标修复。

## 🗺️ 后续更新计划

以下功能是本项目后续重点方向，具体安排会根据实际测试和使用反馈调整：

### 🚧 计划中

- ⏹️ 支持暂停、继续和取消正在进行的转换任务
- 📋 增加更完整的任务队列管理，例如移除单个文件、调整顺序和重复文件提示
- 🏷️ 增加输出文件名模板，例如按“艺术家 - 标题”自动命名
- 🖼️ 改进封面和歌曲元数据处理，支持更多标签字段
- 📈 增加更详细的转换统计，例如耗时、平均速度和输出文件大小
- 📝 增加可选的日志文件，方便定位格式兼容问题

### 🔍 探索中

- 🎼 支持更多音频格式和更完善的编码参数
- 🎵 探索兼容 QQ 音乐、酷狗音乐等平台的专属音乐文件；这类文件可能使用私有封装或加密格式，具体扩展名、解密方式和兼容范围将根据实际测试逐步研究
- 🌐 增加多语言界面
- 🎨 继续优化高 DPI、深色模式和无障碍使用体验
- 📦 提供更方便的便携版或安装包发布方式
- 🔄 支持检查新版本并跳转到 GitHub Releases

### 💡 欢迎提出建议

如果你有希望加入的格式、界面功能或转换选项，欢迎提交 [Issue](https://github.com/crazyzhang277/MusicMP3/issues) 讨论。功能是否加入会综合考虑稳定性、体积、许可证和维护成本。

## 🤝 贡献

欢迎提交 Issue 或 Pull Request。提交代码前，请确保不会把个人音乐文件、构建缓存、FFmpeg 二进制依赖或其他敏感文件加入仓库。
