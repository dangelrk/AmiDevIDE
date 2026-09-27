#!/usr/bin/env python3
"""Resolve and download the external sources listed in sources.toml.

    acd_fetch.py                 resolve latest, download what is missing
    acd_fetch.py --offline       never touch the network; use var/downloads/
    acd_fetch.py --check-updates report newer versions, change nothing
    acd_fetch.py --update        re-resolve and refresh sources.lock.json
    acd_fetch.py --write-docs    regenerate docs/SOURCES.html and stop

Anything already sitting in var/downloads/ is used as-is, no matter how it got
there, so the manual route from docs/SOURCES.html works without extra flags.
"""

import argparse
import hashlib
import html
import json
import re
import shutil
import ssl
import subprocess
import sys
import tomllib
import urllib.error
import urllib.request
from datetime import date
from pathlib import Path

from acd_common import (
    DOCS, DOWNLOADS, SOURCES_LOCK, SOURCES_TOML,
    ensure_dirs, fail, info, ok, rel, step, warn,
)

UA = {"User-Agent": "AmiDevIDE-bootstrap/1.0"}
TIMEOUT = 60


def _ssl_context() -> ssl.SSLContext:
    """Default TLS context, preferring certifi's bundle when it is installed.

    A python.org build on macOS ships without a usable trust store unless
    "Install Certificates.command" was run, so plain urlopen() fails on every
    https:// URL.  certifi fixes that when present; when it is not, _urlopen()
    falls back to curl, which uses the system keychain.  Verification stays on
    in every one of those paths.
    """
    try:
        import certifi
        return ssl.create_default_context(cafile=certifi.where())
    except Exception:
        return ssl.create_default_context()


SSL_CTX = _ssl_context()


def _external_downloader() -> list[str] | None:
    """curl or wget, as a last resort for hosts whose Python has no CA bundle."""
    if shutil.which("curl"):
        return ["curl", "-fsSL", "--max-time", str(TIMEOUT)]
    if shutil.which("wget"):
        return ["wget", "-q", "-O-", "--timeout", str(TIMEOUT)]
    return None


def _urlopen(url: str):
    req = urllib.request.Request(url, headers=UA)
    return urllib.request.urlopen(req, timeout=TIMEOUT, context=SSL_CTX)


# ------------------------------------------------------------------ helpers

def load_sources():
    with SOURCES_TOML.open("rb") as fh:
        return tomllib.load(fh)["source"]


def load_lock() -> dict:
    if SOURCES_LOCK.exists():
        return json.loads(SOURCES_LOCK.read_text())
    return {}


def save_lock(lock: dict) -> None:
    SOURCES_LOCK.write_text(json.dumps(lock, indent=2, sort_keys=True) + "\n")


def get_text(url: str) -> str:
    try:
        with _urlopen(url) as resp:
            return resp.read().decode("utf-8", "replace")
    except ssl.SSLError:
        tool = _external_downloader()
        if not tool:
            raise
        proc = subprocess.run(tool + [url], capture_output=True)
        if proc.returncode != 0:
            raise
        return proc.stdout.decode("utf-8", "replace")


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def local_candidate(src: dict) -> Path | None:
    """An archive already in var/downloads/, however it got there."""
    exact = DOWNLOADS / src.get("filename", "")
    if src.get("filename") and exact.is_file():
        return exact
    hits: list[Path] = []
    for pattern in src.get("patterns", []):
        hits.extend(sorted(DOWNLOADS.glob(pattern)))
    return hits[-1] if hits else None


# ---------------------------------------------------------------- resolvers

def _vbcc_version_key(name: str):
    """vbcc0_9hP2.tar.gz -> (0, 9, 'h', 2) so that plain sorting is correct."""
    m = re.match(r"vbcc(\d+)_(\d+)([a-z]?)(?:P(\d+))?\.tar\.gz$", name)
    if not m:
        return (0, 0, "", 0)
    major, minor, letter, patch = m.groups()
    return (int(major), int(minor), letter or "", int(patch or 0))


def resolve_vbcc_tag(src: dict) -> tuple[str, str, str]:
    page = get_text(src["page"])
    urls = re.findall(r'href="(https?://[^"]*/tags/(vbcc[^"/]+\.tar\.gz))"', page)
    if not urls:
        raise LookupError("no vbcc source archives found on the vbcc page")
    best_url, best_name = max(urls, key=lambda u: _vbcc_version_key(u[1]))
    version = re.sub(r"^vbcc|\.tar\.gz$", "", best_name).replace("_", ".")
    return best_url, best_name, version


