using System.Windows;
using System.Windows.Threading;

namespace MusicToMp3;

public partial class App : System.Windows.Application
{
    public App()
    {
        DispatcherUnhandledException += OnUnhandledException;
    }

    private static void OnUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        var msg = e.Exception.Message;
        if (e.Exception.InnerException != null)
        {
            msg += "\n\n详细原因：\n" + e.Exception.InnerException.Message;
        }
        System.Windows.MessageBox.Show("程序遇到错误：\n" + msg, "音乐转 MP3", MessageBoxButton.OK, MessageBoxImage.Error);
        e.Handled = true;
    }
}
