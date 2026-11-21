/* PSP platform layer on the PSP SDK: start-up, files, time, input, sound and saved games.
 * The graphics are in psp_gu.c.
 *
 * Sound: the PSP plays what a program hands it in blocks of samples, so the sounds are
 * mixed here, in a thread of their own that feeds one output channel. Sound effects are
 * 16-bit samples in the package; music is a WAV file that is read while it plays.
 *
 * Leaving the program: the HOME button, through the system's own question. */
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pspaudio.h>
#include <pspctrl.h>
#include <pspkernel.h>
#include <psputility.h>

#include "../../core/rr_platform.h"
#include "psp_gfx.h"

PSP_MODULE_INFO("RetroRuntime", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(512);
/* Everything but 2 MB, which stay for the stacks of the threads and the system's own needs. */
PSP_HEAP_SIZE_KB(-2048);

#define MAX_SOUNDS   64
#define MIX_RATE     44100
#define MIX_FRAMES   1024 /* per block handed to the system: 23 ms */
#define MUSIC_FRAMES 2048 /* read from the file at a time */

typedef struct {
    const int16_t *samples; /* NULL: free slot */
    uint32_t       frames;
    uint32_t       rate;
    uint8_t        channels;
    bool           playing, loop;
    uint64_t       position; /* in frames, 32.32 fixed point */
    uint64_t       step;     /* frames of the sound per frame that is mixed */
    int32_t        left, right; /* volume, 256 = as recorded */
} Voice;

typedef struct {
    int      file; /* -1: none */
    uint32_t data_start, data_bytes, data_left;
    uint32_t rate;
    uint8_t  channels;
    bool     loop;
    int32_t  volume;
    int16_t  buffer[MUSIC_FRAMES * 2];
    uint32_t buffered;  /* frames in the buffer */
    uint64_t position;  /* in the buffer, 32.32 */
} Music;

static volatile bool g_quit;
static uint32_t      g_last_frame_us;

static Voice  g_voices[MAX_SOUNDS];
static Music  g_music = { .file = -1 };
static SceUID g_audio_lock = -1, g_audio_thread = -1;
static int    g_audio_channel = -1;
static volatile bool g_audio_run;

uint8_t rr_plat_id(void)
{
    return RR_PLATFORM_PSP;
}

/* --- the system's callbacks: it asks the program to end through one ------------------ */

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    g_quit = true;
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    sceKernelRegisterExitCallback(sceKernelCreateCallback("exit", exit_callback, NULL));
    sceKernelSleepThreadCB();
    return 0;
}

/* --- sound --------------------------------------------------------------------------- */

static void audio_lock(void)
{
    sceKernelWaitSema(g_audio_lock, 1, NULL);
}

static void audio_unlock(void)
{
    sceKernelSignalSema(g_audio_lock, 1);
}

/* Fills the music buffer from the file. False at the end of a piece that does not loop. */
static bool music_fill(Music *m)
{
    const uint32_t frame_bytes = 2u * m->channels;
    uint32_t       want = MUSIC_FRAMES * frame_bytes, got = 0;

    while (got < want) {
        uint32_t chunk = want - got;
        int      n;

        if (m->data_left == 0) {
            if (!m->loop || m->data_bytes < frame_bytes) {
                break;
            }
            lseek(m->file, (off_t)m->data_start, SEEK_SET);
            m->data_left = m->data_bytes;
        }
        if (chunk > m->data_left) {
            chunk = m->data_left;
        }
        n = read(m->file, (uint8_t *)m->buffer + got, chunk);
        if (n <= 0) {
            m->data_left = 0;
            if (!m->loop) {
                break;
            }
            m->loop = false; /* a file that cannot be read is not tried again and again */
            continue;
        }
        got += (uint32_t)n;
        m->data_left -= (uint32_t)n;
    }
    m->buffered = got / frame_bytes;
    m->position = 0;
    return m->buffered != 0;
}

static void music_close(Music *m)
{
    if (m->file >= 0) {
        close(m->file);
        m->file = -1;
    }
}