def resolve_vbcc_target(src: dict) -> tuple[str, str, str]:
    page = get_text(src["page"])
    fname = src["filename"]
    hits = re.findall(
        r'href="(https?://[^"]*/vbcc/(\d{4}-\d{2}-\d{2})/' + re.escape(fname) + r')"',
        page,
    )
    if not hits:
        raise LookupError(f"no dated {fname} found on the vbcc page")
    best_url, best_date = max(hits, key=lambda h: h[1])
    return best_url, fname, best_date


def resolve_static(src: dict) -> tuple[str, str, str]:
    url = src["url"]
    name = src.get("filename") or url.rsplit("/", 1)[-1]
    # A pinned release states its version; the others read it off a page.
    version = src.get("version", "")
    if not version and src.get("version_page"):
        try:
            page = get_text(src["version_page"])
            m = re.search(src["version_regex"], page)
            if m:
                version = m.group(1)
        except Exception:  # a missing version label must not stop a download
            pass
    return url, name, version


RESOLVERS = {
    "static": resolve_static,
    "vbcc_tag": resolve_vbcc_tag,
    "vbcc_target": resolve_vbcc_target,
}


def resolve(src: dict) -> tuple[str, str, str]:
    return RESOLVERS[src["resolver"]](src)


# ----------------------------------------------------------------- download

def _download_external(url: str, tmp: Path) -> None:
    """Fetch via curl/wget, which trust the OS certificate store."""
    tool = _external_downloader()
    if not tool:
        raise RuntimeError("no CA bundle for Python and neither curl nor wget found "
                           "(try: pip3 install certifi)")
    if tool[0] == "curl":
        cmd = ["curl", "-fL", "--max-time", str(TIMEOUT), "-o", str(tmp), url]
    else:
        cmd = ["wget", "-q", "--timeout", str(TIMEOUT), "-O", str(tmp), url]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"{tool[0]} failed: {proc.stderr.strip() or proc.returncode}")


def download(url: str, dest: Path) -> None:
    tmp = dest.with_suffix(dest.suffix + ".part")
    try:
        with _urlopen(url) as resp:
            total = int(resp.headers.get("Content-Length") or 0)
            got = 0
            with tmp.open("wb") as out:
                while chunk := resp.read(1 << 16):
                    out.write(chunk)
                    got += len(chunk)
                    if total and sys.stdout.isatty():
                        pct = got * 100 // total
                        print(f"\r    {dest.name}: {pct:3d}%  "
                              f"{got >> 10:,} / {total >> 10:,} KiB", end="", flush=True)
        if sys.stdout.isatty() and total:
            print()
    except ssl.SSLError as exc:
        info(f"TLS verification unavailable in this Python ({exc.reason if hasattr(exc, 'reason') else exc}); "
             f"retrying through the system certificate store")
        _download_external(url, tmp)
    tmp.replace(dest)


# --------------------------------------------------------------- SOURCES.html

