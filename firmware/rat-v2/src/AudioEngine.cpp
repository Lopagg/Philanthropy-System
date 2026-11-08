/**
 * @file AudioEngine.cpp
 * @brief Implementazione del Mixer Polifonico a 4 canali (2x MP3 Helix + 2x DDS) con cache PSRAM.
 */

#include "AudioEngine.h"
#include <driver/i2s.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>

// Helper per allocare preferibilmente negli 8 MB di PSRAM dell'ESP32-S3 N16R8
static void* allocAudioMem(size_t size) {
    if (psramFound()) {
        void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
    }
    return malloc(size);
}

// ============================================================================
// COSTRUTTORE, DISTRUTTORE E INIZIALIZZAZIONE I2S
// ============================================================================

AudioEngine::AudioEngine()
    : _pinBclk(0), _pinLrc(0), _pinDout(0), _pinAudioEn(0),
      _initialized(false),
      _ampEnabled(false),
      _lastActiveAudioMs(0),
      _masterGain(1.0f),
      _hpPrevIn(0.0f),
      _hpPrevOut(0.0f),
      _lpPrevOut(0.0f) {
    _mutex = xSemaphoreCreateMutex();
    memset(_mp3Slots, 0, sizeof(_mp3Slots));
    memset((void*)&_sweepDds, 0, sizeof(_sweepDds));
    memset((void*)&_beepDds, 0, sizeof(_beepDds));
}

AudioEngine::~AudioEngine() {
    for (uint8_t i = 0; i < 2; i++) {
        closeSlotInternal(i);
        if (_mp3Slots[i].decoder)     MP3FreeDecoder(_mp3Slots[i].decoder);
        if (_mp3Slots[i].streamBuf)   free(_mp3Slots[i].streamBuf);
        if (_mp3Slots[i].rawFrameBuf) free(_mp3Slots[i].rawFrameBuf);
        if (_mp3Slots[i].pcmRing)     free(_mp3Slots[i].pcmRing);
    }
}

void AudioEngine::begin(uint8_t pinBclk, uint8_t pinLrc, uint8_t pinDout, uint8_t pinAudioEn) {
    _pinBclk = pinBclk;
    _pinLrc = pinLrc;
    _pinDout = pinDout;
    _pinAudioEn = pinAudioEn;

    pinMode(_pinAudioEn, OUTPUT);
    digitalWrite(_pinAudioEn, LOW);
    _ampEnabled = false;

    // 1. Configurazione driver I2S_NUM_0 fisso a 44100 Hz, 16-bit Stereo
    i2s_config_t i2sConfig = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = MIXER_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pinConfig = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = _pinBclk,
        .ws_io_num = _pinLrc,
        .data_out_num = _pinDout,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pinConfig);
    i2s_zero_dma_buffer(I2S_NUM_0);

    // 2. Allocazione dei 2 Decoder MP3 Helix e dei relativi buffer in PSRAM
    for (uint8_t i = 0; i < 2; i++) {
        _mp3Slots[i].decoder     = MP3InitDecoder();
        _mp3Slots[i].streamBuf   = (uint8_t*)allocAudioMem(STREAM_BUF_SIZE);
        _mp3Slots[i].rawFrameBuf = (int16_t*)allocAudioMem(2304 * sizeof(int16_t));
        _mp3Slots[i].pcmRing     = (int16_t*)allocAudioMem(PCM_RING_SAMPLES * sizeof(int16_t));
        _mp3Slots[i].gain        = (i == AUDIO_CH_VOICE) ? 1.05f : 0.90f;
        _mp3Slots[i].lastSampleRate = MIXER_SAMPLE_RATE;
    }

    _sweepDds.gain = 0.65f;
    _beepDds.gain  = 0.75f;
    _initialized   = true;

    Serial.printf("[AUDIO-ENGINE] Mixer 4 canali attivo (PSRAM: %s, Libera: %u KB)\n",
                  psramFound() ? "OK" : "NO",
                  (uint32_t)(ESP.getFreePsram() / 1024));
}

// ============================================================================
// RISOLUZIONE PERCORSI SD E PRE-CARICAMENTO IN PSRAM
// ============================================================================

