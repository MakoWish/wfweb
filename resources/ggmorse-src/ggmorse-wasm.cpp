#include <emscripten.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include "ggmorse/ggmorse.h"

static GGMorse* g_ggMorse = nullptr;
static std::vector<float> g_buffer;
static std::string g_newText;
static float g_sampleRate = 3200.0f;

// ---- Prosigns ------------------------------------------------------------
// ggmorse's built-in alphabet maps every pattern to a single character, so the
// run-together prosigns either land on punctuation (".-.-." -> '+') or are
// missing entirely and decode as '?'.  setCharacter() re-keys those patterns
// onto sentinel bytes, which ggmorse_decode() then expands to <AR>, <SK>, ...
// Patterns are ggmorse's own notation: 0 = dot, 1 = dash.
struct Prosign {
    const char*   pattern;
    std::uint8_t  sentinel;
    const char*   text;
};

static const Prosign kProsigns[] = {
    { "01010",    0x01, "<AR>" },   // . - . - .      end of message
    { "01000",    0x02, "<AS>" },   // . - . . .      wait
    { "1000101",  0x03, "<BK>" },   // - . . . - . -  break
    { "10001",    0x04, "<BT>" },   // - . . . -      separator / new paragraph
    { "10101",    0x05, "<CT>" },   // - . - . -      attention
    { "00000000", 0x06, "<HH>" },   // . . . . . . . . correction
    { "10110",    0x07, "<KN>" },   // - . - - .      go ahead, named station only
    { "000101",   0x08, "<SK>" },   // . . . - . -    end of contact
    { "00010",    0x09, "<SN>" },   // . . . - .      understood
};

static const char* prosignText(std::uint8_t byte) {
    for (const auto & p : kProsigns) {
        if (p.sentinel == byte) return p.text;
    }
    return nullptr;
}

static void createInstance(float sampleRate) {
    delete g_ggMorse;

    GGMorse::Parameters params;
    params.sampleRateInp = sampleRate;
    params.sampleRateOut = sampleRate;
    params.samplesPerFrame = GGMorse::kDefaultSamplesPerFrame;
    params.sampleFormatInp = GGMORSE_SAMPLE_FORMAT_F32;
    params.sampleFormatOut = GGMORSE_SAMPLE_FORMAT_F32;

    g_ggMorse = new GGMorse(params);

    auto paramsDecode = GGMorse::getDefaultParametersDecode();
    paramsDecode.frequency_hz = -1.0f;          // auto-detect pitch
    paramsDecode.speed_wpm = -1.0f;             // auto-detect speed
    paramsDecode.frequencyRangeMin_hz = 200.0f;
    paramsDecode.frequencyRangeMax_hz = 1200.0f;
    g_ggMorse->setParametersDecode(paramsDecode);

    for (const auto & p : kProsigns) {
        g_ggMorse->setCharacter(p.pattern, (char) p.sentinel);
    }
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void ggmorse_init(float sampleRate) {
    g_sampleRate = sampleRate;
    g_buffer.clear();
    g_newText.clear();
    createInstance(sampleRate);
}

EMSCRIPTEN_KEEPALIVE
void ggmorse_queue(float* samples, int n) {
    if (!g_ggMorse || !samples || n <= 0) return;
    g_buffer.insert(g_buffer.end(), samples, samples + n);
}

EMSCRIPTEN_KEEPALIVE
int ggmorse_decode() {
    if (!g_ggMorse || g_buffer.empty()) return 0;

    const uint32_t sampleSizeBytes = sizeof(float);
    size_t readOffset = 0;
    const size_t totalSamples = g_buffer.size();

    GGMorse::CBWaveformInp cb = [&](void* data, uint32_t nMaxBytes) -> uint32_t {
        const uint32_t nMaxSamples = nMaxBytes / sampleSizeBytes;
        const size_t remaining = totalSamples - readOffset;

        // Must provide exactly a full frame or 0 — never a partial frame.
        // Remaining samples below one frame are kept for the next decode() call.
        if (remaining < nMaxSamples) return 0;

        memcpy(data, g_buffer.data() + readOffset, nMaxBytes);
        readOffset += nMaxSamples;
        return nMaxBytes;
    };

    g_ggMorse->decode(cb);

    // Keep any unconsumed tail samples for the next decode() call
    if (readOffset > 0 && readOffset < totalSamples) {
        g_buffer.erase(g_buffer.begin(), g_buffer.begin() + readOffset);
    } else {
        g_buffer.clear();
    }

    // Collect newly decoded text
    GGMorse::TxRx rxData;
    int nNew = g_ggMorse->takeRxData(rxData);
    if (nNew > 0) {
        for (auto byte : rxData) {
            if (const char* text = prosignText(byte)) {
                g_newText += text;
            } else if (byte >= 32 && byte < 128) {
                g_newText += (char)byte;
            }
        }
    }

    return nNew;
}

EMSCRIPTEN_KEEPALIVE
const char* ggmorse_get_text() {
    static std::string result;
    result = g_newText;
    g_newText.clear();
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE
float ggmorse_get_frequency() {
    if (!g_ggMorse) return 0.0f;
    return g_ggMorse->getStatistics().estimatedPitch_Hz;
}

EMSCRIPTEN_KEEPALIVE
float ggmorse_get_speed() {
    if (!g_ggMorse) return 0.0f;
    return g_ggMorse->getStatistics().estimatedSpeed_wpm;
}

EMSCRIPTEN_KEEPALIVE
void ggmorse_reset() {
    g_buffer.clear();
    g_newText.clear();
    createInstance(g_sampleRate);
}

} // extern "C"
