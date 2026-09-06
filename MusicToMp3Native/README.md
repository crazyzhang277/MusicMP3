# 音乐转 MP3 Native

这是独立的 C++ 原生 Windows 工程。

当前原型已经包含：

- Win32 原生界面
- 添加文件、添加文件夹、拖拽导入、清空列表
- 批量转换和进度显示
- NCM AES/RC4 解码
- FFmpeg API 进程内解码和 MP3 编码
- 码率选择和输出目录选择
- 应用图标和自适应窗口布局

## 构建

需要 MSYS2 UCRT64、CMake、Ninja 和 FFmpeg 开发库：

```powershell
cmake -S . -B build-dynamic -G Ninja -DMUSICMP3_STATIC_FFMPEG=OFF
cmake --build build-dynamic
```

当前 `OFF` 配置用于验证 UI、NCM 和 FFmpeg API 逻辑，运行时需要 MSYS2 的 FFmpeg DLL。

最终发布版使用裁剪后的静态 FFmpeg 构建：

```powershell
cmake -S . -B build-single -G Ninja `
  -DMUSICMP3_STATIC_FFMPEG=ON `
  -DMUSICMP3_FFMPEG_ROOT=D:/msys64/usr/local
cmake --build build-single --config Release
```

发布产物为仓库根目录的 `MusicToMp3Native-Single.exe`。它约 7 MB，依赖扫描只包含 Windows 系统 DLL，不需要随 exe 携带 FFmpeg、MinGW 或其他第三方 DLL。
