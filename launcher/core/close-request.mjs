// Share one request between the window's close button and app.quit().
export function createCloseRequest({ getStatus, prompt, stop, quit, onError }) {
  let pending;
  return () => {
    if (pending) return pending;
    pending = (async () => {
      const status = await getStatus();
      if (status.running) {
        const choice = await prompt();
        if (choice === 'cancel') return;
        if (choice === 'stop') await stop();
        else if (choice !== 'leave') return;
      }
      await quit();
    })().catch(onError).finally(() => { pending = null; });
    return pending;
  };
}
