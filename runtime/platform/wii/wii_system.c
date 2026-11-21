/* Wii and GameCube platform layer on libogc: start-up, files, time, input, sound and saved
 * games. The graphics are in wii_gx.c. One source for both consoles: what only the Wii
 * has (the Wii Remote, the console's settings) is inside HW_RVL.
 *
 * Files are on the SD card, through libfat. Sound effects play on the voices of libogc's
 * mixer (ASND); music is a WAV file that a thread reads while it plays.
 *
 * Leaving the program: HOME on the Wii Remote, or the console's reset button. */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <asndlib.h>
#include <fat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#ifdef HW_RVL
#include <wiiuse/wpad.h>
#endif

#include "../../core/rr_platform.h"
#include "wii_gfx.h"

#define MAX_SOUNDS   64
#define MUSIC_BYTES  (32 * 1024) /* of one of the two blocks the music plays from */
#define NO_VOICE     (-1)

/* The buttons of a GameCube pad, which the binding codes are (see wii_baker.gd). */
#define BTN_LEFT  0x0001u
#define BTN_RIGHT 0x0002u
#define BTN_DOWN  0x0004u
#define BTN_UP    0x0008u
#define BTN_Z     0x0010u
#define BTN_R     0x0020u
#define BTN_L     0x0040u
#define BTN_A     0x0100u
#define BTN_B     0x0200u
#define BTN_X     0x0400u
#define BTN_Y     0x0800u
#define BTN_START 0x1000u

typedef struct {
    void    *samples; /* NULL: free slot */
    uint32_t bytes;
    uint32_t rate;
    uint8_t  channels;
    int      voice; /* the mixer's voice it plays on, NO_VOICE if it was never played */
} Sound;

typedef struct {
    FILE    *file;
    uint32_t data_start, data_bytes, data_left;
    uint32_t rate;
    uint8_t  channels;
    bool     loop;
    int      volume; /* 0..255 */
    int      voice;
    int      next;   /* which of the two blocks is filled next */
    void    *blocks[2];
} Music;

static volatile bool g_quit;
static u64           g_last_frame;

static Sound g_sounds[MAX_SOUNDS];
static int   g_voice_sound[MAX_SND_VOICES]; /* the sound a voice was last given to */
static Music g_music = { .voice = NO_VOICE };
static mutex_t g_music_lock;
static lwp_t   g_music_thread = LWP_THREAD_NULL;
static volatile bool g_music_run;

/* The pads, read once per frame. */
static uint32_t g_buttons;
static float    g_stick_x, g_stick_y, g_trigger_l, g_trigger_r;
static uint32_t g_frame, g_scanned_frame = 0xFFFFFFFFu;

uint8_t rr_plat_id(void)
{
    return RR_PLATFORM_WII;
}

static void on_reset(u32 irq, void *context)
{
    (void)irq;
    (void)context;
    g_quit = true;
}

#ifdef HW_RVL
static void on_power(void)
{
    g_quit = true;
}
#endif

void rr_plat_boot(void)
{
    /* The card the packages are on. Without one the first file that is opened fails with
     * its own message. */
    fatInitDefault();
}

/* --- music --------------------------------------------------------------------------- */

/* Fills a block from the file. Returns the bytes that are in it, 0 at the end of a piece
 * that does not loop. */
static uint32_t music_fill(Music *m, void *block)
{
    const uint32_t frame_bytes = 2u * m->channels;
    uint32_t       got = 0;

    while (got < MUSIC_BYTES) {
        uint32_t chunk = MUSIC_BYTES - got;
        size_t   n;

        if (m->data_left == 0) {
            if (!m->loop || m->data_bytes < frame_bytes) {
                break;
            }
            fseek(m->file, (long)m->data_start, SEEK_SET);
            m->data_left = m->data_bytes;
        }
        if (chunk > m->data_left) {
            chunk = m->data_left;
        }
        n = fread((uint8_t *)block + got, 1, chunk, m->file);
        if (n == 0) {
            m->data_left = 0;
            m->loop = false; /* a file that cannot be read is not tried again and again */
            break;
        }
        got += (uint32_t)n;
        m->data_left -= (uint32_t)n;
    }
    got -= got % frame_bytes;
    /* The mixer takes whole blocks of 32 bytes. */
    if ((got & 31u) != 0) {
        memset((uint8_t *)block + got, 0, 32u - (got & 31u));
        got = (got + 31u) & ~31u;
    }
    DCFlushRange(block, got);
    return got;
}

