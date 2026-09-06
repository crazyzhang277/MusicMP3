from __future__ import annotations

import os
import shutil
import subprocess
import threading
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk


SUPPORTED = {".aac", ".flac", ".m4a", ".mp4", ".ogg", ".opus", ".wav", ".wma", ".aiff", ".ape"}


def ffmpeg_command(source: Path, target: Path, bitrate: str) -> list[str]:
    return [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-i",
        str(source),
        "-map_metadata",
        "0",
        "-vn",
        "-codec:a",
        "libmp3lame",
        "-b:a",
        bitrate,
        str(target),
    ]


def convert_one(source: Path, output_dir: Path, bitrate: str) -> Path:
    target = output_dir / f"{source.stem}.mp3"
    subprocess.run(ffmpeg_command(source, target, bitrate), check=True, capture_output=True, text=True)
    return target


class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("音乐转 MP3")
        self.geometry("760x560")
        self.minsize(680, 500)
        self.configure(bg="#f5f7fb")
        self.files: list[Path] = []
        self.output_dir = tk.StringVar(value=str(Path.home() / "Music" / "MP3"))
        self.bitrate = tk.StringVar(value="320k")
        self.status = tk.StringVar(value="添加音乐文件后开始转换")
        self.progress = tk.DoubleVar(value=0)
        self._build()

    def _build(self) -> None:
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("App.TFrame", background="#f5f7fb")
        style.configure("Title.TLabel", background="#f5f7fb", foreground="#172033", font=("Segoe UI", 22, "bold"))
        style.configure("Muted.TLabel", background="#f5f7fb", foreground="#667085", font=("Segoe UI", 10))
        style.configure("Section.TLabel", background="#f5f7fb", foreground="#344054", font=("Segoe UI", 10, "bold"))
        style.configure("Primary.TButton", font=("Segoe UI", 10, "bold"), padding=(16, 9))
        style.configure("TButton", padding=(10, 7))
        style.configure("TProgressbar", thickness=10, troughcolor="#e6eaf2", background="#3366ff")

        root = ttk.Frame(self, padding=28, style="App.TFrame")
        root.pack(fill="both", expand=True)
        ttk.Label(root, text="音乐转 MP3", style="Title.TLabel").pack(anchor="w")
        ttk.Label(root, text="批量转换本地音频，保留可读取的歌曲元数据。", style="Muted.TLabel").pack(anchor="w", pady=(4, 22))

        files_box = ttk.LabelFrame(root, text=" 1 · 选择音乐 ", padding=14)
        files_box.pack(fill="both", expand=True)
        toolbar = ttk.Frame(files_box)
        toolbar.pack(fill="x", pady=(0, 10))
        ttk.Button(toolbar, text="添加文件", command=self.add_files).pack(side="left")
        ttk.Button(toolbar, text="添加文件夹", command=self.add_folder).pack(side="left", padx=8)
        ttk.Button(toolbar, text="清空", command=self.clear_files).pack(side="left")
        self.count_label = ttk.Label(toolbar, text="0 个文件", style="Muted.TLabel")
        self.count_label.pack(side="right")

        list_frame = ttk.Frame(files_box)
        list_frame.pack(fill="both", expand=True)
        self.listbox = tk.Listbox(list_frame, selectmode=tk.EXTENDED, activestyle="none", relief="flat", borderwidth=0,
                                  bg="white", fg="#344054", selectbackground="#dbe5ff", selectforeground="#172033",
                                  font=("Segoe UI", 10), highlightthickness=1, highlightcolor="#c6d4ff")
        scrollbar = ttk.Scrollbar(list_frame, orient="vertical", command=self.listbox.yview)
        self.listbox.configure(yscrollcommand=scrollbar.set)
        self.listbox.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        options = ttk.LabelFrame(root, text=" 2 · 输出设置 ", padding=14)
        options.pack(fill="x", pady=16)
        ttk.Label(options, text="输出目录", style="Section.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Entry(options, textvariable=self.output_dir).grid(row=1, column=0, sticky="ew", pady=(5, 0))
        ttk.Button(options, text="选择", command=self.choose_output).grid(row=1, column=1, padx=(8, 0), pady=(5, 0))
        ttk.Label(options, text="MP3 码率", style="Section.TLabel").grid(row=0, column=2, sticky="w", padx=(22, 0))
        ttk.Combobox(options, textvariable=self.bitrate, values=("128k", "192k", "256k", "320k"), state="readonly", width=9).grid(row=1, column=2, sticky="w", padx=(22, 0), pady=(5, 0))
        options.columnconfigure(0, weight=1)

        ttk.Label(root, textvariable=self.status, style="Muted.TLabel").pack(anchor="w")
        ttk.Progressbar(root, variable=self.progress, maximum=100).pack(fill="x", pady=(8, 14))
        actions = ttk.Frame(root, style="App.TFrame")
        actions.pack(fill="x")
        ttk.Button(actions, text="打开输出目录", command=self.open_output).pack(side="left")
        ttk.Button(actions, text="开始转换", command=self.start_conversion, style="Primary.TButton").pack(side="right")

    def add_files(self) -> None:
        selected = filedialog.askopenfilenames(title="选择音频文件", filetypes=[("音频文件", "*.aac *.ape *.aiff *.flac *.m4a *.mp4 *.ogg *.opus *.wav *.wma"), ("所有文件", "*.*")])
        self._append(Path(item) for item in selected)

    def add_folder(self) -> None:
        folder = filedialog.askdirectory(title="选择音乐文件夹")
        if folder:
            self._append(path for path in Path(folder).iterdir() if path.is_file())

    def _append(self, paths) -> None:
        existing = set(self.files)
        for path in paths:
            if path.suffix.lower() == ".ncm":
                continue
            if path.suffix.lower() in SUPPORTED and path not in existing:
                self.files.append(path)
                existing.add(path)
                self.listbox.insert(tk.END, str(path))
        self.count_label.configure(text=f"{len(self.files)} 个文件")
        if self.files:
            self.status.set("准备就绪")

    def clear_files(self) -> None:
        self.files.clear()
        self.listbox.delete(0, tk.END)
        self.count_label.configure(text="0 个文件")
        self.progress.set(0)
        self.status.set("添加音乐文件后开始转换")

    def choose_output(self) -> None:
        folder = filedialog.askdirectory(title="选择输出目录")
        if folder:
            self.output_dir.set(folder)

    def open_output(self) -> None:
        folder = Path(self.output_dir.get()).expanduser()
        folder.mkdir(parents=True, exist_ok=True)
        os.startfile(folder)

    def start_conversion(self) -> None:
        if not self.files:
            messagebox.showinfo("还没有文件", "请先添加要转换的音频文件。")
            return
        if shutil.which("ffmpeg") is None:
            messagebox.showerror("找不到 ffmpeg", "请先安装 ffmpeg，并把它加入系统 PATH。")
            return
        output = Path(self.output_dir.get()).expanduser()
        output.mkdir(parents=True, exist_ok=True)
        threading.Thread(target=self._convert_all, args=(output,), daemon=True).start()

    def _convert_all(self, output: Path) -> None:
        total = len(self.files)
        success = 0
        failures: list[str] = []
        for index, source in enumerate(self.files, 1):
            self.after(0, self.status.set, f"正在转换 {index}/{total}: {source.name}")
            try:
                convert_one(source, output, self.bitrate.get())
                success += 1
            except (OSError, subprocess.CalledProcessError) as exc:
                detail = getattr(exc, "stderr", "") or str(exc)
                failures.append(f"{source.name}: {detail.strip()[-160:]}")
            self.after(0, self.progress.set, index / total * 100)
        result = f"完成：成功 {success} 个"
        if failures:
            result += f"，失败 {len(failures)} 个"
        self.after(0, self.status.set, result)
        if failures:
            self.after(0, messagebox.showwarning, "转换完成", result + "\n\n" + "\n".join(failures))
        else:
            self.after(0, messagebox.showinfo, "转换完成", result + f"\n文件已保存到：\n{output}")


if __name__ == "__main__":
    App().mainloop()