String AudioEngine::resolveSdPath(const String& requestedPath, SemaphoreHandle_t sdMutex) {
    auto existsValid = [](const String& p) -> bool {
        if (!SD.exists(p)) return false;
        File f = SD.open(p, FILE_READ);
        if (!f) return false;
        size_t sz = f.size();
        f.close();
        return (sz > 256);
    };

    if (existsValid(requestedPath)) return requestedPath;

    int lastSlash = requestedPath.lastIndexOf('/');
    if (lastSlash >= 0) {
        String baseName = requestedPath.substring(lastSlash);
        if (existsValid(baseName)) return baseName;

        String legacy = baseName;
        legacy.replace("/bomb_exploded.mp3", "/explosion.mp3");
        legacy.replace("/bomb_armed.mp3", "/armed.mp3");
        legacy.replace("/bomb_defused.mp3", "/defused.mp3");
        if (existsValid(legacy)) return legacy;
    }
    return "";
}

size_t AudioEngine::skipId3v2Tag(const uint8_t* data, size_t len) {
    if (len >= 10 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        uint32_t tagSize = ((data[6] & 0x7F) << 21) |
                           ((data[7] & 0x7F) << 14) |
                           ((data[8] & 0x7F) << 7)  |
                           (data[9] & 0x7F);
        size_t totalSkip = tagSize + 10;
        if (totalSkip < len) return totalSkip;
    }
    return 0;
}

void AudioEngine::closeSlotInternal(uint8_t slot) {
    Mp3Slot& s = _mp3Slots[slot];
    s.active = false;
    if (s.sdFile) {
        s.sdFile.close();
    }
    if (s.psramData) {
        free(s.psramData);
        s.psramData = nullptr;
    }
    s.psramSize = 0;
    s.psramReadOffset = 0;
    s.streamBytesLeft = 0;
    s.ringReadIdx = 0;
    s.ringWriteIdx = 0;
    s.ringCount = 0;
    s.resamplePhase = 0;
    s.prevSample = 0;
}

bool AudioEngine::playFile(const String& filename, AudioChannel channel) {
    if (!_initialized) return false;

    // Scelta automatica del canale: annunci (min_, sec_, match_) su VOICE, effetti su SFX
    uint8_t targetSlot = AUDIO_CH_VOICE;
    if (channel == AUDIO_CH_SFX) {
        targetSlot = AUDIO_CH_SFX;
    } else if (channel == AUDIO_CH_AUTO) {
        if (filename.indexOf("explo") != -1 || filename.indexOf("siren") != -1 || filename.indexOf("alarm") != -1) {
            targetSlot = AUDIO_CH_SFX;
        } else if (_mp3Slots[AUDIO_CH_VOICE].active && !_mp3Slots[AUDIO_CH_SFX].active) {
            targetSlot = AUDIO_CH_SFX;
        } else {
            targetSlot = AUDIO_CH_VOICE;
        }
    }

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(150)) != pdTRUE) return false;

    String path = resolveSdPath(filename, nullptr);
    if (path.length() == 0) {
        xSemaphoreGive(_mutex);
        return false;
    }

    closeSlotInternal(targetSlot);
    Mp3Slot& s = _mp3Slots[targetSlot];

    File f = SD.open(path, FILE_READ);
    if (!f) {
        xSemaphoreGive(_mutex);
        return false;
    }

    size_t fileSize = f.size();
    // Se il file terminava con il vecchio padding ZPAD, ignora il marker finale
    if (fileSize > 10012) {
        uint8_t marker[4] = {0};
        f.seek(fileSize - 4);
        f.read(marker, 4);
        if (memcmp(marker, "ZPAD", 4) == 0) {
            fileSize -= (24 * 417 + 4);
        }
        f.seek(0);
    }

    // Se entra in PSRAM (<= 512 KB), caricalo interamente in RAM e chiudi subito il file SD!
    if (psramFound() && fileSize > 0 && fileSize <= MAX_PSRAM_FILE_SZ) {
        uint8_t* ramBuf = (uint8_t*)heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ramBuf) {
            size_t readBytes = f.read(ramBuf, fileSize);
            f.close();
            if (readBytes > 128) {
                s.usePsramCache = true;
                s.psramData = ramBuf;
                s.psramSize = readBytes;
                s.psramReadOffset = skipId3v2Tag(ramBuf, readBytes);
                s.active = true;
                xSemaphoreGive(_mutex);
                return true;
            }
            free(ramBuf);
        }
    }

    // Fallback streaming a blocchi da 4 KB per file di grandi dimensioni
    s.usePsramCache = false;
    s.sdFile = f;
    s.streamBytesLeft = s.sdFile.read(s.streamBuf, STREAM_BUF_SIZE);
    size_t id3Skip = skipId3v2Tag(s.streamBuf, s.streamBytesLeft);
    if (id3Skip > 0) {
        s.sdFile.seek(id3Skip);
        s.streamBytesLeft = s.sdFile.read(s.streamBuf, STREAM_BUF_SIZE);
    }
    s.active = (s.streamBytesLeft > 0);

    xSemaphoreGive(_mutex);
    return s.active;
}