static void music_close(Music *m)
{
    if (m->voice != NO_VOICE) {
        ASND_StopVoice(m->voice);
        m->voice = NO_VOICE;
    }
    if (m->file != NULL) {
        fclose(m->file);
        m->file = NULL;
    }
}

/* Keeps the music's voice fed: whenever the mixer has room for the next block, the block
 * that is not playing is filled from the file and handed over. */
static void *music_thread(void *arg)
{
    (void)arg;
    while (g_music_run) {
        Music *m = &g_music;

        LWP_MutexLock(g_music_lock);
        if (m->file != NULL) {
            /* The WAV file's samples are little-endian, which the mixer can be told. */
            const int format = m->channels == 2 ? VOICE_STEREO_16BIT_LE : VOICE_MONO_16BIT_LE;

            if (m->voice == NO_VOICE || ASND_StatusVoice(m->voice) == SND_UNUSED) {
                const uint32_t bytes = music_fill(m, m->blocks[m->next]);
                if (m->voice == NO_VOICE) {
                    m->voice = ASND_GetFirstUnusedVoice();
                }
                if (bytes == 0 || m->voice < 0) {
                    m->voice = m->voice < 0 ? NO_VOICE : m->voice;
                    music_close(m);
                } else {
                    g_voice_sound[m->voice] = -1;
                    ASND_SetVoice(m->voice, format, (s32)m->rate, 0, m->blocks[m->next], (s32)bytes,
                                  m->volume, m->volume, NULL);
                    m->next ^= 1;
                }
            } else if (ASND_TestVoiceBufferReady(m->voice) == 1) {
                const uint32_t bytes = music_fill(m, m->blocks[m->next]);
                if (bytes == 0) {
                    /* What was handed over plays to its end. */
                    fclose(m->file);
                    m->file = NULL;
                    m->voice = NO_VOICE;
                } else if (ASND_AddVoice(m->voice, m->blocks[m->next], (s32)bytes) == SND_OK) {
                    m->next ^= 1;
                }
            }
        }
        LWP_MutexUnlock(g_music_lock);
        usleep(10000);
    }
    return NULL;
}

/* Reads the header of a WAV file up to its samples. */
static bool wav_open(Music *m, const char *path)
{
    uint8_t  head[12], chunk[8], format[16];
    uint32_t size, at = 12;
    bool     have_format = false;

    m->file = fopen(path, "rb");
    if (m->file == NULL) {
        return false;
    }
    if (fread(head, 1, 12, m->file) != 12 || memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "WAVE", 4) != 0) {
        return false;
    }
    while (fread(chunk, 1, 8, m->file) == 8) {
        size = (uint32_t)chunk[4] | ((uint32_t)chunk[5] << 8) | ((uint32_t)chunk[6] << 16) | ((uint32_t)chunk[7] << 24);
        at += 8;
        if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            if (fread(format, 1, 16, m->file) != 16) {
                return false;
            }
            m->channels = format[2];
            m->rate = (uint32_t)format[4] | ((uint32_t)format[5] << 8) | ((uint32_t)format[6] << 16);
            /* 16-bit PCM with one or two channels is all that is played. */
            have_format = format[0] == 1 && format[1] == 0 && format[14] == 16 &&
                          (m->channels == 1 || m->channels == 2) && m->rate != 0;
        } else if (memcmp(chunk, "data", 4) == 0) {
            m->data_start = at;
            m->data_bytes = size;
            m->data_left = size;
            return have_format;
        }
        at += size + (size & 1u);
        fseek(m->file, (long)at, SEEK_SET);
    }
    return false;
}

