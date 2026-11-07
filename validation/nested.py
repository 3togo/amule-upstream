import ctypes
from ctypes import wintypes
import _winapi
import msvcrt
import os
from pathlib import Path
import subprocess
import tempfile
import wave

kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
kernel.CreateJobObjectW.restype = wintypes.HANDLE
kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
kernel.ResumeThread.argtypes = [wintypes.HANDLE]
kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
helper = Path('build-probe/src/Release/amule-media-probe.exe').resolve()
with tempfile.TemporaryDirectory() as temporary:
    media = Path(temporary) / 'input.wav'
    with wave.open(str(media), 'wb') as output:
        output.setparams((1, 2, 8000, 0, 'NONE', 'not compressed'))
        output.writeframes(bytes(16000))
    with media.open('rb') as source, tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
        handles = [msvcrt.get_osfhandle(f.fileno()) for f in (source, output, errors)]
        for handle in handles:
            os.set_handle_inheritable(handle, True)
        startup = subprocess.STARTUPINFO()
        startup.dwFlags = subprocess.STARTF_USESTDHANDLES
        startup.hStdInput, startup.hStdOutput, startup.hStdError = handles
        startup.lpAttributeList = {'handle_list': handles}
        job = kernel.CreateJobObjectW(None, None)
        assert job, ctypes.get_last_error()
        process, thread, pid, tid = _winapi.CreateProcess(str(helper), f'"{helper}"', None, None,
            True, 0x80000 | 4 | 8, None, None, startup)
        assert kernel.AssignProcessToJobObject(job, process), ctypes.get_last_error()
        assert kernel.ResumeThread(thread) != 0xffffffff, ctypes.get_last_error()
        assert kernel.WaitForSingleObject(process, 40000) == 0, ctypes.get_last_error()
        code = _winapi.GetExitCodeProcess(process)
        output.seek(0)
        errors.seek(0)
        print('Nested Job exit:', code, 'stdout:', output.read(1024))
        print('Nested Job diagnostics:', errors.read().decode(errors='replace')[-4000:])
        kernel.CloseHandle(thread)
        kernel.CloseHandle(process)
        kernel.CloseHandle(job)
        assert code == 0
