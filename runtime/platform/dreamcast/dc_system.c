/* Dreamcast platform layer on KallistiOS: start-up, time, input, sound and saved games.
 * The graphics are in dc_pvr.c (or dc_gldc.c, see dc_gfx.h).
 *
 * Leaving the program: KallistiOS' default A+B+X+Y+START. */
#include <math.h>
#include <string.h>

#include <kos.h>
#include <dc/sound/sfxmgr.h>
#include <dc/flashrom.h>
#include <dc/fs_vmu.h>
#include <dc/maple/purupuru.h>
#include <dc/vmu_pkg.h>
#include <dc/sound/aica_comm.h>
#include <dc/sound/sound.h>
#include <wav/sndwav.h>

#include "../../core/rr_platform.h"
#include "dc_gfx.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

#define MAX_SOUNDS 64

static uint64_t g_last_frame_us;
static sfxhnd_t g_sounds[MAX_SOUNDS];
static wav_stream_hnd_t g_music = -1;
static int      g_sound_channels[MAX_SOUNDS];
static uint32_t g_sound_rates[MAX_SOUNDS]; /* sample rate each sound was recorded at */
static bool     g_sound_stereo[MAX_SOUNDS]; /* plays on two voices: its own and the next */

uint8_t rr_plat_id(void)
{
    return RR_PLATFORM_DREAMCAST;
}

bool rr_plat_init(const char *title)
{
    (void)title;
    if (!dc_gfx_init()) {
        return false;
    }
    snd_init();
    wav_init();
    g_last_frame_us = timer_us_gettime64();
    return true;
}

void rr_plat_shutdown(void)
{
    snd_sfx_unload_all();
    dc_gfx_shutdown();
}

bool rr_plat_should_quit(void)
{
    return false;
}

uint32_t rr_plat_time_us(void)
{
    return (uint32_t)timer_us_gettime64();
}

float rr_plat_frame_time(void)
{
    const uint64_t now = timer_us_gettime64();
    const float    dt = (float)(uint32_t)(now - g_last_frame_us) * 0.000001f;
    g_last_frame_us = now;
    return dt;
}

bool rr_plat_screenshot(const char *path)
{
    (void)path;
    return false;
}

/* Binding codes (see dreamcast_baker.gd): buttons are CONT_* masks, axes are
 * 0 stick X, 1 stick Y, 2 left trigger, 3 right trigger. Only the first controller. */
float rr_plat_binding_value(const RRPkgBinding *binding)
{
    maple_device_t *pad = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t   *state = pad != NULL ? (cont_state_t *)maple_dev_status(pad) : NULL;
    float           v = 0.0f;

    if (state == NULL) {
        return 0.0f;
    }
    if (binding->kind == RR_BIND_PAD_BUTTON) {
        return (state->buttons & binding->code) != 0 ? 1.0f : 0.0f;
    }
    if (binding->kind == RR_BIND_PAD_AXIS) {
        switch (binding->code) {
        case 0:  v = (float)state->joyx * (1.0f / 127.0f) * (float)binding->sign; break;
        case 1:  v = (float)state->joyy * (1.0f / 127.0f) * (float)binding->sign; break;
        case 2:  v = (float)state->ltrig * (1.0f / 255.0f); break;
        default: v = (float)state->rtrig * (1.0f / 255.0f); break;
        }
    }
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* --- sound --------------------------------------------------------------------------- */

uint32_t rr_snd_load(const RRPkgSound *sound)
{
    uint32_t i;

    if (sound->format != RR_SOUND_ADPCM) {
        return RR_NONE32;
    }
    for (i = 0; i < MAX_SOUNDS; i++) {
        if (g_sounds[i] == SFXHND_INVALID) {
            /* 4 bits per sample selects the AICA's ADPCM format. */
            g_sounds[i] = snd_sfx_load_raw_buf((char *)(sound + 1), sound->data_size,
                                               sound->sample_rate, 4, sound->channels);
            g_sound_channels[i] = -1;
            g_sound_rates[i]    = sound->sample_rate;
            g_sound_stereo[i]   = sound->channels > 1;
            return g_sounds[i] != SFXHND_INVALID ? i : RR_NONE32;
        }
    }
    return RR_NONE32;
}

void rr_snd_unload(uint32_t handle)
{
    if (handle < MAX_SOUNDS && g_sounds[handle] != SFXHND_INVALID) {
        rr_snd_stop(handle);
        snd_sfx_unload(g_sounds[handle]);
        g_sounds[handle] = SFXHND_INVALID;
    }
}

/* 0..1 and -1..1 to what the AICA takes: 0..255, with 128 in the middle. */
static int aica_volume(float volume)
{
    return (int)((volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume)) * 255.0f);
}

static int aica_pan(float pan)
{
    const int p = 128 + (int)(pan * 127.0f);
    return p < 0 ? 0 : (p > 255 ? 255 : p);
}

void rr_snd_play(uint32_t handle, float volume, float pan, float pitch, bool loop)
{
    if (handle < MAX_SOUNDS && g_sounds[handle] != SFXHND_INVALID) {
        sfx_play_data_t data = { 0 };
        data.chn  = -1;
        data.idx  = g_sounds[handle];
        data.vol  = aica_volume(volume);
        data.pan  = aica_pan(pan);
        data.loop = loop ? 1 : 0;
        /* The AICA plays a sound faster by being told a higher sample rate. */
        data.freq = (int)((float)g_sound_rates[handle] * pitch + 0.5f);
        g_sound_channels[handle] = snd_sfx_play_ex(&data);
    }
}