static int mixer_volume(float volume)
{
    const int v = (int)(volume * 255.0f);
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

bool rr_music_play(const char *path, float volume, bool loop)
{
    Music *m = &g_music;
    bool   ok;

    if (g_music_thread == LWP_THREAD_NULL) {
        return false;
    }
    LWP_MutexLock(g_music_lock);
    music_close(m);
    ok = wav_open(m, path);
    if (ok) {
        m->loop = loop;
        m->volume = mixer_volume(volume);
        m->next = 0;
    } else {
        music_close(m);
    }
    LWP_MutexUnlock(g_music_lock);
    if (!ok) {
        printf("retro: cannot play music '%s'\n", path);
    }
    return ok;
}

void rr_music_set_volume(float volume)
{
    if (g_music_thread == LWP_THREAD_NULL) {
        return;
    }
    LWP_MutexLock(g_music_lock);
    g_music.volume = mixer_volume(volume);
    if (g_music.voice != NO_VOICE) {
        ASND_ChangeVolumeVoice(g_music.voice, g_music.volume, g_music.volume);
    }
    LWP_MutexUnlock(g_music_lock);
}

void rr_music_stop(void)
{
    if (g_music_thread == LWP_THREAD_NULL) {
        return;
    }
    LWP_MutexLock(g_music_lock);
    music_close(&g_music);
    LWP_MutexUnlock(g_music_lock);
}

/* --- sound effects ------------------------------------------------------------------- */

uint32_t rr_snd_load(const RRPkgSound *sound)
{
    uint32_t i;

    if (sound->format != RR_SOUND_PCM16 || sound->data_size == 0) {
        return RR_NONE32;
    }
    for (i = 0; i < MAX_SOUNDS; i++) {
        Sound *s = &g_sounds[i];
        if (s->samples == NULL) {
            /* The mixer wants its samples at an address that is a multiple of 32 and in
             * whole blocks of 32 bytes, which the package does not give. */
            const uint32_t bytes = (sound->data_size + 31u) & ~31u;

            s->samples = memalign(32, bytes);
            if (s->samples == NULL) {
                return RR_NONE32;
            }
            memset(s->samples, 0, bytes);
            memcpy(s->samples, sound + 1, sound->data_size);
            DCFlushRange(s->samples, bytes);
            s->bytes = bytes;
            s->rate = sound->sample_rate;
            s->channels = sound->channels;
            s->voice = NO_VOICE;
            return i;
        }
    }
    printf("retro: more than %d sounds, one is missing\n", MAX_SOUNDS);
    return RR_NONE32;
}

/* The voice a sound plays on, NO_VOICE if it does not play (any more). */
static int voice_of(uint32_t handle)
{
    const int voice = handle < MAX_SOUNDS && g_sounds[handle].samples != NULL ? g_sounds[handle].voice
                                                                              : NO_VOICE;

    if (voice == NO_VOICE || g_voice_sound[voice] != (int)handle || ASND_StatusVoice(voice) == SND_UNUSED) {
        return NO_VOICE;
    }
    return voice;
}

void rr_snd_stop(uint32_t handle)
{
    const int voice = voice_of(handle);

    if (voice != NO_VOICE) {
        ASND_StopVoice(voice);
    }
}

void rr_snd_unload(uint32_t handle)
{
    if (handle < MAX_SOUNDS && g_sounds[handle].samples != NULL) {
        rr_snd_stop(handle);
        free(g_sounds[handle].samples);
        g_sounds[handle].samples = NULL;
    }
}

static void volumes(float volume, float pan, int *left, int *right)
{
    const float p = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);

    *left = mixer_volume(volume * (p > 0.0f ? 1.0f - p : 1.0f));
    *right = mixer_volume(volume * (p < 0.0f ? 1.0f + p : 1.0f));
}

static s32 pitch_of(const Sound *s, float pitch)
{
    const float hz = (float)s->rate * pitch;
    return hz < (float)MIN_PITCH ? MIN_PITCH : (hz > (float)MAX_PITCH ? MAX_PITCH : (s32)hz);
}

