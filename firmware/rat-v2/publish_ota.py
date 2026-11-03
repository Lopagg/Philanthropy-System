"""
publish_ota.py
Script manuale per pubblicare la build corrente del RAT-V2 sul server.
Eseguilo solo quando vuoi rilasciare ufficialmente una nuova versione per l'OTA.
"""

import os
import re
import glob
import json
import shutil
from datetime import datetime

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))

    # 1. Legge FIRMWARE_VERSION da include/HardwareManager.h (es. "v0.0.1")
    header_path = os.path.join(script_dir, "include", "HardwareManager.h")
    fw_version = "v0.0.1"
    if os.path.exists(header_path):
        with open(header_path, "r", encoding="utf-8") as f:
            match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', f.read())
            if match:
                fw_version = match.group(1)

    # 2. Cerca il file firmware.bin compilato dentro .pio/build/
    bin_candidates = glob.glob(os.path.join(script_dir, ".pio", "build", "*", "firmware.bin"))
    if not bin_candidates:
        print("[ERRORE] Nessun file firmware.bin trovato in .pio/build/!")
        print("Compila prima il progetto con PlatformIO: Build.")
        return

    # Prende il firmware.bin modificato più di recente
    firmware_bin = max(bin_candidates, key=os.path.getmtime)

    # 3. Copia il binario in server/static/firmware/rat-v2.bin
    dest_dir = os.path.abspath(os.path.join(script_dir, "..", "..", "server", "static", "firmware"))
    os.makedirs(dest_dir, exist_ok=True)
    dest_bin = os.path.join(dest_dir, "rat-v2.bin")
    shutil.copyfile(firmware_bin, dest_bin)

    # 4. Aggiorna server/static/firmware/manifest.json
    manifest_path = os.path.join(dest_dir, "manifest.json")
    manifest = {}
    if os.path.exists(manifest_path):
        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                manifest = json.load(f)
        except Exception:
            manifest = {}

    manifest["rat-v2"] = {
        "version": fw_version,
        "file": "rat-v2.bin",
        "size_bytes": os.path.getsize(dest_bin),
        "updated_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    }

    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=4)

    print(f"[OTA-PUBLISH] Versione '{fw_version}' pubblicata con successo!")
    print(f" -> File: {dest_bin} ({manifest['rat-v2']['size_bytes']} byte)")
    print(f" -> Manifesto: {manifest_path}")

if __name__ == "__main__":
    main()