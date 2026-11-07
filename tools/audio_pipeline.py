#!/usr/bin/env python3
"""
Script di Generazione Vocale (TTS) e Mastering Audio DSP per RAT-V2.
Posizione: Philanthropy-System/tools/audio_pipeline.py
"""

import asyncio
import shutil
import subprocess
from pathlib import Path

# Percorsi calcolati automaticamente rispetto alla radice della repository
REPO_ROOT   = Path(__file__).resolve().parent.parent
AUDIO_DIR   = REPO_ROOT / "sd_card_assets" / "audio"
BACKUP_DIR  = REPO_ROOT / "sd_card_assets" / "raw_backup"

# Voce neurale italiana (alternative: "it-IT-GiuseppeNeural", "it-IT-IsabellaNeural")
TTS_VOICE = "it-IT-DiegoNeural"
TTS_RATE  = "+5%"

# Profilo 1: Voce ed Effetti Tattici (Taglio netto sotto 180Hz, boost intelligibilità 2.6kHz, compressore)
FILTER_VOICE_SFX = (
    "highpass=f=180:poles=2,"
    "equalizer=f=2600:t=q:w=1.2:g=4.0,"
    "acompressor=threshold=-16dB:ratio=3:attack=10:release=150:makeup=4dB,"
    "alimiter=limit=0.94:attack=5:release=50"
)

# Profilo 2: Brani Musicali (es. test.mp3 - Taglio morbido sotto 110Hz per non far gracchiare il cono + limiter)
FILTER_MUSIC = (
    "highpass=f=110:poles=2,"
    "acompressor=threshold=-14dB:ratio=2:attack=20:release=250:makeup=2dB,"
    "alimiter=limit=0.94:attack=5:release=50"
)

# Tutti gli annunci vocali richiesti da SearchDestroyMode e DominationMode
VOICE_PACK = {
    "common/match_start.mp3": "Partita iniziata! Raggiungere l'obiettivo.",
    "common/match_end.mp3":   "Tempo scaduto! Partita terminata.",
    "sd/bomb_armed.mp3":      "Attenzione! Bomba innescata!",
    "sd/bomb_defused.mp3":    "Bomba disinnescata! Ottimo lavoro.",
    "sd/sec_30.mp3":          "Trenta secondi alla detonazione!",
    "sd/sec_10.mp3":          "Dieci. Nove. Otto. Sette. Sei. Cinque. Quattro. Tre. Due. Uno.",
    "dom/zone_captured.mp3":  "Zona conquistata!",
    "dom/zone_contested.mp3": "Attenzione, conquista zona in corso!",
}

for m in range(1, 16):
    VOICE_PACK[f"sd/min_{m}.mp3"] = "Un minuto rimanente!" if m == 1 else f"{m} minuti rimanenti."


def check_ffmpeg():
    if shutil.which("ffmpeg") is None:
        raise RuntimeError(
            "FFmpeg non trovato nel PATH! Installalo con 'winget install Gyan.FFmpeg' "
            "e riavvia VS Code."
        )


def master_file(input_path: Path, output_path: Path, is_music: bool = False):
    """Converte in Mono 44.1kHz 128kbps CBR senza tag ID3 e applica il filtro DSP."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    dsp_filter = FILTER_MUSIC if is_music else FILTER_VOICE_SFX

    cmd = [
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-i", str(input_path),
        "-vn", "-map_metadata", "-1",
        "-ac", "1",
        "-ar", "44100",
        "-af", dsp_filter,
        "-c:a", "libmp3lame", "-b:a", "128k", "-write_xing", "0",
        str(output_path)
    ]
    subprocess.run(cmd, check=True)
    size_kb = output_path.stat().st_size / 1024.0
    tipo = "MUSIC" if is_music else "VOICE/SFX"
    print(f"  [OK - {tipo:9s}] {output_path.relative_to(AUDIO_DIR).as_posix():26s} ({size_kb:6.1f} KB)")


def backup_and_master_existing():
    """Salva una copia grezza in raw_backup/ (se non esiste già) e masterizza i file esistenti."""
    print("\n--- 1. Backup e Mastering dei file esistenti in sd_card_assets/audio ---")
    if not AUDIO_DIR.exists():
        AUDIO_DIR.mkdir(parents=True, exist_ok=True)
        return

    for mp3_file in list(AUDIO_DIR.rglob("*.mp3")):
        rel_path = mp3_file.relative_to(AUDIO_DIR)
        backup_file = BACKUP_DIR / rel_path

        # Se non abbiamo ancora fatto il backup dell'originale, salviamolo ora
        if not backup_file.exists():
            backup_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(mp3_file, backup_file)
            print(f"  [BACKUP CREATO] {rel_path.as_posix()} -> raw_backup/{rel_path.as_posix()}")

        # Masterizza partendo sempre dal file originale di backup (così non ri-comprime due volte!)
        is_music_track = (rel_path.name.lower() == "test.mp3" or "music" in rel_path.parts)
        master_file(backup_file, mp3_file, is_music=is_music_track)


async def generate_missing_tts(overwrite_all: bool = False):
    """Genera con edge-tts tutti i file vocali mancanti e li masterizza."""
    print("\n--- 2. Generazione e Mastering annunci vocali TTS ---")
    import edge_tts
    tmp_raw = AUDIO_DIR / "_tts_raw_temp.mp3"

    for rel_str, text in VOICE_PACK.items():
        target_path = AUDIO_DIR / rel_str
        if target_path.exists() and not overwrite_all:
            print(f"  [PRESENTE]      {rel_str:26s} (saltato, esiste già)")
            continue

        print(f"  [GENERO TTS]    {rel_str:26s} <- \"{text}\"")
        communicate = edge_tts.Communicate(text, TTS_VOICE, rate=TTS_RATE)
        await communicate.save(str(tmp_raw))

        # Salva anche il TTS grezzo nel backup e masterizza nella cartella audio
        backup_tts = BACKUP_DIR / rel_str
        backup_tts.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(tmp_raw, backup_tts)

        master_file(backup_tts, target_path, is_music=False)

    if tmp_raw.exists():
        tmp_raw.unlink()


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Pipeline Audio RAT-V2")
    parser.add_argument(
        "--overwrite-tts",
        action="store_true",
        help="Sovrascrive anche i file presenti (es. match_start/bomb_armed) con la voce TTS"
    )
    args = parser.parse_args()

    check_ffmpeg()
    backup_and_master_existing()
    asyncio.run(generate_missing_tts(overwrite_all=args.overwrite_tts))
    print("\n=== COMPLETATO! ===")
    print(f"Ora copia il contenuto di:\n  {AUDIO_DIR}\ndentro la cartella /audio della Micro-SD del RAT-V2.\n")