def write_sources_html(sources: list[dict], lock: dict) -> Path:
    rows = []
    for src in sources:
        entry = lock.get(src["id"], {})
        url = entry.get("url") or src.get("url", "")
        fname = entry.get("filename") or src.get("filename", "")
        version = entry.get("version", "")
        rows.append(f"""      <tr>
        <td><strong>{html.escape(src['name'])}</strong><br>
            <span class="muted">{html.escape(src['description'])}</span></td>
        <td>{html.escape(version) or '&mdash;'}</td>
        <td><a href="{html.escape(url)}">{html.escape(fname)}</a><br>
            <span class="muted">{html.escape(src.get('notes', ''))}</span></td>
      </tr>""")

    body = "\n".join(rows)
    generated = date.today().strftime("%d.%m.%Y")
    doc = f"""<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AmiDevIDE &mdash; Quellen</title>
<style>
:root{{--bg:#f7f5f0;--surface:#fffdf8;--surface-2:#f0ece2;--border:#ddd6c6;
  --text:#1e1c18;--muted:#6b6558;--accent:#a3410b;--code:#efeade;
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}}
@media (prefers-color-scheme:dark){{:root:not([data-theme="light"]){{
  --bg:#16150f;--surface:#1e1d16;--surface-2:#262419;--border:#3a382b;
  --text:#eae6da;--muted:#9b9686;--accent:#f0913f;--code:#131209}}}}
:root[data-theme="dark"]{{--bg:#16150f;--surface:#1e1d16;--surface-2:#262419;
  --border:#3a382b;--text:#eae6da;--muted:#9b9686;--accent:#f0913f;--code:#131209}}
*{{box-sizing:border-box}}
body{{margin:0;background:var(--bg);color:var(--text);overflow-x:hidden;
  font:16px/1.65 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,sans-serif}}
.wrap{{max-width:58rem;margin:0 auto;padding:2.5rem 1.25rem 5rem}}
h1{{font-size:clamp(1.7rem,4vw,2.3rem);margin:0 0 .5rem;letter-spacing:-.02em}}
h2{{font-size:1.3rem;margin:2.5rem 0 .8rem;padding-top:1.1rem;border-top:1px solid var(--border)}}
.lede{{color:var(--muted);margin:0 0 .5rem;max-width:44rem}}
.meta{{font:.8rem/1.5 var(--mono);color:var(--muted)}}
a{{color:var(--accent);word-break:break-word}}
code{{font:.875em/1.4 var(--mono);background:var(--code);padding:.12em .38em;border-radius:3px}}
pre{{background:var(--code);border:1px solid var(--border);border-radius:6px;
  padding:.9rem 1rem;overflow-x:auto}}
pre code{{background:none;padding:0;font-size:.85rem;white-space:pre}}
.tablewrap{{overflow-x:auto;border:1px solid var(--border);border-radius:6px;
  background:var(--surface);margin:1rem 0}}
table{{border-collapse:collapse;width:100%;font-size:.9rem;min-width:32rem}}
th,td{{padding:.6rem .85rem;text-align:left;vertical-align:top;border-bottom:1px solid var(--border)}}
thead th{{background:var(--surface-2);font-size:.8rem;letter-spacing:.02em;white-space:nowrap}}
tbody tr:last-child td{{border-bottom:none}}
.muted{{color:var(--muted);font-size:.85em}}
.note{{background:var(--surface);border:1px solid var(--border);
  border-left:3px solid var(--accent);border-radius:0 6px 6px 0;padding:.9rem 1.1rem;margin:1.2rem 0}}
.note p:first-child{{margin-top:0}} .note p:last-child{{margin-bottom:0}}
</style>
</head>
<body>
<div class="wrap">
<h1>Quellen</h1>
<p class="lede">Diese Archive braucht AmiDevIDE, um die Toolchain zu bauen.
Normalerweise holt sie <code>bootstrap/acd_fetch.py</code> automatisch &mdash; diese
Seite ist der manuelle Weg, falls der Rechner offline ist oder ein Server klemmt.</p>
<p class="meta">Generiert am {generated} aus <code>bootstrap/sources.toml</code>. Nicht von Hand bearbeiten.</p>

<h2>So geht der manuelle Weg</h2>
<ol>
  <li>Die Archive unten herunterladen.</li>
  <li>Unver&auml;ndert nach <code>var/downloads/</code> legen &mdash; nicht entpacken,
      nicht umbenennen.</li>
  <li>Bootstrap starten:
      <pre><code>cmake --build build --target acd-toolchain</code></pre>
      oder direkt:
      <pre><code>python3 bootstrap/acd_fetch.py --offline
python3 bootstrap/acd_build_tools.py
python3 bootstrap/acd_install_sdk.py
python3 bootstrap/acd_gen_config.py</code></pre></li>
</ol>
<div class="note">
<p><strong>Bereits Entpacktes wird erkannt.</strong> Liegt ein Quellbaum schon unter
<code>var/src/</code> oder ein SDK unter <code>toolchain/sdk/</code>, wird der jeweilige
Schritt &uuml;bersprungen. Du kannst also auch eine bestehende Installation von Hand
hineinkopieren.</p>
</div>

<h2>Archive</h2>
<div class="tablewrap">
<table>
  <thead><tr><th>Komponente</th><th>Version</th><th>Download</th></tr></thead>
  <tbody>
{body}
  </tbody>
</table>
</div>

<h2>Auf Aktualit&auml;t pr&uuml;fen</h2>
<pre><code>python3 bootstrap/acd_fetch.py --check-updates   # meldet, &auml;ndert nichts
python3 bootstrap/acd_fetch.py --update          # zieht + schreibt sources.lock.json fort</code></pre>

<h2>Entpacken</h2>
<p><code>.lha</code> braucht ein Werkzeug, das nicht &uuml;berall vorhanden ist.
<code>acd_extract.py</code> probiert der Reihe nach <code>lha</code>, <code>lhasa</code>,
<code>7z</code> und zuletzt das Python-Modul <code>lhafile</code>. Wenn nichts davon
da ist:</p>
<pre><code>pip3 install lhafile          # portabel, keine Systempakete n&ouml;tig
# oder
brew install lhasa            # macOS
sudo apt install lhasa        # Debian/Ubuntu
pacman -S mingw-w64-x86_64-p7zip   # MSYS2</code></pre>
</div>
</body>
</html>
"""
    ensure_dirs(DOCS)
    out = DOCS / "SOURCES.html"
    out.write_text(doc)
    return out