void rr_snd_set(uint32_t handle, float volume, float pan, float pitch)
{
    if (handle < MAX_SOUNDS && g_sound_channels[handle] >= 0) {
        /* The same command KallistiOS sends for the volume of a stream. A stereo sound
         * plays on two voices, one in each speaker, where there is nothing to pan. */
        const int voices = g_sound_stereo[handle] ? 2 : 1;
        int       i;

        for (i = 0; i < voices; i++) {
            AICA_CMDSTR_CHANNEL(packet, cmd, chan);
            cmd->cmd       = AICA_CMD_CHAN;
            cmd->timestamp = 0;
            cmd->size      = AICA_CMDSTR_CHANNEL_SIZE;
            cmd->cmd_id    = (uint32_t)(g_sound_channels[handle] + i);
            chan->cmd      = AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_VOL | AICA_CH_UPDATE_SET_PAN |
                             AICA_CH_UPDATE_SET_FREQ;
            chan->freq     = (uint32_t)((float)g_sound_rates[handle] * pitch + 0.5f);
            chan->vol      = (uint32_t)aica_volume(volume);
            chan->pan      = voices == 2 ? (i == 0 ? 0u : 255u) : (uint32_t)aica_pan(pan);
            snd_sh4_to_aica(packet, cmd->size);
        }
    }
}

bool rr_music_play(const char *path, float volume, bool loop)
{
    rr_music_stop();
    g_music = wav_create(path, loop ? 1 : 0);
    if (g_music < 0) {
        printf("retro: cannot play music '%s'\n", path);
        return false;
    }
    wav_volume(g_music, aica_volume(volume));
    wav_play(g_music);
    return true;
}

void rr_music_set_volume(float volume)
{
    if (g_music >= 0) {
        wav_volume(g_music, aica_volume(volume));
    }
}

void rr_music_stop(void)
{
    if (g_music >= 0) {
        wav_stop(g_music);
        wav_destroy(g_music);
        g_music = -1;
    }
}

void rr_snd_stop(uint32_t handle)
{
    if (handle < MAX_SOUNDS && g_sound_channels[handle] >= 0) {
        snd_sfx_stop(g_sound_channels[handle]);
        if (g_sound_stereo[handle]) {
            snd_sfx_stop(g_sound_channels[handle] + 1);
        }
        g_sound_channels[handle] = -1;
    }
}

/* --- saved games: a file on the memory card in port A, slot 1 -------------------------
 * KallistiOS' VMU file system keeps the file header (description, icons) apart from the
 * content: it is set on the open file and stripped again when the file is read. Files
 * are padded to whole blocks, so the content starts with its own length. */

bool rr_plat_save_write(const char *name, const void *data, uint32_t size)
{
    vmu_pkg_t pkg;
    char      path[32];
    file_t    f;
    bool      ok;

    memset(&pkg, 0, sizeof(pkg));
    strcpy(pkg.desc_short, "Retro Runtime");
    strcpy(pkg.desc_long, "Retro Runtime save");
    strcpy(pkg.app_id, "RetroRuntime");
    pkg.eyecatch_type = VMUPKG_EC_NONE;

    snprintf(path, sizeof(path), "/vmu/a1/%s", name);
    f = fs_open(path, O_WRONLY | O_TRUNC);
    if (f == FILEHND_INVALID) {
        return false;
    }
    ok = fs_vmu_set_header(f, &pkg) == 0 && fs_write(f, &size, 4) == 4 &&
         fs_write(f, data, size) == (ssize_t)size;
    return fs_close(f) == 0 && ok;
}

bool rr_plat_save_read(const char *name, void *data, uint32_t size)
{
    char   path[32];
    uint32_t stored = 0;
    file_t f;
    bool   ok;

    snprintf(path, sizeof(path), "/vmu/a1/%s", name);
    f = fs_open(path, O_RDONLY);
    if (f == FILEHND_INVALID) {
        return false;
    }
    ok = fs_read(f, &stored, 4) == 4 && stored == size && fs_read(f, data, size) == (ssize_t)size;
    fs_close(f);
    return ok;
}

/* --- language and rumble -------------------------------------------------------------- */

const char *rr_plat_locale(void)
{
    /* The language the console is set to in its own menu. */
    static const char *const names[] = { "ja", "en", "de", "fr", "es", "it" };
    flashrom_syscfg_t        config;

    if (flashrom_get_syscfg(&config) != 0 || config.language < 0 || config.language > 5) {
        return NULL;
    }
    return names[config.language];
}

void rr_plat_vibrate(float weak, float strong)
{
    maple_device_t   *pack = maple_enum_type(0, MAPLE_FUNC_PURUPURU);
    const float       most = weak > strong ? weak : strong;
    purupuru_effect_t effect = { 0 };

    if (pack == NULL) {
        return;
    }
    /* One motor: its strength is the stronger of the two, and it runs slowly for the
     * heavy motor of other pads and fast for the light one. Power 0 stops it. */
    effect.cont  = true;
    effect.motor = 1;
    effect.fpow  = most <= 0.0f ? 0u : (uint32_t)(most * 6.0f + 1.5f) > 7u ? 7u : (uint32_t)(most * 6.0f + 1.5f);
    effect.freq  = strong >= weak ? 12 : 40;
    purupuru_rumble(pack, &effect);
}
