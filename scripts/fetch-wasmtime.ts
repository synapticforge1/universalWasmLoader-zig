// Fetch the wasmtime C API SDK into vendor/ (gitignored). The SDK provides the
// headers (wasm.h / wasmtime.h / wasi.h) and the static/import libraries the
// loader links against.
//
// Usage: bun scripts/fetch-wasmtime.ts [VERSION] [PLATFORM]
//   VERSION  default: 45.0.2
//   PLATFORM default: x86_64-mingw  (gcc/MinGW on Windows)
//            others:  x86_64-windows (MSVC), x86_64-linux, aarch64-linux,
//                     x86_64-macos, aarch64-macos
//
// Needs nothing but Bun: the download uses fetch() and the .zip is extracted
// in-process (stored + deflate entries), so no curl/unzip/tar is required.
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, join, resolve, sep } from "node:path";
import { inflateRawSync } from "node:zlib";
import { die, ROOT } from "./_lib.ts";

const VERSION = process.argv[2] ?? "45.0.2";
const PLATFORM = process.argv[3] ?? "x86_64-mingw";
const VENDOR = join(ROOT, "vendor");
const NAME = `wasmtime-v${VERSION}-${PLATFORM}-c-api`;
const URL = `https://github.com/bytecodealliance/wasmtime/releases/download/v${VERSION}/${NAME}.zip`;

mkdirSync(VENDOR, { recursive: true });
if (existsSync(join(VENDOR, NAME, "include"))) {
  console.log(`wasmtime C API already present: ${join(VENDOR, NAME)}`);
  process.exit(0);
}

console.log(`Downloading ${URL}`);
const zip = await download(URL, 3);
console.log("Extracting");
extractZip(zip, VENDOR);
console.log(`Done: ${join(VENDOR, NAME)}`);

async function download(url: string, retries: number): Promise<Buffer> {
  for (let attempt = 0; ; attempt++) {
    try {
      const res = await fetch(url);
      if (!res.ok) die(`download failed: HTTP ${res.status} ${res.statusText}`);
      return Buffer.from(await res.arrayBuffer());
    } catch (e) {
      if (attempt >= retries) die(`download failed: ${e}`);
      console.error(`retrying (${attempt + 1}/${retries})…`);
    }
  }
}

/** Minimal .zip extractor: reads the central directory; stored and deflate only; no ZIP64. */
function extractZip(buf: Buffer, destDir: string): void {
  const root = resolve(destDir);
  let eocd = -1;
  for (let i = buf.length - 22; i >= Math.max(0, buf.length - 22 - 0xffff); i--) {
    if (buf.readUInt32LE(i) === 0x06054b50) {
      eocd = i;
      break;
    }
  }
  if (eocd < 0) die("downloaded file is not a zip archive");
  const count = buf.readUInt16LE(eocd + 10);
  let p = buf.readUInt32LE(eocd + 16);
  if (count === 0xffff || p === 0xffffffff) die("ZIP64 archives are not supported");

  for (let n = 0; n < count; n++) {
    if (buf.readUInt32LE(p) !== 0x02014b50) die("corrupt zip central directory");
    const method = buf.readUInt16LE(p + 10);
    const csize = buf.readUInt32LE(p + 20);
    const usize = buf.readUInt32LE(p + 24);
    const nameLen = buf.readUInt16LE(p + 28);
    const extraLen = buf.readUInt16LE(p + 30);
    const commentLen = buf.readUInt16LE(p + 32);
    const local = buf.readUInt32LE(p + 42);
    const name = buf.toString("utf8", p + 46, p + 46 + nameLen);
    p += 46 + nameLen + extraLen + commentLen;

    const out = resolve(root, name);
    if (!out.startsWith(root + sep)) die(`unsafe path in archive: ${name}`);
    if (name.endsWith("/")) {
      mkdirSync(out, { recursive: true });
      continue;
    }
    const start = local + 30 + buf.readUInt16LE(local + 26) + buf.readUInt16LE(local + 28);
    const raw = buf.subarray(start, start + csize);
    let data: Buffer;
    if (method === 0) data = raw;
    else if (method === 8) data = inflateRawSync(raw);
    else die(`unsupported zip compression method ${method} for ${name}`);
    if (data.length !== usize) die(`size mismatch extracting ${name}`);
    mkdirSync(dirname(out), { recursive: true });
    writeFileSync(out, data);
  }
}