static void mix(int32_t *out)
{
    uint32_t i, k;

    memset(out, 0, sizeof(int32_t) * MIX_FRAMES * 2);
    for (i = 0; i < MAX_SOUNDS; i++) {
        Voice *v = &g_voices[i];

        if (v->samples == NULL || !v->playing) {
            continue;
        }
        for (k = 0; k < MIX_FRAMES; k++) {
            uint32_t frame = (uint32_t)(v->position >> 32);
            int32_t  l, r;

            if (frame >= v->frames) {
                if (!v->loop) {
                    v->playing = false;
                    break;
                }
                v->position -= (uint64_t)v->frames << 32;
                frame = (uint32_t)(v->position >> 32) % v->frames;
            }
            if (v->channels == 2) {
                l = v->samples[frame * 2];
                r = v->samples[frame * 2 + 1];
            } else {
                l = r = v->samples[frame];
            }
            out[k * 2] += (l * v->left) >> 8;
            out[k * 2 + 1] += (r * v->right) >> 8;
            v->position += v->step;
        }
    }
    if (g_music.file >= 0) {
        Music         *m = &g_music;
        const uint64_t step = ((uint64_t)m->rate << 32) / MIX_RATE;

        for (k = 0; k < MIX_FRAMES; k++) {
            uint32_t frame = (uint32_t)(m->position >> 32);

            if (frame >= m->buffered) {
                if (!music_fill(m)) {
                    music_close(m);
                    break;
                }
                frame = 0;
            }
            if (m->channels == 2) {
                out[k * 2] += (m->buffer[frame * 2] * m->volume) >> 8;
                out[k * 2 + 1] += (m->buffer[frame * 2 + 1] * m->volume) >> 8;
            } else {
                const int32_t s = (m->buffer[frame] * m->volume) >> 8;
                out[k * 2] += s;
                out[k * 2 + 1] += s;
            }
            m->position += step;
        }
    }
}

static int audio_thread(SceSize args, void *argp)
{
    static int32_t mixed[MIX_FRAMES * 2];
    static int16_t block[MIX_FRAMES * 2] __attribute__((aligned(64)));
    uint32_t       i;

    (void)args;
    (void)argp;
    while (g_audio_run) {
        audio_lock();
        mix(mixed);
        audio_unlock();
        for (i = 0; i < MIX_FRAMES * 2; i++) {
            const int32_t s = mixed[i];
            block[i] = (int16_t)(s < -32768 ? -32768 : (s > 32767 ? 32767 : s));
        }
        /* Waits until the block before this one is played. */
        sceAudioOutputPannedBlocking(g_audio_channel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, block);
    }
    return 0;
}

static void audio_init(void)
{
    g_audio_lock = sceKernelCreateSema("rr_audio", 0, 1, 1, NULL);
    g_audio_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, MIX_FRAMES, PSP_AUDIO_FORMAT_STEREO);
    if (g_audio_lock < 0 || g_audio_channel < 0) {
        printf("retro: no sound output, the game stays silent\n");
        return;
    }
    g_audio_run = true;
    /* Above the main thread, so that a long frame does not make the sound stutter. */
    g_audio_thread = sceKernelCreateThread("rr_audio", audio_thread, 0x12, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (g_audio_thread >= 0) {
        sceKernelStartThread(g_audio_thread, 0, NULL);
    }
}

static void audio_shutdown(void)
{
    if (g_audio_thread >= 0) {
        g_audio_run = false;
        sceKernelWaitThreadEnd(g_audio_thread, NULL);
        sceKernelDeleteThread(g_audio_thread);
        g_audio_thread = -1;
    }
    if (g_audio_channel >= 0) {
        sceAudioChRelease(g_audio_channel);
        g_audio_channel = -1;
    }
    music_close(&g_music);
}

uint32_t rr_snd_load(const RRPkgSound *sound)
{
    uint32_t i;

    if (sound->format != RR_SOUND_PCM16 || sound->sample_count == 0) {
        return RR_NONE32;
    }
    for (i = 0; i < MAX_SOUNDS; i++) {
        if (g_voices[i].samples == NULL) {
            Voice *v = &g_voices[i];
            audio_lock();
            memset(v, 0, sizeof(*v));
            v->frames = sound->sample_count;
            v->rate = sound->sample_rate;
            v->channels = sound->channels;
            v->samples = (const int16_t *)(const void *)(sound + 1); /* stays in the package */
            audio_unlock();
            return i;
        }
    }
    printf("retro: more than %d sounds, one is missing\n", MAX_SOUNDS);
    return RR_NONE32;
}

void rr_snd_unload(uint32_t handle)
{
    if (handle < MAX_SOUNDS) {
        audio_lock();
        g_voices[handle].playing = false;
        g_voices[handle].samples = NULL;
        audio_unlock();
    }
}