bool AudioEngine::playUrl(const String& url, AudioChannel channel) {
    if (!_initialized || WiFi.status() != WL_CONNECTED || url.length() == 0) return false;

    Serial.printf("[AUDIO-URL] Download da: %s\n", url.c_str());

    WiFiClient client;
    HTTPClient http;
    http.useHTTP10(true); // Forza risposta diretta senza chunked encoding per chiusura immediata
    http.setTimeout(3500);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    if (!http.begin(client, url)) {
        Serial.println("[AUDIO-URL] Errore http.begin()");
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[AUDIO-URL] Errore HTTP GET: codice %d (%s)\n",
                      httpCode, http.errorToString(httpCode).c_str());
        http.end();
        return false;
    }

    int contentLen = http.getSize();
    size_t allocSz = (contentLen > 0 && contentLen <= (int)MAX_PSRAM_FILE_SZ) ? (size_t)contentLen : (192 * 1024);
    uint8_t* dlBuf = (uint8_t*)allocAudioMem(allocSz);
    if (!dlBuf) {
        Serial.println("[AUDIO-URL] Errore allocazione PSRAM");
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t totalRead = 0;
    unsigned long startMs = millis();
    while ((http.connected() || stream->available() > 0) &&
           (contentLen > 0 ? (totalRead < (size_t)contentLen) : (totalRead < allocSz)) &&
           (millis() - startMs < 3500)) {
        size_t avail = stream->available();
        if (avail > 0) {
            size_t toRead = min(avail, allocSz - totalRead);
            if (toRead == 0) break;
            int r = stream->readBytes(dlBuf + totalRead, toRead);
            if (r > 0) totalRead += r;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    http.end();

    Serial.printf("[AUDIO-URL] Scaricati %u byte in %lu ms\n",
                  (uint32_t)totalRead, millis() - startMs);

    if (totalRead <= 256) {
        free(dlBuf);
        return false;
    }

    uint8_t targetSlot = (channel == AUDIO_CH_SFX) ? AUDIO_CH_SFX : AUDIO_CH_VOICE;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(150)) != pdTRUE) {
        free(dlBuf);
        return false;
    }

    closeSlotInternal(targetSlot);
    Mp3Slot& s = _mp3Slots[targetSlot];
    s.usePsramCache = true;
    s.psramData = dlBuf;
    s.psramSize = totalRead;
    s.psramReadOffset = skipId3v2Tag(dlBuf, totalRead);
    s.active = true;

    xSemaphoreGive(_mutex);
    return true;
}

void AudioEngine::stopChannel(uint8_t slot) {
    if (slot >= 2) return;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        closeSlotInternal(slot);
        xSemaphoreGive(_mutex);
    }
}

void AudioEngine::stopAllMp3() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        closeSlotInternal(0);
        closeSlotInternal(1);
        xSemaphoreGive(_mutex);
    }
}

// ============================================================================
// SINTETIZZATORI DDS INDIPENDENTI (SWEEP + BEEP)
// ============================================================================

void AudioEngine::startToneSweep(uint16_t startFreq, uint16_t endFreq, uint32_t durationMs) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        uint32_t dur = (durationMs > 0) ? durationMs : 1;
        _sweepDds.startFreq = startFreq;
        _sweepDds.endFreq = endFreq;
        _sweepDds.totalSamples = ((uint64_t)dur * MIXER_SAMPLE_RATE) / 1000ULL;
        if (_sweepDds.totalSamples == 0) _sweepDds.totalSamples = 44;
        _sweepDds.sampleIndex = 0;
        _sweepDds.phase = 0;
        _sweepDds.active = true;
        xSemaphoreGive(_mutex);
    }
}

void AudioEngine::stopToneSweep() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _sweepDds.active = false;
        xSemaphoreGive(_mutex);
    }
}

