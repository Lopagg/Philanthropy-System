/**
 * @file AudioEngine.h
 * @brief Motore e Mixer Audio Polifonico I2S a 4 canali per RAT-V2 (ESP32-S3 N16R8).
 *
 * Architettura a 4 Canali Mixati in Tempo Reale (44.1 kHz, 16-bit):
 * - Canale MP3 0 (AUDIO_CH_VOICE): Annunci vocali (da Micro-SD o scaricati in PSRAM via HTTP).
 * - Canale MP3 1 (AUDIO_CH_SFX)  : Effetti sonori / Sirene / Esplosioni (in sovrapposizione alla voce).
 * - Canale DDS 2 (SWEEP)         : Generatore di frequenza continua variabile (HoldButtonInteraction).
 * - Canale DDS 3 (BEEP)          : Generatore di toni brevi (Tastierino PIN, RFID, Beep Countdown).
 *
 * Caratteristiche:
 * - Pre-caricamento automatico in PSRAM (8 MB N16R8) per azzerare gli accessi concorrenti alla SD.
 * - Resampler lineare frazionario su ciascun canale MP3 (supporta 16/22.05/24/32/44.1/48 kHz).
 * - Auto-Ducking: attenua automaticamente SFX e Sweep quando parla il canale VOICE.
 * - DSP High-Pass (180 Hz) + Presence Boost + Soft-Limiter anti-clipping per MAX98357A.
 */

#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <Arduino.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <MP3DecoderHelix.h>

enum AudioChannel : uint8_t {
    AUDIO_CH_VOICE = 0, // Canale prioritario per voci e annunci (attiva auto-ducking)
    AUDIO_CH_SFX   = 1, // Canale per effetti sonori, allarmi ed esplosioni
    AUDIO_CH_AUTO  = 2  // Scelta automatica: usa VOICE per annunci o il primo slot libero
};

class AudioEngine {
public:
    static const uint32_t MIXER_SAMPLE_RATE = 44100;
    static const size_t   MIX_CHUNK_FRAMES  = 256;          // ~5.8 ms di latenza per blocco
    static const size_t   MAX_PSRAM_FILE_SZ = 512 * 1024;   // Fino a 512 KB precaricati in PSRAM
    static const size_t   STREAM_BUF_SIZE   = 4096;         // Buffer 4 KB per file MP3 > 512 KB
    static const size_t   PCM_RING_SAMPLES  = 4096;         // Ring buffer PCM post-resampling

    AudioEngine();
    ~AudioEngine();

    /**
     * @brief Inizializza la periferica hardware I2S_NUM_0 e alloca i buffer in PSRAM (se presente).
     */
    void begin(uint8_t pinBclk, uint8_t pinLrc, uint8_t pinDout, uint8_t pinAudioEn);

    /**
     * @brief Ciclo di mixaggio real-time da richiamare continuamente in audioTask (Core 1).
     */
    void loop();

    // --- Riproduzione File MP3 (Micro-SD) e Streaming HTTP TTS (PSRAM) ---
    bool playFile(const String& filename, AudioChannel channel = AUDIO_CH_AUTO);
    bool playUrl(const String& url, AudioChannel channel = AUDIO_CH_VOICE);
    void stopChannel(uint8_t slot);
    void stopAllMp3();

    // --- Sintetizzatori DDS Indipendenti (Non interrompono mai gli MP3!) ---
    void startToneSweep(uint16_t startFreq, uint16_t endFreq, uint32_t durationMs);
    void stopToneSweep();
    void playTone(uint16_t freq, uint32_t durationMs);
    void stopAllTones();

    // --- Controllo Volumi e Stato ---
    void setMasterVolume(uint8_t vol); // 0..21 (compatibile con la scala precedente)
    bool isPlaying() const;            // true se almeno un canale MP3 sta riproducendo
    bool isChannelPlaying(uint8_t slot) const;

private:
    struct Mp3Slot {
        bool active;
        bool usePsramCache;
        uint8_t* psramData;
        size_t psramSize;
        size_t psramReadOffset;

        File sdFile;
        uint8_t* streamBuf;
        size_t streamBytesLeft;

        HMP3Decoder decoder;
        int16_t* rawFrameBuf;     // 2304 int16_t (1 frame MP3 decodificato)
        int16_t* pcmRing;         // Buffer circolare mono a 44100 Hz
        size_t ringReadIdx;
        size_t ringWriteIdx;
        size_t ringCount;

        uint32_t lastSampleRate;
        uint32_t resamplePhase;   // Fase 16.16 fixed-point per conversione sample rate
        int16_t prevSample;
        float gain;
    };

    struct DdsChannel {
        volatile bool active;
        uint16_t startFreq;
        uint16_t endFreq;
        uint32_t totalSamples;
        uint32_t sampleIndex;
        uint32_t phase;
        float gain;
    };

    uint8_t _pinBclk, _pinLrc, _pinDout, _pinAudioEn;
    bool _initialized;
    bool _ampEnabled;
    unsigned long _lastActiveAudioMs;

    SemaphoreHandle_t _mutex;
    float _masterGain;

    Mp3Slot _mp3Slots[2];
    DdsChannel _sweepDds; // Canale dedicato per HoldButtonInteraction
    DdsChannel _beepDds;  // Canale dedicato per PinInteraction / Rfid / CountdownTimer

    // Stato filtro DSP High-Pass (180 Hz) + Presence Boost
    float _hpPrevIn;
    float _hpPrevOut;
    float _lpPrevOut;

    String resolveSdPath(const String& requestedPath, SemaphoreHandle_t sdMutex);
    void closeSlotInternal(uint8_t slot);
    size_t skipId3v2Tag(const uint8_t* data, size_t len);
    bool decodeNextMp3Frame(uint8_t slot, SemaphoreHandle_t sdMutex);
    void pushResampledMono(Mp3Slot& s, const int16_t* samples, int numFrames, int numChans, uint32_t srcRate);
    int16_t sampleDds(DdsChannel& dds);
    int16_t applySpeakerDsp(int32_t mixSample);
};

#endif // AUDIO_ENGINE_H