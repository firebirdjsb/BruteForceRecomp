"""Native desktop ISO chooser; imported only when the user presses Browse."""
import sys


def windows_iso():
    import ctypes
    from ctypes import wintypes as w

    class OpenFileName(ctypes.Structure):
        # OPENFILENAMEW uses native pointer alignment on x64. WORD offsets
        # are 16-bit; lengths and flags are 32-bit even on a 64-bit host.
        _fields_ = [('size', w.DWORD), ('owner', w.HWND), ('instance', w.HANDLE),
                    ('filter', w.LPCWSTR), ('custom_filter', w.LPWSTR), ('custom_length', w.DWORD),
                    ('filter_index', w.DWORD), ('file', w.LPWSTR), ('file_length', w.DWORD),
                    ('file_title', w.LPWSTR), ('title_length', w.DWORD), ('initial_dir', w.LPCWSTR),
                    ('title', w.LPCWSTR), ('flags', w.DWORD), ('file_offset', w.WORD),
                    ('extension_offset', w.WORD), ('default_extension', w.LPCWSTR), ('data', w.LPARAM),
                    ('hook', w.LPVOID), ('template', w.LPCWSTR), ('reserved', w.LPVOID),
                    ('reserved_flags', w.DWORD), ('extended_flags', w.DWORD)]
    dialog = ctypes.WinDLL('comdlg32', use_last_error=True)
    dialog.GetOpenFileNameW.argtypes = [ctypes.POINTER(OpenFileName)]
    dialog.GetOpenFileNameW.restype = w.BOOL
    dialog.CommDlgExtendedError.restype = w.DWORD
    buffer = ctypes.create_unicode_buffer(32768)
    options = OpenFileName()
    options.size = ctypes.sizeof(options)
    options.filter = 'Xbox disc images\0*.iso;*.xiso\0All files\0*.*\0\0'
    options.filter_index = 1
    options.file = ctypes.cast(buffer, w.LPWSTR)
    options.file_length = len(buffer)
    options.title = 'Select your Brute Force disc image'
    # Explorer dialog; existing file/path only; retain the server's CWD.
    options.flags = 0x00080000 | 0x00001000 | 0x00000800 | 0x00000008
    if dialog.GetOpenFileNameW(ctypes.byref(options)):
        return buffer.value
    error = dialog.CommDlgExtendedError()
    if error:
        raise ValueError(f'Windows file chooser failed (0x{error:08X}). Paste the full ISO path instead.')
    return ''  # Cancel is a normal outcome.


def pick_iso():
    if sys.platform == 'win32':
        return windows_iso()
    try:
        import tkinter as tk
        from tkinter import filedialog
        root = tk.Tk()
        root.withdraw()
        try:
            return filedialog.askopenfilename(parent=root, title='Select your Brute Force disc image',
                                              filetypes=[('Xbox disc image', '*.iso *.xiso'), ('All files', '*.*')])
        finally:
            root.destroy()
    except Exception as error:
        raise ValueError('Native file picker unavailable. Install python3-tk or paste the full ISO path.') from error