void AudioEngine::playTone(uint16_t freq, uint32_t durationMs) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        uint32_t dur = (durationMs > 0) ? durationMs : 1;
        _beepDds.startFreq = freq;
        _beepDds.endFreq = freq;
        _beepDds.totalSamples = ((uint64_t)dur * MIXER_SAMPLE_RATE) / 1000ULL;
        if (_beepDds.totalSamples == 0) _beepDds.totalSamples = 44;
        _beepDds.sampleIndex = 0;
        _beepDds.phase = 0;
        _beepDds.active = true;
        xSemaphoreGive(_mutex);
    }
}

void AudioEngine::stopAllTones() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _sweepDds.active = false;
        _beepDds.active = false;
        xSemaphoreGive(_mutex);
    }
}

int16_t AudioEngine::sampleDds(DdsChannel& dds) {
    if (!dds.active) return 0;
    if (dds.sampleIndex >= dds.totalSamples) {
        dds.active = false;
        return 0;
    }

    int32_t fStart = dds.startFreq;
    int32_t fEnd = dds.endFreq;
    uint16_t curFreq = fStart + ((int64_t)(fEnd - fStart) * (int64_t)dds.sampleIndex) / (int64_t)dds.totalSamples;
    dds.sampleIndex++;

    uint32_t phaseStep = ((uint64_t)curFreq << 16) / MIXER_SAMPLE_RATE;
    dds.phase = (dds.phase + phaseStep) & 0xFFFF;
    float angle = (dds.phase * 6.2831853f) / 65536.0f;

    // Inviluppo fade-in / fade-out di 3 ms (132 campioni) per eliminare qualsiasi "click"
    float env = 1.0f;
    if (dds.sampleIndex < 132) {
        env = dds.sampleIndex / 132.0f;
    } else if (dds.totalSamples - dds.sampleIndex < 132) {
        env = (dds.totalSamples - dds.sampleIndex) / 132.0f;
    }

    return (int16_t)(sinf(angle) * 24000.0f * dds.gain * env);
}

// ============================================================================
// DECODIFICA MP3 HELIX, RESAMPLING A 44.1 kHz E MIXING
// ============================================================================

void AudioEngine::pushResampledMono(Mp3Slot& s, const int16_t* samples, int numFrames, int numChans, uint32_t srcRate) {
    if (srcRate == 0) srcRate = MIXER_SAMPLE_RATE;
    uint32_t stepFp = (srcRate << 16) / MIXER_SAMPLE_RATE;

    for (int i = 0; i < numFrames; i++) {
        int32_t mono = samples[i * numChans];
        if (numChans == 2) {
            mono = (mono + samples[i * 2 + 1]) >> 1;
        }
        int16_t curSample = (int16_t)mono;

        if (srcRate == MIXER_SAMPLE_RATE) {
            if (s.ringCount < PCM_RING_SAMPLES) {
                s.pcmRing[s.ringWriteIdx] = curSample;
                s.ringWriteIdx = (s.ringWriteIdx + 1) % PCM_RING_SAMPLES;
                s.ringCount++;
            }
        } else {
            while (s.resamplePhase < 65536 && s.ringCount < PCM_RING_SAMPLES) {
                int32_t interp = s.prevSample + (((int32_t)(curSample - s.prevSample) * (int32_t)s.resamplePhase) >> 16);
                s.pcmRing[s.ringWriteIdx] = (int16_t)interp;
                s.ringWriteIdx = (s.ringWriteIdx + 1) % PCM_RING_SAMPLES;
                s.ringCount++;
                s.resamplePhase += stepFp;
            }
            s.resamplePhase -= 65536;
        }
        s.prevSample = curSample;
    }
}

