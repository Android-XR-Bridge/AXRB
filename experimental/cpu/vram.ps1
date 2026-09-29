# Resident local VRAM of the emulator process, from the Windows GPU counters.
$qemu = Get-Process qemu-system-x86_64-headless -ErrorAction Stop | Select-Object -First 1
$local = (Get-Counter "\GPU Process Memory(pid_$($qemu.Id)*)\Local Usage").CounterSamples | Measure-Object CookedValue -Sum
$committed = (Get-Counter "\GPU Process Memory(pid_$($qemu.Id)*)\Total Committed").CounterSamples | Measure-Object CookedValue -Sum
"local {0:F0} MiB, committed {1:F0} MiB" -f ($local.Sum / 1MB), ($committed.Sum / 1MB)
