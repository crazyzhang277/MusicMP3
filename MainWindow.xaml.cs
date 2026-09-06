using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Animation;
using Forms = System.Windows.Forms;
using WpfMessageBox = System.Windows.MessageBox;

namespace MusicToMp3;

public partial class MainWindow : Window
{
    private enum ConversionMode
    {
        Smart,
        HighQualityVbr,
        ConstantBitrate
    }

    private static readonly HashSet<string> SupportedExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".aac", ".ape", ".aiff", ".flac", ".m4a", ".mp4", ".ncm", ".ogg", ".opus", ".wav", ".wma"
    };

    private readonly List<string> _files = [];
    private bool _isConverting;

    public MainWindow()
    {
        InitializeComponent();
        OutputPathText.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyMusic), "MP3");
        UpdateModeUi(animate: false);
    }

    private void AddFiles_Click(object sender, RoutedEventArgs e)
    {
        using var dialog = new Forms.OpenFileDialog
        {
            Multiselect = true,
            Filter = "音频文件|*.aac;*.ape;*.aiff;*.flac;*.m4a;*.mp4;*.ncm;*.ogg;*.opus;*.wav;*.wma|所有文件|*.*"
        };
        if (dialog.ShowDialog() == Forms.DialogResult.OK)
            AddFiles(dialog.FileNames);
    }

    private void AddFolder_Click(object sender, RoutedEventArgs e)
    {
        using var dialog = new Forms.FolderBrowserDialog { Description = "选择音乐文件夹" };
        if (dialog.ShowDialog() == Forms.DialogResult.OK)
            AddFiles(Directory.EnumerateFiles(dialog.SelectedPath));
    }

    private void AddFiles(IEnumerable<string> paths)
    {
        var addedAny = false;
        foreach (var path in paths)
        {
            try
            {
                var candidates = Directory.Exists(path) ? Directory.EnumerateFiles(path) : [path];
                foreach (var file in candidates)
                {
                    if (SupportedExtensions.Contains(Path.GetExtension(file)) && !_files.Contains(file, StringComparer.OrdinalIgnoreCase))
                    {
                        _files.Add(file);
                        addedAny = true;
                    }
                }
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                StatusText.Text = $"无法读取：{Path.GetFileName(path)}";
            }
        }
        RefreshFileList();
        if (addedAny)
            Progress.Value = 0;
        StatusText.Text = _files.Count == 0 ? "没有找到可转换的音频文件" : "准备就绪";
    }

    private void RefreshFileList()
    {
        FilesList.ItemsSource = null;
        FilesList.ItemsSource = _files;
        FileCountText.Text = $"{_files.Count} 个文件";
        DropHint.Visibility = _files.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
    }

    private void FilesList_PreviewDragOver(object sender, System.Windows.DragEventArgs e)
    {
        SetDragEffect(e);
        e.Handled = true;
    }

    private void FilesList_PreviewDrop(object sender, System.Windows.DragEventArgs e)
    {
        HandleDrop(e);
        e.Handled = true;
    }

    private void Window_PreviewDragOver(object sender, System.Windows.DragEventArgs e)
    {
        SetDragEffect(e);
        e.Handled = true;
    }

    private void Window_PreviewDrop(object sender, System.Windows.DragEventArgs e)
    {
        HandleDrop(e);
        e.Handled = true;
    }

    private void SetDragEffect(System.Windows.DragEventArgs e)
    {
        try
        {
            e.Effects = TryGetDroppedPaths(e.Data, out _)
                ? System.Windows.DragDropEffects.Copy
                : System.Windows.DragDropEffects.None;
            if (e.Effects == System.Windows.DragDropEffects.Copy)
                StatusText.Text = "松开鼠标即可添加音乐";
        }
        catch
        {
            e.Effects = System.Windows.DragDropEffects.None;
        }
    }

    private void HandleDrop(System.Windows.DragEventArgs e)
    {
        try
        {
            if (TryGetDroppedPaths(e.Data, out var paths))
                AddFiles(paths);
            else
                StatusText.Text = "未识别到文件，请拖入本地音频文件";
        }
        catch (Exception ex)
        {
            StatusText.Text = "拖入文件失败";
            WpfMessageBox.Show("无法读取拖入的文件：\n" + ex.Message, "拖拽失败", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private static bool TryGetDroppedPaths(System.Windows.IDataObject data, out string[] paths)
    {
        paths = [];
        try
        {
            if (!data.GetDataPresent(System.Windows.DataFormats.FileDrop, true))
                return false;
            paths = data.GetData(System.Windows.DataFormats.FileDrop, true) as string[] ?? [];
            return paths.Length > 0;
        }
        catch
        {
            return false;
        }
    }

    private void Clear_Click(object sender, RoutedEventArgs e)
    {
        _files.Clear();
        RefreshFileList();
        Progress.Value = 0;
        StatusText.Text = "添加音乐文件后开始转换";
    }

    private void ChooseOutput_Click(object sender, RoutedEventArgs e)
    {
        using var dialog = new Forms.FolderBrowserDialog { SelectedPath = OutputPathText.Text };
        if (dialog.ShowDialog() == Forms.DialogResult.OK)
            OutputPathText.Text = dialog.SelectedPath;
    }

    private void OpenOutput_Click(object sender, RoutedEventArgs e)
    {
        var output = EnsureOutputDirectory();
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{output}\"") { UseShellExecute = true });
    }

    private async void Convert_Click(object sender, RoutedEventArgs e)
    {
        if (_isConverting) return;
        if (_files.Count == 0)
        {
            WpfMessageBox.Show("请先添加要转换的音频文件。", "还没有文件", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        _isConverting = true;
        Progress.Value = 0;
        try
        {
            var output = EnsureOutputDirectory();
            var bitrate = ((ComboBoxItem)BitrateBox.SelectedItem).Content.ToString()!;
            var mode = GetConversionMode();
            var ffmpeg = FindFfmpeg();
            var failures = new List<string>();
            for (var i = 0; i < _files.Count; i++)
            {
                var source = _files[i];
                StatusText.Text = $"正在转换 {i + 1}/{_files.Count}: {Path.GetFileName(source)}";
                try
                {
                    if (Path.GetExtension(source).Equals(".ncm", StringComparison.OrdinalIgnoreCase))
                        await ConvertNcmOneAsync(ffmpeg, source, output, bitrate, mode);
                    else
                        await ConvertOneAsync(RequireFfmpeg(ffmpeg), source, output, bitrate, mode);
                }
                catch (Exception ex)
                {
                    failures.Add($"{Path.GetFileName(source)}: {ex.Message}");
                }
                Progress.Value = (i + 1) * 100d / _files.Count;
            }

            var success = _files.Count - failures.Count;
            StatusText.Text = $"完成：成功 {success} 个，失败 {failures.Count} 个";
            if (failures.Count == 0)
                WpfMessageBox.Show("转换完成，文件已保存到：\n" + output, "转换完成", MessageBoxButton.OK, MessageBoxImage.Information);
            else
                WpfMessageBox.Show(string.Join(Environment.NewLine, failures), "部分文件转换失败", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        finally
        {
            _isConverting = false;
        }
    }

    private static async Task ConvertNcmOneAsync(string? ffmpeg, string source, string output, string bitrate, ConversionMode mode)
    {
        var decoded = await Task.Run(() => NcmDecoder.Decode(source));
        var name = SanitizeFileName(string.IsNullOrWhiteSpace(decoded.Title) ? Path.GetFileNameWithoutExtension(source) : decoded.Title);
        if (mode == ConversionMode.Smart && decoded.Format.Equals("mp3", StringComparison.OrdinalIgnoreCase))
        {
            await File.WriteAllBytesAsync(Path.Combine(output, name + ".mp3"), decoded.AudioData);
            return;
        }

        var temp = Path.Combine(Path.GetTempPath(), "music-to-mp3-" + Guid.NewGuid().ToString("N") + "." + decoded.Format);
        await File.WriteAllBytesAsync(temp, decoded.AudioData);
        try
        {
            await ConvertOneAsync(RequireFfmpeg(ffmpeg), temp, output, bitrate, mode, name + ".mp3", decoded);
        }
        finally
        {
            try { File.Delete(temp); } catch { }
        }
    }

    private static async Task ConvertOneAsync(string ffmpeg, string source, string output, string bitrate, ConversionMode mode, string? targetName = null, NcmDecodedTrack? metadata = null)
    {
        var target = Path.Combine(output, targetName ?? Path.GetFileNameWithoutExtension(source) + ".mp3");
        var info = new ProcessStartInfo
        {
            FileName = ffmpeg,
            UseShellExecute = false,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        var arguments = new List<string> { "-hide_banner", "-loglevel", "error", "-y", "-i", source, "-map_metadata", "0", "-vn", "-codec:a", "libmp3lame" };
        if (mode == ConversionMode.HighQualityVbr)
            arguments.AddRange(["-q:a", "2"]);
        else
            arguments.AddRange(["-b:a", bitrate]);
        if (metadata is not null)
        {
            if (!string.IsNullOrWhiteSpace(metadata.Title)) arguments.AddRange(["-metadata", "title=" + metadata.Title]);
            if (!string.IsNullOrWhiteSpace(metadata.Artist)) arguments.AddRange(["-metadata", "artist=" + metadata.Artist]);
            if (!string.IsNullOrWhiteSpace(metadata.Album)) arguments.AddRange(["-metadata", "album=" + metadata.Album]);
        }
        arguments.Add(target);
        foreach (var argument in arguments)
            info.ArgumentList.Add(argument);
        using var process = Process.Start(info) ?? throw new InvalidOperationException("无法启动 ffmpeg");
        var error = await process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        if (process.ExitCode != 0)
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(error) ? "ffmpeg 转换失败" : error.Trim());
    }

    private static string SanitizeFileName(string value)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var clean = new string(value.Select(ch => invalid.Contains(ch) ? '_' : ch).ToArray()).Trim();
        return string.IsNullOrWhiteSpace(clean) ? "converted" : clean;
    }

    private ConversionMode GetConversionMode()
        => VbrModeButton?.IsChecked == true
            ? ConversionMode.HighQualityVbr
            : CbrModeButton?.IsChecked == true
                ? ConversionMode.ConstantBitrate
                : ConversionMode.Smart;

    private void ConversionMode_Checked(object sender, RoutedEventArgs e)
        => UpdateModeUi();

    private void UpdateModeUi(bool animate = true)
    {
        if (FixedBitratePanel is null || ModeDescriptionText is null)
            return;

        var mode = GetConversionMode();
        ModeDescriptionText.Text = mode switch
        {
            ConversionMode.Smart => "智能保真：NCM 内部本来就是 MP3 时直接导出，不二次压缩，音质与下载文件一致。遇到 FLAC 等其它格式时，软件会按高音质默认设置转为 MP3。",
            ConversionMode.HighQualityVbr => "高音质省空间：使用 MP3 VBR Q2 动态分配码率，通常比 320k 小约四成，听感优秀；但会重新编码，不能保证与原文件逐字节一致。",
            _ => "固定码率：按所选 128k 至 320k 输出，大小易预测、兼容性稳定；码率越低，文件越小，音质损失也越明显。"
        };

        FixedBitratePanel.Visibility = mode == ConversionMode.ConstantBitrate ? Visibility.Visible : Visibility.Collapsed;
        if (!animate)
            return;

        ModeDescriptionText.BeginAnimation(OpacityProperty, new DoubleAnimation(0.25, 1, TimeSpan.FromMilliseconds(180))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
        });
        if (FixedBitratePanel.Visibility == Visibility.Visible)
        {
            FixedBitratePanel.BeginAnimation(OpacityProperty, new DoubleAnimation(0.2, 1, TimeSpan.FromMilliseconds(220))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            });
        }
    }

    private string EnsureOutputDirectory()
    {
        var path = string.IsNullOrWhiteSpace(OutputPathText.Text)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyMusic), "MP3")
            : OutputPathText.Text.Trim();
        Directory.CreateDirectory(path);
        return path;
    }

    private static string? FindFfmpeg()
    {
        var local = Path.Combine(AppContext.BaseDirectory, "ffmpeg.exe");
        if (File.Exists(local)) return local;
        var path = Environment.GetEnvironmentVariable("PATH")?.Split(Path.PathSeparator)
            .Select(p => Path.Combine(p, "ffmpeg.exe"))
            .FirstOrDefault(File.Exists);
        return path;
    }

    private static string RequireFfmpeg(string? ffmpeg)
        => ffmpeg ?? throw new InvalidOperationException("需要重新编码，但找不到 ffmpeg.exe。请将 ffmpeg.exe 放在软件旁边，或加入系统 PATH。");
}