void rr_snd_play(uint32_t handle, float volume, float pan, float pitch, bool loop)
{
    if (handle < MAX_SOUNDS && g_sounds[handle].samples != NULL) {
        Sound    *s = &g_sounds[handle];
        const int format = s->channels == 2 ? VOICE_STEREO_16BIT : VOICE_MONO_16BIT;
        int       voice = voice_of(handle), left, right;

        /* A sound that still plays starts again on its voice. */
        if (voice == NO_VOICE) {
            voice = ASND_GetFirstUnusedVoice();
            if (voice < 0) {
                return; /* all 16 voices are busy */
            }
        }
        volumes(volume, pan, &left, &right);
        g_voice_sound[voice] = (int)handle;
        s->voice = voice;
        if (loop) {
            ASND_SetInfiniteVoice(voice, format, pitch_of(s, pitch), 0, s->samples, (s32)s->bytes, left, right);
        } else {
            ASND_SetVoice(voice, format, pitch_of(s, pitch), 0, s->samples, (s32)s->bytes, left, right, NULL);
        }
    }
}

void rr_snd_set(uint32_t handle, float volume, float pan, float pitch)
{
    const int voice = voice_of(handle);

    if (voice != NO_VOICE) {
        int left, right;
        volumes(volume, pan, &left, &right);
        ASND_ChangeVolumeVoice(voice, left, right);
        ASND_ChangePitchVoice(voice, pitch_of(&g_sounds[handle], pitch));
    }
}

/* --- start-up, time, files ----------------------------------------------------------- */

bool rr_plat_init(const char *title)
{
    int i;

    (void)title;
#ifdef RR_LOG_REPORT
    /* Test builds: what the program prints goes to the debug output, which an emulator
     * writes into its log. */
    SYS_STDIO_Report(true);
#endif
    if (!wii_gfx_init()) {
        return false;
    }
    PAD_Init();
#ifdef HW_RVL
    WPAD_Init();
    SYS_SetPowerCallback(on_power);
#endif
    SYS_SetResetCallback(on_reset);

    ASND_Init();
    ASND_Pause(0);
    for (i = 0; i < MAX_SND_VOICES; i++) {
        g_voice_sound[i] = -1;
    }
    g_music.blocks[0] = memalign(32, MUSIC_BYTES);
    g_music.blocks[1] = memalign(32, MUSIC_BYTES);
    if (g_music.blocks[0] != NULL && g_music.blocks[1] != NULL &&
        LWP_MutexInit(&g_music_lock, false) == 0) {
        g_music_run = true;
        if (LWP_CreateThread(&g_music_thread, music_thread, NULL, NULL, 16 * 1024, 80) != 0) {
            g_music_thread = LWP_THREAD_NULL;
            g_music_run = false;
        }
    }
    g_last_frame = gettime();
    return true;
}

void rr_plat_shutdown(void)
{
    if (g_music_thread != LWP_THREAD_NULL) {
        g_music_run = false;
        LWP_JoinThread(g_music_thread, NULL);
        g_music_thread = LWP_THREAD_NULL;
        music_close(&g_music);
    }
    ASND_End();
    wii_gfx_shutdown();
}

bool rr_plat_should_quit(void)
{
    return g_quit;
}

uint32_t rr_plat_time_us(void)
{
    return (uint32_t)ticks_to_microsecs(gettime());
}

float rr_plat_frame_time(void)
{
    const u64   now = gettime();
    const float dt = (float)ticks_to_microsecs(now - g_last_frame) * 0.000001f;
    g_last_frame = now;
    return dt;
}

void *rr_plat_read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    long  length;
    void *block;

    g_frame++; /* loading takes long: the pads are read again afterwards */
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
    block = malloc((size_t)length);
    if (block == NULL || fread(block, 1, (size_t)length, f) != (size_t)length) {
        free(block);
        fclose(f);
        rr_set_error("cannot read %ld bytes from '%s'", length, path);
        return NULL;
    }
    fclose(f);
    *size = (uint32_t)length;
    return block;
}

bool rr_plat_screenshot(const char *path)
{
    (void)path;
    return false;
}