/* Volume, place between the speakers and speed of a voice. The caller holds the lock. */
static void voice_set(Voice *v, float volume, float pan, float pitch)
{
    const float vol = volume < 0.0f ? 0.0f : (volume > 4.0f ? 4.0f : volume);
    const float p = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);

    v->left = (int32_t)(vol * (p > 0.0f ? 1.0f - p : 1.0f) * 256.0f);
    v->right = (int32_t)(vol * (p < 0.0f ? 1.0f + p : 1.0f) * 256.0f);
    v->step = (uint64_t)((double)v->rate * (double)(pitch > 0.01f ? pitch : 0.01f) *
                         (4294967296.0 / (double)MIX_RATE));
}

void rr_snd_play(uint32_t handle, float volume, float pan, float pitch, bool loop)
{
    if (handle < MAX_SOUNDS && g_voices[handle].samples != NULL) {
        Voice *v = &g_voices[handle];
        audio_lock();
        voice_set(v, volume, pan, pitch);
        v->position = 0;
        v->loop = loop;
        v->playing = true;
        audio_unlock();
    }
}

void rr_snd_set(uint32_t handle, float volume, float pan, float pitch)
{
    if (handle < MAX_SOUNDS && g_voices[handle].samples != NULL) {
        audio_lock();
        voice_set(&g_voices[handle], volume, pan, pitch);
        audio_unlock();
    }
}

void rr_snd_stop(uint32_t handle)
{
    if (handle < MAX_SOUNDS) {
        audio_lock();
        g_voices[handle].playing = false;
        audio_unlock();
    }
}

/* Reads the header of a WAV file up to its samples. */
static bool wav_open(Music *m, const char *path)
{
    uint8_t  head[12], chunk[8], format[16];
    uint32_t size, at = 12;
    bool     have_format = false;

    m->file = open(path, O_RDONLY);
    if (m->file < 0) {
        return false;
    }
    if (read(m->file, head, 12) != 12 || memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "WAVE", 4) != 0) {
        return false;
    }
    while (read(m->file, chunk, 8) == 8) {
        size = (uint32_t)chunk[4] | ((uint32_t)chunk[5] << 8) | ((uint32_t)chunk[6] << 16) | ((uint32_t)chunk[7] << 24);
        at += 8;
        if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            if (read(m->file, format, 16) != 16) {
                return false;
            }
            m->channels = format[2];
            m->rate = (uint32_t)format[4] | ((uint32_t)format[5] << 8) | ((uint32_t)format[6] << 16);
            /* 16-bit PCM with one or two channels is all the mixer takes. */
            have_format = format[0] == 1 && format[1] == 0 && format[14] == 16 &&
                          (m->channels == 1 || m->channels == 2) && m->rate != 0;
        } else if (memcmp(chunk, "data", 4) == 0) {
            m->data_start = at;
            m->data_bytes = size;
            m->data_left = size;
            return have_format;
        }
        at += size + (size & 1u);
        lseek(m->file, (off_t)at, SEEK_SET);
    }
    return false;
}

bool rr_music_play(const char *path, float volume, bool loop)
{
    Music *m = &g_music;
    bool   ok;

    if (g_audio_thread < 0) {
        return false;
    }
    audio_lock();
    music_close(m);
    ok = wav_open(m, path);
    if (ok) {
        m->loop = loop;
        m->volume = (int32_t)((volume < 0.0f ? 0.0f : volume) * 256.0f);
        m->buffered = 0;
        m->position = 0;
    } else {
        music_close(m);
    }
    audio_unlock();
    if (!ok) {
        printf("retro: cannot play music '%s'\n", path);
    }
    return ok;
}

void rr_music_set_volume(float volume)
{
    audio_lock();
    g_music.volume = (int32_t)((volume < 0.0f ? 0.0f : volume) * 256.0f);
    audio_unlock();
}

void rr_music_stop(void)
{
    if (g_audio_lock >= 0) {
        audio_lock();
        music_close(&g_music);
        audio_unlock();
    }
}

/* --- start-up, time, files ----------------------------------------------------------- */

void rr_plat_boot(void)
{
}

