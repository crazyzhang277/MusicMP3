# 音乐转 MP3

一个简洁、快速、完全本地运行的 Windows 音频转换工具。

支持将常见音频格式批量转换为 MP3，也支持网易云音乐 `.ncm` 文件解码。文件不会上传到网络，转换过程和输出目录都由用户控制。

## 功能

- 支持拖拽添加文件、选择文件夹或批量选择音频
- 支持 `.aac`、`.ape`、`.aiff`、`.flac`、`.m4a`、`.mp4`、`.ncm`、`.ogg`、`.opus`、`.wav`、`.wma`
- 支持智能模式、高质量 VBR 和固定码率
- 尽量保留标题、艺术家和专辑等元数据
- 转换过程中显示当前文件和整体进度
- 添加新文件或开始新一轮转换时，进度条自动归零
- 转换失败的文件会单独列出，不影响其他文件继续处理

## 下载使用

发布版位于 [`轻量版/音乐转MP3.exe`](轻量版/音乐转MP3.exe)。

这是依赖 .NET 8 Desktop Runtime 的轻量单文件版本。首次运行前请准备：

1. Windows 10/11
2. [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0)
3. `ffmpeg.exe`：放在 exe 同目录，或加入系统 `PATH`

双击 exe 后，拖入音乐文件，选择输出目录和码率，再点击“开始转换”。

## 从源码构建

需要安装 .NET 8 SDK，然后在项目目录执行：

```powershell
dotnet build
dotnet run
```

生成轻量单文件发布版：

```powershell
dotnet publish MusicToMp3.csproj -c Release -r win-x64 `
  --self-contained false `
  -p:PublishSingleFile=true `
  -p:IncludeNativeLibrariesForSelfExtract=true
```

发布文件会生成到 `bin/Release/net8.0-windows/win-x64/publish/`。

## 项目结构

| 路径 | 说明 |
| --- | --- |
| `MainWindow.xaml` | WPF 界面布局 |
| `MainWindow.xaml.cs` | 文件管理、进度显示和转换流程 |
| `NcmDecoder.cs` | `.ncm` 解码逻辑 |
| `MusicToMp3.csproj` | .NET 8 项目配置 |
| `轻量版/音乐转MP3.exe` | 最新轻量发布版 |

## 许可与隐私

本工具只处理本地文件，不收集、不上传用户音乐或转换记录。`ffmpeg` 请根据其自身许可和发行方式使用。
