const POLL_MS = 5000;

function describe(status) {
  if (!status.running) return { phase: 'stopped', detail: 'The Android emulator is not running.' };
  if (status.adbState === 'device') {
    return { phase: 'online', detail: `The Android emulator is running and reachable (pid ${status.pid ?? '?'}).` };
  }
  const reachability = status.adbState ? `Android is ${status.adbState}` : 'Android is not yet reachable';
  const multiple = status.count > 1 ? ` ${status.count} matching processes were found; AXRB manages only one at a time.` : '';
  return { phase: 'starting', detail: `The Android emulator process is running (pid ${status.pid ?? '?'}); ${reachability}.${multiple}` };
}

// Polls the Android emulator's OS process and ADB reachability independently
// of any game session, so a boot that never completes or a process that
// outlives its shutdown request is visible even when nothing else is running.
export class EmulatorWatchdog {
  constructor({ getStatus, onChange = () => {}, onTransition = () => {}, pollMs = POLL_MS }) {
    Object.assign(this, { getStatus, onChange, onTransition, pollMs });
    this.state = { phase: 'unknown', pid: null, count: 0, adbState: '', detail: 'Checking the Android emulator.', since: null, error: null };
    this.active = false; this.generation = 0;
  }
  snapshot() { return { ...this.state }; }
  start() {
    if (this.active) return;
    this.active = true;
    this.loop();
  }
  async stop() {
    if (!this.active) return;
    this.active = false; this.generation++;
    clearTimeout(this.timer);
    await this.task;
  }
  apply(next) {
    const prev = this.state;
    const changed = prev.phase !== next.phase || prev.adbState !== next.adbState || prev.error !== next.error;
    this.state = changed ? { ...next, since: new Date().toISOString() } : { ...prev, ...next, since: prev.since };
    if (changed) { this.onTransition(this.state, prev); this.onChange(); }
  }
  async tick() {
    const generation = this.generation;
    try {
      const status = await this.getStatus();
      if (!this.active || generation !== this.generation) return;
      this.apply({ ...describe(status), pid: status.pid, count: status.count, adbState: status.adbState, error: null });
    } catch (error) {
      if (!this.active || generation !== this.generation) return;
      this.apply({ phase: 'unknown', pid: null, count: 0, adbState: '', detail: error.message, error: error.message });
    }
  }
  loop() {
    this.task = this.tick().finally(() => {
      if (!this.active) return;
      this.timer = setTimeout(() => this.loop(), this.pollMs);
      this.timer.unref?.();
    });
  }
}
