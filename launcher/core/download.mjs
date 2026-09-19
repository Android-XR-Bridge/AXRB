import fs from 'node:fs/promises';
import { createReadStream, createWriteStream } from 'node:fs';
import { pipeline } from 'node:stream/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';

export function safeName(value) {
  if (typeof value !== 'string' || !value || value.length > 220 || /[<>:"/\\|?*\x00-\x1f]/.test(value) || /[. ]$/.test(value) || /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(value))
    throw new Error('Meta returned an unsafe asset filename.');
  return value;
}
export function allowedDownload(value) {
  const url = new URL(value);
  const trusted = ['oculus.com', 'oculuscdn.com', 'facebook.com', 'fbcdn.net', 'meta.com', 'fbsbx.com'];
  if (url.protocol !== 'https:' || url.username || url.password || (url.port && url.port !== '443') ||
    !trusted.some(d => url.hostname === d || url.hostname.endsWith(`.${d}`))) throw new Error('Download URL is not on a Meta delivery domain.');
  return url;
}
async function fetchFile(url, options, request, validate) {
  for (let redirects = 0; redirects < 8; redirects++) {
    validate(url);
    const response = await request(url, { ...options, redirect: 'manual' });
    if ([301, 302, 303, 307, 308].includes(response.status)) {
      const location = response.headers.get('location');
      await response.body?.cancel();
      if (!location) throw new Error('Download redirect has no location.');
      url = new URL(location, url).href;
      continue;
    }
    return response;
  }
  throw new Error('Too many download redirects.');
}

// Only rename completed files. Range resume is conditional on a stable ETag.
export async function downloadFile({ url, destination, size = 0, sha256: expectedSha256 = '', sha1: expectedSha1 = '', signal, progress = () => {}, request = fetch, validate = allowedDownload }) {
  validate(url);
  if (!Number.isSafeInteger(size) || size < 0) throw new Error('Invalid download size.');
  await fs.mkdir(path.dirname(destination), { recursive: true });
  const part = `${destination}.part`, sidecar = `${part}.json`;
  let offset = 0, etag = '';
  try {
    const meta = JSON.parse(await fs.readFile(sidecar, 'utf8'));
    const stat = await fs.stat(part);
    if (meta.size === size && meta.etag && !meta.etag.startsWith('W/') && stat.size <= (size || Infinity)) { offset = stat.size; etag = meta.etag; }
  } catch {}
  const headers = offset ? { Range: `bytes=${offset}-`, 'If-Range': etag } : {};
  const hash = createHash('sha256');
  const sha1Hash = expectedSha1 && !expectedSha256 ? createHash('sha1') : null;
  const idle = new AbortController();
  const downloadSignal = signal ? AbortSignal.any([signal, idle.signal]) : idle.signal;
  // Limit inactivity, not total transfer time: slow downloads may take hours.
  const timer = setTimeout(() => idle.abort(new DOMException('Download stalled for 30 minutes.', 'TimeoutError')), 30 * 60 * 1000);
  timer.unref();
  let received;
  try {
    const response = await fetchFile(url, { headers, signal: downloadSignal }, request, validate);
    timer.refresh();
    if (![200, 206].includes(response.status)) { await response.body?.cancel(); throw new Error(`Meta's file server refused the download (HTTP ${response.status}). Library ownership and file-delivery access are separate; try reconnecting Meta.`); }
    if (/text\/html|application\/json/i.test(response.headers.get('content-type') || '')) { await response.body?.cancel(); throw new Error('Meta returned an error page instead of the requested file.'); }
    if (response.status === 206) {
      const range = response.headers.get('content-range')?.match(/^bytes (\d+)-(\d+)\/(\d+)$/);
      if (!range || Number(range[1]) !== offset || (size && Number(range[3]) !== size)) { await response.body?.cancel(); throw new Error('Server returned an inconsistent download range.'); }
    } else offset = 0;
    const length = Number(response.headers.get('content-length') || 0);
    // CDN content-length is not reliable here: Meta may serve a compressed or
    // transformed representation, and proxies can omit/update it. The streamed
    // byte count below is authoritative and is checked against the requested
    // metadata before the completed file is renamed.
    const total = size || (length ? length + offset : 0);
    await fs.writeFile(sidecar, JSON.stringify({ size, etag: response.headers.get('etag') || '' }));
    received = offset;
    if (!response.body) throw new Error('Download response was empty.');
    // A resumed digest includes the saved prefix; HTTP 200 resets offset above.
    if (offset) {
      for await (const chunk of createReadStream(part, { signal: downloadSignal })) {
        hash.update(chunk); sha1Hash?.update(chunk);
      }
    }
    // Batch small chunks with bounded backpressure; flush syncs before close.
    await pipeline(async function* () {
      try {
        for await (const chunk of response.body) {
          downloadSignal.throwIfAborted();
          if (total && received + chunk.length > total) throw new Error('Download exceeded its expected size.');
          if (chunk.length) timer.refresh();
          hash.update(chunk); sha1Hash?.update(chunk);
          received += chunk.length;
          yield chunk;
          progress(received, total);
        }
      } finally { clearTimeout(timer); }
    }, createWriteStream(part, { flags: offset ? 'a' : 'w', highWaterMark: 1024 * 1024, flush: true }), { signal: downloadSignal });
    downloadSignal.throwIfAborted();
    if (!received || (total && received !== total)) throw new Error('Download ended before the complete file arrived.');
  } catch (error) {
    const reason = downloadSignal.aborted ? downloadSignal.reason : error;
    idle.abort();
    throw reason;
  } finally { clearTimeout(timer); }
  // Verify supplied checksums before publishing; keep the SHA-256 for installation.
  const sha256 = hash.digest('hex');
  if ((expectedSha256 && sha256 !== expectedSha256) || (sha1Hash && sha1Hash.digest('hex') !== expectedSha1)) {
    await fs.rm(sidecar, { force: true });
    await fs.rm(part, { force: true });
    throw new Error('Download checksum mismatch. Retry the download.');
  }
  await fs.rename(part, destination);
  await fs.rm(sidecar, { force: true });
  return { bytes: received, sha256 };
}

export async function checkSpace(directory, bytes, { reserve = 5 * 1024 ** 3 } = {}) {
  await fs.mkdir(directory, { recursive: true });
  const space = await fs.statfs(directory);
  const available = Number(space.bavail) * Number(space.bsize);
  if (available < bytes + reserve) throw new Error(`Not enough disk space. This download needs ${(bytes / 1024 ** 3).toFixed(1)} GB plus 5 GB free for Android. Choose another download folder in Settings.`);
}
