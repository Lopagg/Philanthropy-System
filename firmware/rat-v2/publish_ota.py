"""
publish_ota.py
Automazione One-Click per il rilascio OTA del firmware RAT-V2:
1. Incrementa automaticamente FIRMWARE_VERSION in include/HardwareManager.h (es. v0.0.1 -> v0.0.2)
2. Compila il firmware con PlatformIO (con rollback automatico se la build fallisce)
3. Copia il binario in server/static/firmware/rat-v2.bin e aggiorna manifest.json
4. Esegue git add, git commit e git push verso GitHub
"""

import os
import sys
import re
import glob
import json
import shutil
import subprocess
from datetime import datetime


def find_pio_executable():
    """Trova l'eseguibile di PlatformIO su Windows/Linux/macOS anche fuori dal PATH standard."""
    pio_in_path = shutil.which("pio") or shutil.which("platformio")
    if pio_in_path:
        return pio_in_path

    home = os.path.expanduser("~")
    candidates = [
        os.path.join(home, ".platformio", "penv", "Scripts", "pio.exe"),
        os.path.join(home, ".platformio", "penv", "bin", "pio"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def bump_version(ver_str):
    """Incrementa di +1 l'ultimo numero della versione (es. 'v0.0.1' -> 'v0.0.2')."""
    match = re.match(r"^([vV]?\d+\.\d+\.)(\d+)$", ver_str.strip())
    if not match:
        raise ValueError(f"Formato versione non riconosciuto: '{ver_str}' (atteso es. v0.0.1)")
    prefix, patch = match.groups()
    return f"{prefix}{int(patch) + 1}"


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    header_path = os.path.join(script_dir, "include", "HardwareManager.h")

    if not os.path.exists(header_path):
        print(f"[ERRORE] File non trovato: {header_path}")
        sys.exit(1)

    # 1. Legge la versione attuale e calcola la nuova
    with open(header_path, "r", encoding="utf-8") as f:
        header_content = f.read()

    match = re.search(r'(#define\s+FIRMWARE_VERSION\s+")([^"]+)(")', header_content)
    if not match:
        print("[ERRORE] #define FIRMWARE_VERSION non trovata in HardwareManager.h")
        sys.exit(1)

    old_version = match.group(2)
    new_version = sys.argv[1] if len(sys.argv) > 1 else bump_version(old_version)

    print(f"=== AVVIO RILASCIO FIRMWARE RAT-V2: {old_version} -> {new_version} ===")

    # 2. Scrive la nuova versione in HardwareManager.h
    updated_header = (
        header_content[:match.start(2)] + new_version + header_content[match.end(2):]
    )
    with open(header_path, "w", encoding="utf-8") as f:
        f.write(updated_header)

    # 3. Compila il firmware con PlatformIO
    pio_exe = find_pio_executable()
    if not pio_exe:
        print("[ERRORE] Eseguibile PlatformIO (pio) non trovato!")
        with open(header_path, "w", encoding="utf-8") as f:
            f.write(header_content)
        sys.exit(1)

    print("[1/4] Compilazione firmware con PlatformIO in corso...")
    build_res = subprocess.run([pio_exe, "run"], cwd=script_dir)
    if build_res.returncode != 0:
        print("[ERRORE] Compilazione fallita! Ripristino versione precedente in HardwareManager.h.")
        with open(header_path, "w", encoding="utf-8") as f:
            f.write(header_content)
        sys.exit(1)

    # 4. Copia firmware.bin e aggiorna manifest.json
    print("[2/4] Aggiornamento binario OTA e manifest.json...")
    bin_candidates = glob.glob(os.path.join(script_dir, ".pio", "build", "*", "firmware.bin"))
    if not bin_candidates:
        print("[ERRORE] Nessun firmware.bin trovato dopo la compilazione!")
        sys.exit(1)

    firmware_bin = max(bin_candidates, key=os.path.getmtime)
    dest_dir = os.path.join(repo_root, "server", "static", "firmware")
    os.makedirs(dest_dir, exist_ok=True)
    dest_bin = os.path.join(dest_dir, "rat-v2.bin")
    shutil.copyfile(firmware_bin, dest_bin)

    manifest_path = os.path.join(dest_dir, "manifest.json")
    manifest = {}
    if os.path.exists(manifest_path):
        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                manifest = json.load(f)
        except Exception:
            manifest = {}

    manifest["rat-v2"] = {
        "version": new_version,
        "file": "rat-v2.bin",
        "size_bytes": os.path.getsize(dest_bin),
        "updated_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    }

    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=4)

    # 5. Commit e Push automatico su GitHub
    print("[3/4] Creazione commit Git...")
    files_to_stage = [
        "firmware/rat-v2/include/HardwareManager.h",
        "server/static/firmware/rat-v2.bin",
        "server/static/firmware/manifest.json"
    ]
    subprocess.run(["git", "add"] + files_to_stage, cwd=repo_root, check=True)
    subprocess.run(["git", "commit", "-m", f"Release firmware RAT-V2 {new_version}"], cwd=repo_root, check=True)

    print("[4/4] Invio aggiornamento a GitHub (git push)...")
    subprocess.run(["git", "push"], cwd=repo_root, check=True)

    print(f"\n=== RILASCIO {new_version} COMPLETATO E PUBBLICATO SU GITHUB! ===")


if __name__ == "__main__":
    main()