/* --- input --------------------------------------------------------------------------- */

static float clamp1(float v)
{
    return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

#ifdef HW_RVL
/* A stick of something that is plugged into the Wii Remote: how far it is pushed (0..1)
 * and where to, in degrees clockwise from straight up. */
static void add_stick(const joystick_t *js)
{
    const float angle = js->ang * (3.14159265f / 180.0f);

    if (js->mag > 0.0f && js->mag == js->mag) {
        g_stick_x = clamp1(g_stick_x + js->mag * sinf(angle));
        g_stick_y = clamp1(g_stick_y - js->mag * cosf(angle));
    }
}

/* The first Wii Remote, as the buttons of a GameCube pad. On its own it is held sideways:
 * 2 is A, 1 is B. With a Nunchuk it is held upright, and a Classic Controller has the
 * buttons under their own names. */
static void scan_remote(void)
{
    static const uint32_t sideways[][2] = {
        { WPAD_BUTTON_2, BTN_A },         { WPAD_BUTTON_1, BTN_B },        { WPAD_BUTTON_A, BTN_X },
        { WPAD_BUTTON_B, BTN_Y },         { WPAD_BUTTON_PLUS, BTN_START }, { WPAD_BUTTON_MINUS, BTN_Z },
        { WPAD_BUTTON_RIGHT, BTN_UP },    { WPAD_BUTTON_LEFT, BTN_DOWN },  { WPAD_BUTTON_UP, BTN_LEFT },
        { WPAD_BUTTON_DOWN, BTN_RIGHT },
    };
    static const uint32_t upright[][2] = {
        { WPAD_BUTTON_A, BTN_A },         { WPAD_BUTTON_B, BTN_B },        { WPAD_NUNCHUK_BUTTON_C, BTN_X },
        { WPAD_NUNCHUK_BUTTON_Z, BTN_Y }, { WPAD_BUTTON_PLUS, BTN_START }, { WPAD_BUTTON_MINUS, BTN_Z },
        { WPAD_BUTTON_UP, BTN_UP },       { WPAD_BUTTON_DOWN, BTN_DOWN },  { WPAD_BUTTON_LEFT, BTN_LEFT },
        { WPAD_BUTTON_RIGHT, BTN_RIGHT },
    };
    static const uint32_t classic[][2] = {
        { WPAD_CLASSIC_BUTTON_A, BTN_A },       { WPAD_CLASSIC_BUTTON_B, BTN_B },
        { WPAD_CLASSIC_BUTTON_X, BTN_X },       { WPAD_CLASSIC_BUTTON_Y, BTN_Y },
        { WPAD_CLASSIC_BUTTON_PLUS, BTN_START }, { WPAD_CLASSIC_BUTTON_MINUS, BTN_Z },
        { WPAD_CLASSIC_BUTTON_UP, BTN_UP },     { WPAD_CLASSIC_BUTTON_DOWN, BTN_DOWN },
        { WPAD_CLASSIC_BUTTON_LEFT, BTN_LEFT }, { WPAD_CLASSIC_BUTTON_RIGHT, BTN_RIGHT },
        { WPAD_CLASSIC_BUTTON_ZL, BTN_L },      { WPAD_CLASSIC_BUTTON_ZR, BTN_R },
    };
    const uint32_t (*map)[2] = sideways;
    uint32_t       count = sizeof(sideways) / sizeof(sideways[0]), held, i;
    expansion_t    plugged;

    WPAD_ScanPads();
    held = WPAD_ButtonsHeld(0);
    if ((held & WPAD_BUTTON_HOME) != 0 || (held & WPAD_CLASSIC_BUTTON_HOME) != 0) {
        g_quit = true;
    }
    WPAD_Expansion(0, &plugged);
    if (plugged.type == WPAD_EXP_NUNCHUK) {
        map = upright;
        count = sizeof(upright) / sizeof(upright[0]);
        add_stick(&plugged.nunchuk.js);
    } else if (plugged.type == WPAD_EXP_CLASSIC) {
        map = classic;
        count = sizeof(classic) / sizeof(classic[0]);
        add_stick(&plugged.classic.ljs);
        if ((held & WPAD_CLASSIC_BUTTON_FULL_L) != 0) {
            g_trigger_l = 1.0f;
        }
        if ((held & WPAD_CLASSIC_BUTTON_FULL_R) != 0) {
            g_trigger_r = 1.0f;
        }
    }
    for (i = 0; i < count; i++) {
        if ((held & map[i][0]) != 0) {
            g_buttons |= map[i][1];
        }
    }
}
#endif

/* Reads the pads, once per frame: the first GameCube pad and the first Wii Remote count
 * as one pad. */
static void scan_pads(void)
{
    PAD_ScanPads();
    g_buttons = PAD_ButtonsHeld(0) & 0xFFFFu;
    /* A stick reaches about 100 of its 127 steps, a trigger about 200 of its 255. */
    g_stick_x = clamp1((float)PAD_StickX(0) * (1.0f / 100.0f));
    g_stick_y = clamp1(-(float)PAD_StickY(0) * (1.0f / 100.0f));
    g_trigger_l = clamp1((float)PAD_TriggerL(0) * (1.0f / 200.0f));
    g_trigger_r = clamp1((float)PAD_TriggerR(0) * (1.0f / 200.0f));
#ifdef HW_RVL
    scan_remote();
#endif
    g_scanned_frame = g_frame;
}

/* Binding codes (see wii_baker.gd): buttons are the masks above, axes are 0 stick X,
 * 1 stick Y, 2 left trigger, 3 right trigger. */
float rr_plat_binding_value(const RRPkgBinding *binding)
{
    float v = 0.0f;

    if (g_scanned_frame != g_frame) {
        scan_pads();
    }
    if (binding->kind == RR_BIND_PAD_BUTTON) {
        return (g_buttons & binding->code) != 0 ? 1.0f : 0.0f;
    }
    if (binding->kind == RR_BIND_PAD_AXIS) {
        switch (binding->code) {
        case 0:  v = g_stick_x * (float)binding->sign; break;
        case 1:  v = g_stick_y * (float)binding->sign; break;
        case 2:  v = g_trigger_l; break;
        default: v = g_trigger_r; break;
        }
    }
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Called by the graphics layer when a frame is through: the pads are read again for the
 * next one. */
void wii_frame_done(void)
{
    g_frame++;
}

void rr_plat_vibrate(float weak, float strong)
{
    const bool on = weak > 0.0f || strong > 0.0f;

    /* The motors only know on and off. */
    PAD_ControlMotor(0, on ? PAD_MOTOR_RUMBLE : PAD_MOTOR_STOP);
#ifdef HW_RVL
    WPAD_Rumble(0, on ? 1 : 0);
#endif
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
#ifdef HW_RVL
    /* The language the console is set to in its own menu. */
    static const char *const names[] = { "ja", "en", "de", "fr", "es", "it", "nl", "zh_CN", "zh_TW", "ko" };
    const s32                language = CONF_GetLanguage();

    if (language < 0 || language >= (s32)(sizeof(names) / sizeof(names[0]))) {
        return NULL;
    }
    return names[language];
#else
    static const char *const names[] = { "en", "de", "fr", "es", "it", "nl" };
    const u8                 language = SYS_GetLanguage();

    return language < sizeof(names) / sizeof(names[0]) ? names[language] : NULL;
#endif
}

/* Box2D counts the memory it has taken in a 64-bit atomic number. This CPU has no
 * instruction for that and devkitPPC comes without libatomic, so the compiler calls
 * these two functions. The engine is only called from the main thread: plain code does. */
unsigned long long __atomic_fetch_add_8(volatile void *at, unsigned long long add, int order)
{
    volatile unsigned long long *number = (volatile unsigned long long *)at;
    const unsigned long long     before = *number;

    (void)order;
    *number = before + add;
    return before;
}

unsigned long long __atomic_load_8(const volatile void *at, int order)
{
    (void)order;
    return *(const volatile unsigned long long *)at;
}
