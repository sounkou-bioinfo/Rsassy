#!/usr/bin/env node

const fs = require("fs");
const http = require("http");
const os = require("os");
const path = require("path");
const zlib = require("zlib");
const { execFileSync } = require("child_process");
const { WebR, ChannelType } = require("webr");

const tgzPath = path.resolve(process.argv[2] || process.env.RSASSY_WEBR_TGZ || "");
if (!tgzPath || !fs.existsSync(tgzPath)) {
  console.error("Usage: node tools/webr-check.cjs path/to/Rsassy_<version>.tgz");
  process.exit(2);
}

function parseDcf(text) {
  const fields = {};
  let current = null;
  for (const line of text.split(/\r?\n/)) {
    if (!line) continue;
    if (/^\s/.test(line) && current) {
      fields[current] += `\n${line}`;
      continue;
    }
    const idx = line.indexOf(":");
    if (idx < 0) continue;
    current = line.slice(0, idx);
    fields[current] = line.slice(idx + 1).replace(/^\s*/, "");
  }
  return fields;
}

function writePackagesIndex(repoDir, fields, fileName) {
  fs.mkdirSync(repoDir, { recursive: true });
  fs.copyFileSync(tgzPath, path.join(repoDir, fileName));
  const keys = Object.keys(fields).filter((key) => fields[key] !== undefined && fields[key] !== "");
  if (!keys.includes("File")) keys.push("File");
  fields.File = fileName;
  const packages = keys.map((key) => `${key}: ${fields[key]}`).join("\n") + "\n";
  fs.writeFileSync(path.join(repoDir, "PACKAGES"), packages);
  fs.writeFileSync(path.join(repoDir, "PACKAGES.gz"), zlib.gzipSync(packages));
}

function createLocalRepo(tmpRoot, rSeries) {
  const extractDir = path.join(tmpRoot, "extract");
  fs.mkdirSync(extractDir, { recursive: true });
  execFileSync("tar", ["-xzf", tgzPath, "-C", extractDir], { stdio: "inherit" });
  const descPath = path.join(extractDir, "Rsassy", "DESCRIPTION");
  const fields = parseDcf(fs.readFileSync(descPath, "utf8"));
  if (fields.Package !== "Rsassy" || !fields.Version) {
    throw new Error(`Unexpected DESCRIPTION in ${tgzPath}`);
  }
  const fileName = path.basename(tgzPath);
  writePackagesIndex(path.join(tmpRoot, "repo", "src", "contrib"), { ...fields }, fileName);
  writePackagesIndex(path.join(tmpRoot, "repo", "bin", "emscripten", "contrib", rSeries), { ...fields }, fileName);
  return path.join(tmpRoot, "repo");
}

function contentType(filePath) {
  if (filePath.endsWith(".gz") || filePath.endsWith(".tgz")) return "application/gzip";
  if (filePath.endsWith(".rds")) return "application/octet-stream";
  return "text/plain; charset=utf-8";
}

function serveDirectory(root) {
  const server = http.createServer((req, res) => {
    const url = new URL(req.url, "http://127.0.0.1");
    const decoded = decodeURIComponent(url.pathname).replace(/^\/+/, "");
    const filePath = path.normalize(path.join(root, decoded));
    if (!filePath.startsWith(root)) {
      res.writeHead(403);
      res.end("forbidden");
      return;
    }
    fs.readFile(filePath, (err, data) => {
      if (err) {
        res.writeHead(404);
        res.end("not found");
        return;
      }
      res.writeHead(200, { "content-type": contentType(filePath) });
      res.end(data);
    });
  });
  return new Promise((resolve) => {
    server.listen(0, "127.0.0.1", () => resolve(server));
  });
}

function outputText(capture) {
  return (capture.output || [])
    .map((entry) => (typeof entry.data === "string" ? entry.data : ""))
    .filter(Boolean)
    .join("\n");
}

(async () => {
  const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), "rsassy-webr-"));
  let server;
  let webR;
  try {
    webR = new WebR({ channelType: ChannelType.PostMessage, interactive: false });
    await webR.init();
    const [major, minor] = (webR.versionR || "4.5.0").split(".");
    const repoRoot = createLocalRepo(tmpRoot, `${major}.${minor}`);
    server = await serveDirectory(repoRoot);
    const repoUrl = `http://127.0.0.1:${server.address().port}`;

    console.log(`webR ${webR.version}; R ${webR.versionR}`);
    await webR.installPackages(["Rsassy"], { repos: [repoUrl], mount: false });

    const shelter = await new webR.Shelter();
    const capture = await shelter.captureR(`
library(Rsassy)
sassy_set_backend("wasm_simd128")
f <- sassy_features()
print(f)
stopifnot(identical(f$rsassy_dispatch, "static"))
stopifnot(identical(f$rsassy_selected_backend, "wasm_simd128"))
stopifnot(identical(f$rsassy_installed_backends, "wasm_simd128"))
stopifnot(identical(f$rsassy_supported_backends, "wasm_simd128"))
stopifnot(identical(f$target_arch, "wasm32"))
stopifnot(identical(f$target_os, "emscripten"))
stopifnot(isTRUE(f$selected_compiled_wasm_simd128))
m <- sassy_search(list("ATCGATCG"), list("GGGGATCGATCGTTTT"), 1, alphabet = "dna")
print(m)
stopifnot(nrow(m) == 3L)
`, { withAutoprint: false });
    const text = outputText(capture);
    if (text) console.log(text);
    console.log("PASS Rsassy webR check");
    await webR.close();
  } finally {
    if (server) server.close();
    if (webR) webR.close();
    fs.rmSync(tmpRoot, { recursive: true, force: true });
  }
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
