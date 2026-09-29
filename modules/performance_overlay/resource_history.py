"""Low-frequency Windows/GPU data; no extra dependencies or UI automation."""
import ctypes as c
from ctypes import wintypes as w
import os
import shutil
import subprocess
import time


class Memory(c.Structure):
    _fields_ = [('length', w.DWORD), ('load', w.DWORD)] + [(n, c.c_ulonglong) for n in
        ('total_phys', 'avail_phys', 'total_pagefile', 'avail_pagefile', 'total_virtual', 'avail_virtual', 'avail_extended')]


class ProcessEntry(c.Structure):
    _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('pid', w.DWORD), ('heap', c.c_size_t),
        ('module', w.DWORD), ('threads', w.DWORD), ('parent', w.DWORD), ('priority', w.LONG),
        ('flags', w.DWORD), ('exe', w.WCHAR * 260)]


class ProcessMemory(c.Structure):
    _fields_ = [('cb', w.DWORD), ('page_faults', w.DWORD)] + [(n, c.c_size_t) for n in
        ('peak_working_set', 'working_set', 'peak_paged_pool', 'paged_pool', 'peak_nonpaged_pool',
         'nonpaged_pool', 'pagefile', 'peak_pagefile', 'private_bytes')]


class Io(c.Structure):
    _fields_ = [(n, c.c_ulonglong) for n in ('read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]


def fields(value):
    return {n: getattr(value, n) for n, _ in value._fields_ if n not in ('cb', 'length')}


class Resources:
    def __init__(self):
        self.k = c.WinDLL('kernel32', use_last_error=True)
        self.p = c.WinDLL('psapi', use_last_error=True)
        self.k.CreateToolhelp32Snapshot.restype = w.HANDLE
        self.k.Process32FirstW.argtypes = [w.HANDLE, c.POINTER(ProcessEntry)]
        self.k.Process32NextW.argtypes = [w.HANDLE, c.POINTER(ProcessEntry)]
        self.k.OpenProcess.restype = w.HANDLE
        self.k.CloseHandle.argtypes = [w.HANDLE]
        self.k.GetProcessTimes.argtypes = [w.HANDLE] + [c.POINTER(c.c_ulonglong)] * 4
        self.k.GetProcessIoCounters.argtypes = [w.HANDLE, c.POINTER(Io)]
        self.k.GetProcessAffinityMask.argtypes = [w.HANDLE, c.POINTER(c.c_size_t), c.POINTER(c.c_size_t)]
        self.p.GetProcessMemoryInfo.argtypes = [w.HANDLE, c.POINTER(ProcessMemory), w.DWORD]
        self.previous = {}
        self.previous_system = None

    def sample(self, output):
        now = time.perf_counter_ns()
        memory = Memory(); memory.length = c.sizeof(memory)
        self.k.GlobalMemoryStatusEx(c.byref(memory))
        idle, kernel, user = c.c_ulonglong(), c.c_ulonglong(), c.c_ulonglong()
        self.k.GetSystemTimes(c.byref(idle), c.byref(kernel), c.byref(user))
        system = dict(idle=idle.value, kernel=kernel.value, user=user.value)
        result = dict(host_monotonic_ns=now, memory=fields(memory), cpu_ticks_100ns=system,
                      logical_cpus=os.cpu_count(), free_bytes=shutil.disk_usage(output).free, processes=[])
        if self.previous_system:
            before = self.previous_system
            total = kernel.value + user.value - before['kernel'] - before['user']
            if total > 0:
                result['host_cpu_percent'] = 100 * (1 - (idle.value - before['idle']) / total)
        self.previous_system = system
        snapshot = self.k.CreateToolhelp32Snapshot(2, 0)
        if snapshot == c.c_void_p(-1).value:
            return result
        try:
            entry = ProcessEntry(); entry.size = c.sizeof(entry)
            ok = self.k.Process32FirstW(snapshot, c.byref(entry))
            while ok:
                if entry.exe.lower() in ('qemu-system-x86_64-headless.exe', 'axrb-host-bridge.exe', 'vrcompositor.exe', 'vrserver.exe'):
                    handle = self.k.OpenProcess(0x410, False, entry.pid)
                    if handle:
                        try:
                            created, exited, kt, ut = [c.c_ulonglong() for _ in range(4)]
                            record = dict(pid=entry.pid, exe=entry.exe, threads=entry.threads)
                            if self.k.GetProcessTimes(handle, c.byref(created), c.byref(exited), c.byref(kt), c.byref(ut)):
                                record.update(created_100ns=created.value, kernel_100ns=kt.value, user_100ns=ut.value)
                                identity = (entry.pid, created.value)
                                ticks = kt.value + ut.value
                                before = self.previous.get(identity)
                                if before and now > before[0]:
                                    record['cpu_percent_one_core'] = (ticks - before[1]) * 10000 / (now - before[0])
                                self.previous[identity] = (now, ticks)
                            mem = ProcessMemory(); mem.cb = c.sizeof(mem)
                            if self.p.GetProcessMemoryInfo(handle, c.byref(mem), mem.cb): record['memory'] = fields(mem)
                            io = Io()
                            if self.k.GetProcessIoCounters(handle, c.byref(io)): record['io_cumulative'] = fields(io)
                            affinity, sysmask = c.c_size_t(), c.c_size_t()
                            if self.k.GetProcessAffinityMask(handle, c.byref(affinity), c.byref(sysmask)): record['affinity_mask'] = affinity.value
                            result['processes'].append(record)
                        finally:
                            self.k.CloseHandle(handle)
                ok = self.k.Process32NextW(snapshot, c.byref(entry))
        finally:
            self.k.CloseHandle(snapshot)
        return result


def gpu_sample():
    query = 'timestamp,name,pstate,utilization.gpu,utilization.memory,memory.used,memory.total,temperature.gpu,power.draw,clocks.current.graphics,clocks.current.memory'
    try:
        proc = subprocess.run(['nvidia-smi', '--query-gpu=' + query, '--format=csv,noheader,nounits'],
            capture_output=True, text=True, timeout=3, creationflags=subprocess.CREATE_NO_WINDOW, check=True)
        return dict(columns=query.split(','), rows=proc.stdout.strip().splitlines())
    except (OSError, subprocess.SubprocessError) as e:
        return dict(unavailable=type(e).__name__)