bool AudioEngine::decodeNextMp3Frame(uint8_t slot, SemaphoreHandle_t sdMutex) {
    Mp3Slot& s = _mp3Slots[slot];
    if (!s.active || !s.decoder) return false;

    if (s.usePsramCache) {
        while (s.psramReadOffset < s.psramSize) {
            int rem = (int)(s.psramSize - s.psramReadOffset);
            if (rem < 4) {
                closeSlotInternal(slot);
                return false;
            }
            uint8_t* ptr = s.psramData + s.psramReadOffset;
            int syncOffset = MP3FindSyncWord(ptr, rem);
            if (syncOffset < 0) {
                closeSlotInternal(slot);
                return false;
            }
            s.psramReadOffset += syncOffset;
            ptr = s.psramData + s.psramReadOffset;
            int bytesLeft = (int)(s.psramSize - s.psramReadOffset);

            int err = MP3Decode(s.decoder, &ptr, &bytesLeft, s.rawFrameBuf, 0);
            s.psramReadOffset = s.psramSize - bytesLeft;

            if (err == ERR_MP3_NONE) {
                MP3FrameInfo info;
                MP3GetLastFrameInfo(s.decoder, &info);
                if (info.outputSamps > 0 && info.nChans > 0) {
                    int frames = info.outputSamps / info.nChans;
                    pushResampledMono(s, s.rawFrameBuf, frames, info.nChans, info.samprate);
                    return true;
                }
            } else if (err == ERR_MP3_INDATA_UNDERFLOW) {
                closeSlotInternal(slot);
                return false;
            } else {
                s.psramReadOffset++;
            }
        }
        closeSlotInternal(slot);
        return false;
    }

    // Modalità Streaming da SD
    while (s.active) {
        if (s.streamBytesLeft < 1024 && s.sdFile && s.sdFile.available()) {
            size_t r = s.sdFile.read(s.streamBuf + s.streamBytesLeft, STREAM_BUF_SIZE - s.streamBytesLeft);
            s.streamBytesLeft += r;
        }
        if (s.streamBytesLeft < 4) {
            closeSlotInternal(slot);
            return false;
        }

        int syncOffset = MP3FindSyncWord(s.streamBuf, s.streamBytesLeft);
        if (syncOffset < 0) {
            if (s.sdFile && s.sdFile.available()) {
                s.streamBytesLeft = s.sdFile.read(s.streamBuf, STREAM_BUF_SIZE);
                continue;
            }
            closeSlotInternal(slot);
            return false;
        }

        if (syncOffset > 0) {
            memmove(s.streamBuf, s.streamBuf + syncOffset, s.streamBytesLeft - syncOffset);
            s.streamBytesLeft -= syncOffset;
            if (s.streamBytesLeft < 1024 && s.sdFile && s.sdFile.available()) {
                s.streamBytesLeft += s.sdFile.read(s.streamBuf + s.streamBytesLeft, STREAM_BUF_SIZE - s.streamBytesLeft);
            }
        }

        uint8_t* readPtr = s.streamBuf;
        int bytesLeft = (int)s.streamBytesLeft;
        int err = MP3Decode(s.decoder, &readPtr, &bytesLeft, s.rawFrameBuf, 0);
        size_t consumed = s.streamBytesLeft - bytesLeft;

        if (consumed > 0) {
            memmove(s.streamBuf, s.streamBuf + consumed, bytesLeft);
            s.streamBytesLeft = bytesLeft;
        }

        if (err == ERR_MP3_NONE) {
            MP3FrameInfo info;
            MP3GetLastFrameInfo(s.decoder, &info);
            if (info.outputSamps > 0 && info.nChans > 0) {
                int frames = info.outputSamps / info.nChans;
                pushResampledMono(s, s.rawFrameBuf, frames, info.nChans, info.samprate);
                return true;
            }
        } else if (err == ERR_MP3_INDATA_UNDERFLOW) {
            if (!s.sdFile || !s.sdFile.available()) {
                closeSlotInternal(slot);
                return false;
            }
        } else {
            if (s.streamBytesLeft > 0) {
                memmove(s.streamBuf, s.streamBuf + 1, s.streamBytesLeft - 1);
                s.streamBytesLeft--;
            }
        }
    }
    return false;
}

int16_t AudioEngine::applySpeakerDsp(int32_t mixSample) {
    // 1. Filtro Passa-Alto 1° ordine a ~180 Hz (protegge gli speaker compatti da escursione sub-bassi)
    float in = (float)mixSample;
    float hpOut = 0.975f * (_hpPrevOut + in - _hpPrevIn);
    _hpPrevIn = in;
    _hpPrevOut = hpOut;

    // 2. Presence / Treble Boost (+4.5 dB sulle medio-alte per intelligibilità all'aperto)
    _lpPrevOut += 0.18f * (hpOut - _lpPrevOut);
    float highComponent = hpOut - _lpPrevOut;
    float boosted = (hpOut + highComponent * 0.65f) * _masterGain;

    // 3. Soft-Clipping Limiter (saturazione morbida senza mai troncare bruscamente l'onda)
    if (boosted > 28000.0f) {
        boosted = 28000.0f + (boosted - 28000.0f) * 0.15f;
        if (boosted > 32700.0f) boosted = 32700.0f;
    } else if (boosted < -28000.0f) {
        boosted = -28000.0f + (boosted + 28000.0f) * 0.15f;
        if (boosted < -32700.0f) boosted = -32700.0f;
    }
    return (int16_t)boosted;
}