# --------------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--offline", action="store_true",
                    help="never use the network; only accept what is in var/downloads/")
    ap.add_argument("--check-updates", action="store_true",
                    help="report newer versions and exit without changing anything")
    ap.add_argument("--update", action="store_true",
                    help="re-resolve every source even if it is already downloaded")
    ap.add_argument("--write-docs", action="store_true",
                    help="regenerate docs/SOURCES.html and exit")
    args = ap.parse_args()

    sources = load_sources()
    lock = load_lock()

    if args.write_docs:
        out = write_sources_html(sources, lock)
        ok(f"wrote {rel(out)}")
        return 0

    ensure_dirs(DOWNLOADS)

    if args.check_updates:
        step("Checking for newer upstream releases")
        newer = 0
        for src in sources:
            sid = src["id"]
            try:
                url, name, version = resolve(src)
            except Exception as exc:
                warn(f"{sid}: could not resolve ({exc})")
                continue
            was = lock.get(sid, {})
            if was.get("url") != url or (version and was.get("version") != version):
                newer += 1
                old = was.get("version") or was.get("filename") or "(not locked)"
                print(f"    {sid:20s} {old}  ->  {version or name}")
                print(f"    {'':20s} {url}")
            else:
                info(f"{sid:20s} up to date ({version or name})")
        if newer:
            warn(f"{newer} source(s) have newer releases -- run with --update to take them")
        else:
            ok("everything is at the locked version")
        return 0

    step("Fetching external sources")
    failures: list[str] = []

    for src in sources:
        sid = src["id"]
        entry = dict(lock.get(sid, {}))

        have_local = local_candidate(src)
        if have_local and not args.update:
            info(f"{sid:20s} already present: {rel(have_local)}")
            entry.setdefault("filename", have_local.name)
            entry["sha256"] = entry.get("sha256") or sha256_of(have_local)
            entry.setdefault("url", src.get("url", ""))
            lock[sid] = entry
            continue

        if args.offline:
            failures.append(f"{sid}: missing from {rel(DOWNLOADS)} and --offline was given")
            warn(f"{sid:20s} MISSING (offline) -- see docs/SOURCES.html")
            continue

        try:
            url, name, version = resolve(src)
        except Exception as exc:
            failures.append(f"{sid}: {exc}")
            warn(f"{sid:20s} could not resolve: {exc}")
            continue

        dest = DOWNLOADS / name
        if dest.is_file() and not args.update:
            info(f"{sid:20s} already present: {rel(dest)}")
        else:
            info(f"{sid:20s} downloading {version or name}")
            try:
                download(url, dest)
            except (urllib.error.URLError, OSError, RuntimeError) as exc:
                reason = getattr(exc, "reason", exc)
                failures.append(f"{sid}: download failed ({reason})")
                warn(f"{sid:20s} download failed: {reason}")
                continue

        lock[sid] = {
            "url": url,
            "filename": name,
            "version": version,
            "sha256": sha256_of(dest),
            "resolved": date.today().isoformat(),
        }
        ok(f"{sid:20s} {version or name}  ({dest.stat().st_size >> 10:,} KiB)")

    save_lock(lock)
    write_sources_html(sources, lock)

    if failures:
        print()
        warn("some sources are missing:")
        for f in failures:
            print(f"        {f}")
        print()
        print(f"    Put the archives into {rel(DOWNLOADS)} by hand and re-run;")
        print(f"    {rel(DOCS / 'SOURCES.html')} lists every download link.")
        return 1

    ok(f"lock file up to date: {rel(SOURCES_LOCK)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