bool rr_plat_init(const char *title)
{
    SceUID thread;

    (void)title;
#ifdef RR_LOG_FILE
    /* Test builds: what the program prints goes into a file next to it, line by line, so
     * that it can be read while an emulator runs the program. Errors go into a second
     * file, as two streams cannot share one. */
    if (freopen(RR_LOG_FILE ".log", "w", stdout) != NULL) {
        setvbuf(stdout, NULL, _IOLBF, 256);
    }
    if (freopen(RR_LOG_FILE "_errors.log", "w", stderr) != NULL) {
        setvbuf(stderr, NULL, _IONBF, 0);
    }
#endif
    thread = sceKernelCreateThread("rr_callbacks", callback_thread, 0x11, 0x1000, PSP_THREAD_ATTR_USER, NULL);
    if (thread >= 0) {
        sceKernelStartThread(thread, 0, NULL);
    }
    if (!psp_gfx_init()) {
        return false;
    }
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    audio_init();
    g_last_frame_us = sceKernelGetSystemTimeLow();
    return true;
}

void rr_plat_shutdown(void)
{
    audio_shutdown();
    psp_gfx_shutdown();
    fflush(stdout);
}

bool rr_plat_should_quit(void)
{
    return g_quit;
}

uint32_t rr_plat_time_us(void)
{
    return sceKernelGetSystemTimeLow();
}

float rr_plat_frame_time(void)
{
    const uint32_t now = sceKernelGetSystemTimeLow();
    const float    dt = (float)(now - g_last_frame_us) * 0.000001f;
    g_last_frame_us = now;
    return dt;
}

/* The graphics chip reads meshes straight from this block, and from memory, not from the
 * processor's cache: what was read into the block is written back before it is used. */
void *rr_plat_read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    long  length;
    void *block;

    if (f == NULL) {
        rr_set_error("cannot open '%s'", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (length <= 0) {
        fclose(f);
        rr_set_error("'%s' is empty or unreadable", path);
        return NULL;
    }
    block = memalign(64, ((size_t)length + 63u) & ~(size_t)63u);
    if (block == NULL || fread(block, 1, (size_t)length, f) != (size_t)length) {
        free(block);
        fclose(f);
        rr_set_error("cannot read %ld bytes from '%s'", length, path);
        return NULL;
    }
    fclose(f);
    sceKernelDcacheWritebackRange(block, (unsigned int)length);
    *size = (uint32_t)length;
    return block;
}

bool rr_plat_screenshot(const char *path)
{
    (void)path;
    return false;
}

/* --- input --------------------------------------------------------------------------- */

/* Binding codes (see psp_baker.gd): buttons are PSP_CTRL_* masks, axes are 0 stick X,
 * 1 stick Y, 2 the L button, 3 the R button. */
float rr_plat_binding_value(const RRPkgBinding *binding)
{
    SceCtrlData pad;
    float       v = 0.0f;

    if (sceCtrlPeekBufferPositive(&pad, 1) < 1) {
        return 0.0f;
    }
    if (binding->kind == RR_BIND_PAD_BUTTON) {
        return (pad.Buttons & binding->code) != 0 ? 1.0f : 0.0f;
    }
    if (binding->kind == RR_BIND_PAD_AXIS) {
        switch (binding->code) {
        case 0:  v = ((float)pad.Lx - 127.5f) * (1.0f / 127.5f) * (float)binding->sign; break;
        case 1:  v = ((float)pad.Ly - 127.5f) * (1.0f / 127.5f) * (float)binding->sign; break;
        case 2:  v = (pad.Buttons & PSP_CTRL_LTRIGGER) != 0 ? 1.0f : 0.0f; break;
        default: v = (pad.Buttons & PSP_CTRL_RTRIGGER) != 0 ? 1.0f : 0.0f; break;
        }
    }
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void rr_plat_vibrate(float weak, float strong)
{
    (void)weak;
    (void)strong;
}

/* --- saved games: one file per block, next to the program ----------------------------- */

bool rr_plat_save_write(const char *name, const void *data, uint32_t size)
{
    char  path[64];
    FILE *f;
    bool  ok;

    snprintf(path, sizeof(path), "%s.sav", name);
    f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

bool rr_plat_save_read(const char *name, void *data, uint32_t size)
{
    char  path[64];
    FILE *f;
    long  length;
    bool  ok;

    snprintf(path, sizeof(path), "%s.sav", name);
    f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    ok = length == (long)size && fread(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

/* --- language ------------------------------------------------------------------------ */

const char *rr_plat_locale(void)
{
    /* The language the console is set to in its own menu. */
    static const char *const names[] = { "ja", "en", "fr", "es", "de", "it", "nl", "pt", "ru", "ko", "zh_TW", "zh_CN" };
    int                      language = -1;

    if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &language) != 0 || language < 0 ||
        language >= (int)(sizeof(names) / sizeof(names[0]))) {
        return NULL;
    }
    return names[language];
}