void AudioEngine::loop() {
    if (!_initialized) return;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

    // 1. Riempi i ring buffer PCM dei due canali MP3 se hanno spazio
    for (uint8_t slot = 0; slot < 2; slot++) {
        while (_mp3Slots[slot].active && _mp3Slots[slot].ringCount < (PCM_RING_SAMPLES - 1200)) {
            if (!decodeNextMp3Frame(slot, nullptr)) break;
        }
    }

    bool anyActive = _mp3Slots[0].active || (_mp3Slots[0].ringCount > 0) ||
                     _mp3Slots[1].active || (_mp3Slots[1].ringCount > 0) ||
                     _sweepDds.active    || _beepDds.active;

    if (!anyActive) {
        if (_ampEnabled && (millis() - _lastActiveAudioMs >= 300)) {
            i2s_zero_dma_buffer(I2S_NUM_0);
            digitalWrite(_pinAudioEn, LOW);
            _ampEnabled = false;
        }
        xSemaphoreGive(_mutex);
        return;
    }

    if (!_ampEnabled) {
        digitalWrite(_pinAudioEn, HIGH);
        _ampEnabled = true;
    }
    _lastActiveAudioMs = millis();

    // Auto-Ducking: se il canale VOICE sta parlando, attenua SFX e Sweep per far emergere la voce
    bool voiceSpeaking = (_mp3Slots[AUDIO_CH_VOICE].ringCount > 0);
    float duckFactor = voiceSpeaking ? 0.50f : 1.0f;

    int16_t i2sOut[MIX_CHUNK_FRAMES * 2];

    for (size_t i = 0; i < MIX_CHUNK_FRAMES; i++) {
        int32_t sum = 0;

        // Canale 0: Voce MP3
        if (_mp3Slots[0].ringCount > 0) {
            int16_t s0 = _mp3Slots[0].pcmRing[_mp3Slots[0].ringReadIdx];
            _mp3Slots[0].ringReadIdx = (_mp3Slots[0].ringReadIdx + 1) % PCM_RING_SAMPLES;
            _mp3Slots[0].ringCount--;
            sum += (int32_t)(s0 * _mp3Slots[0].gain);
        }

        // Canale 1: Effetti MP3
        if (_mp3Slots[1].ringCount > 0) {
            int16_t s1 = _mp3Slots[1].pcmRing[_mp3Slots[1].ringReadIdx];
            _mp3Slots[1].ringReadIdx = (_mp3Slots[1].ringReadIdx + 1) % PCM_RING_SAMPLES;
            _mp3Slots[1].ringCount--;
            sum += (int32_t)(s1 * _mp3Slots[1].gain * duckFactor);
        }

        // Canale 2: Sweep DDS (Hold Button)
        if (_sweepDds.active) {
            sum += (int32_t)(sampleDds(_sweepDds) * duckFactor);
        }

        // Canale 3: Beep DDS (Keypad, RFID, Countdown)
        if (_beepDds.active) {
            sum += sampleDds(_beepDds);
        }

        int16_t finalSample = applySpeakerDsp(sum);
        i2sOut[i * 2]     = finalSample;
        i2sOut[i * 2 + 1] = finalSample;
    }

    xSemaphoreGive(_mutex);

    size_t bytesWritten = 0;
    i2s_write(I2S_NUM_0, i2sOut, sizeof(i2sOut), &bytesWritten, pdMS_TO_TICKS(50));
}

void AudioEngine::setMasterVolume(uint8_t vol) {
    if (vol > 100) vol = 100;
    _masterGain = (float)vol / 100.0f;
}

bool AudioEngine::isPlaying() const {
    return _mp3Slots[0].active || (_mp3Slots[0].ringCount > 0) ||
           _mp3Slots[1].active || (_mp3Slots[1].ringCount > 0);
}

bool AudioEngine::isChannelPlaying(uint8_t slot) const {
    if (slot >= 2) return false;
    return _mp3Slots[slot].active || (_mp3Slots[slot].ringCount > 0);
}