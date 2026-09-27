/* D349: PC UI reads GE watch settings; PC-UI edits persist per Bond file.
 * The original watch remains session-only. Never call GE setters/save routines
 * from the F10 input/render thread. This file is port-only. */
#include <string.h>
#include <SDL.h>
#include <ultra64.h>
#include <bondconstants.h>
#include "file.h"
#include "file2.h"
#include "front.h"
#include "options.h"
#include "player.h"
#include "system.h"
#include "watchsettings.h"

extern s32 g_StageNum; /* boss.c: direct -level_XX starts with MENU_INVALID */
extern save_data *fileGetSaveForFoldernum(u32 folder);
extern void fileSaveSettingsForFolder(save_data *save);
extern void fileWriteSave(save_data *save);
extern s32 fileGamePakProbe(void);
extern void fileGenerateCRC(u8 *start, u8 *end, save_data *result);
extern void sub_GAME_7F0A91A0(u16 volume);
extern void set_cur_player_look_vertical_inverted(u32 value);
extern void cur_player_set_aim_control(u32 value);
extern void cur_player_set_sight_onscreen_control(u32 value);
extern void cur_player_set_ammo_onscreen_setting(u32 value);

static const char *const keys[WATCH_SETTING_COUNT] = {
    "Bond.Music", "Bond.FX", "Bond.Look", "Bond.AutoAim",
    "Bond.AimControl", "Bond.Sight", "Bond.LookAhead", "Bond.Ammo"
};
static const u16 bits[WATCH_SETTING_COUNT] = {
    0, 0, OPTION_INVERTLOOK, OPTION_AUTOAIM, OPTION_AIMCONTROL,
    OPTION_SIGHTONSCREEN, OPTION_LOOKAHEAD, OPTION_DISPLAYAMMO
};

/* Explicit chooser is separate from selected_folder_num (which frontoptions
 * assigns FOLDER1 merely to draw its dossier background). -1 means none. */
static int chosen = -1;
static int staged[WATCH_SETTING_COUNT];
static int stagedField = -1, stagedFolder = -1;

/* Bounded producer (F10 scheduler) -> consumer (game thread) queue. Snapshot
 * is also guarded; no game/watch reads from F10 render or input paths. */
struct Command { int folder, field, value, commit; };
#define CMD_CAP 128
static struct Command cmds[CMD_CAP];
static int head, count;
static int snapshot[WATCH_SETTING_COUNT];
static int snapFolder = -1;
static SDL_SpinLock lock;

int watchSettingsFieldForKey(const char *key)
{
    for (int i = 0; i < WATCH_SETTING_COUNT; i++)
        if (strcmp(keys[i], key) == 0) return i;
    return -1;
}

/* GE_STARTMENU skips fileValidateSaves: zeroed saves[] looks like a valid
 * folder-0 save to fileGetSaveForFoldernum. Never let a PC UI write that
 * phantom slot (D299). Check the actual CRC before treating it as a file. */
static save_data *validSave(int folder)
{
    if (folder < FOLDER1 || folder >= MAX_FOLDER_COUNT) return NULL;
    save_data *save = fileGetSaveForFoldernum((u32)folder);
    if (!save) return NULL;
    save_data crc = {0};
    fileGenerateCRC((u8 *)&save->completion_bitflags, (u8 *)(save + 1), &crc);
    return crc.chksum1 == save->chksum1 && crc.chksum2 == save->chksum2
        ? save : NULL;
}

static save_data *chosenSave(void) { return validSave(chosen); }

int watchSettingsFolder(void) { return chosen; }

void watchSettingsChooseFile(int dir)
{
    /* Include 'none' so selection is always intentional; skip empty slots.
     * Do not load the file here: GE's loader would change front-end audio. */
    int next = chosen;
    for (int n = 0; n <= MAX_FOLDER_COUNT; n++) {
        next += dir < 0 ? -1 : 1;
        if (next < -1) next = MAX_FOLDER_COUNT - 1;
        if (next >= MAX_FOLDER_COUNT) next = -1;
        if (next == -1 || validSave(next)) break;
    }
    chosen = next;
    stagedField = -1;
    sysLogPrintf(LOG_INFO, "watchsettings: selected Bond file %d", chosen < 0 ? 0 : chosen + 1);
}

static int frontScreen(void) { return current_menu == MENU_PC_OPTIONS; }
static int stageActive(void)
{
    /* Direct -level_XX boots never visit the front-end RUN_STAGE state. */
    return current_menu == MENU_RUN_STAGE ||
           (current_menu == MENU_INVALID && g_StageNum != LEVELID_TITLE);
}

int watchSettingsAvailable(void)
{
    if (frontScreen()) return chosenSave() != NULL;
    if (!stageActive()) return 0;
    int valid;
    SDL_AtomicLock(&lock);
    valid = snapFolder >= FOLDER1 && snapFolder < MAX_FOLDER_COUNT;
    SDL_AtomicUnlock(&lock);
    return valid;
}

static int savedValue(const save_data *save, enum WatchSettingField field)
{
    if (field == WATCH_SETTING_MUSIC) return ((int)save->music_vol << 7) | (save->music_vol >> 1);
    if (field == WATCH_SETTING_FX) return ((int)save->sfx_vol << 7) | (save->sfx_vol >> 1);
    return (save->options & bits[field]) ? 1 : 0;
}

int watchSettingsRead(enum WatchSettingField field)
{
    if ((unsigned)field >= WATCH_SETTING_COUNT) return 0;
    if (frontScreen()) {
        save_data *save = chosenSave();
        if (!save) return 0;
        if (stagedField == (int)field && stagedFolder == chosen) return staged[field];
        return savedValue(save, field);
    }
    int value = 0;
    SDL_AtomicLock(&lock);
    if (snapFolder >= FOLDER1 && snapFolder < MAX_FOLDER_COUNT)
        value = snapshot[field];
    SDL_AtomicUnlock(&lock);
    return value;
}

/* F10 game thread calls this after applying the requested GE setter.
 * temp serialization must NOT overwrite unrelated, watch-only edits or the
 * port-forced controller type in the Bond file. */
int watchSettingsPersistField(int folder, enum WatchSettingField field)
{
    if (folder < FOLDER1 || folder >= MAX_FOLDER_COUNT ||
        (unsigned)field >= WATCH_SETTING_COUNT || !g_CurrentPlayer) return 0;
    save_data *save = validSave(folder);
    if (!save || !fileGamePakProbe()) return 0;
    save_data serialized = *save;
    fileSaveSettingsForFolder(&serialized);
    if (field == WATCH_SETTING_MUSIC) {
        if (save->music_vol == serialized.music_vol) return 1;
        save->music_vol = serialized.music_vol;
    } else if (field == WATCH_SETTING_FX) {
        if (save->sfx_vol == serialized.sfx_vol) return 1;
        save->sfx_vol = serialized.sfx_vol;
    } else {
        u16 mask = bits[field];
        u16 merged = (save->options & (u16)~mask) | (serialized.options & mask);
        if (save->options == merged) return 1;
        save->options = merged;
    }
    fileWriteSave(save); /* real saves[] slot; writes CRC + EEPROM */
    sysLogPrintf(LOG_INFO, "watchsettings: persisted field %d in Bond file %d", field, folder + 1);
    return 1;
}

/* Front end has no active GE watch/player settings. Edit the explicitly
 * chosen save field directly instead of calling fileLoadSettingsForFolder
 * (which also applies the chosen file's audio volume to the front end). */
static void saveFront(enum WatchSettingField field, int value)
{
    save_data *save = chosenSave();
    if (!save || !fileGamePakProbe()) return;
    if (value < 0) value = 0;
    if (field == WATCH_SETTING_MUSIC || field == WATCH_SETTING_FX) {
        if (value > 32767) value = 32767;
        u8 raw = (u8)(value >> 7);
        u8 *dest = field == WATCH_SETTING_MUSIC ? &save->music_vol : &save->sfx_vol;
        if (*dest == raw) return;
        *dest = raw;
    } else {
        u16 mask = bits[field];
        u16 merged = (save->options & (u16)~mask) | (value ? mask : 0);
        if (save->options == merged) return;
        save->options = merged;
    }
    fileWriteSave(save);
    sysLogPrintf(LOG_INFO, "watchsettings: persisted field %d in Bond file %d", field, chosen + 1);
}

void watchSettingsSet(enum WatchSettingField field, int value, int commit)
{
    if ((unsigned)field >= WATCH_SETTING_COUNT || !watchSettingsAvailable()) return;
    if (frontScreen()) {
        if (commit) {
            saveFront(field, value);
            stagedField = -1;
        } else {
            stagedField = field; stagedFolder = chosen; staged[field] = value;
        }
        return;
    }
    SDL_AtomicLock(&lock);
    struct Command cmd = { snapFolder, field, value, commit };
    if (count < CMD_CAP) {
        cmds[(head + count++) % CMD_CAP] = cmd;
        snapshot[field] = value; /* immediate UI feedback for held adjustments */
    } else {
        sysLogPrintf(LOG_WARNING, "watchsettings: command queue full; edit not applied");
    }
    SDL_AtomicUnlock(&lock);
}

void watchSettingsCommit(enum WatchSettingField field)
{
    if ((unsigned)field >= WATCH_SETTING_COUNT || !watchSettingsAvailable()) return;
    watchSettingsSet(field, watchSettingsRead(field), 1);
}

static int liveValue(enum WatchSettingField field)
{
    switch (field) {
    case WATCH_SETTING_MUSIC: return get_mTrack2Vol();
    case WATCH_SETTING_FX: return call_sndGetSfxSlotFirstNaturalVolume();
    case WATCH_SETTING_LOOK: return get_cur_player_look_vertical_inverted();
    case WATCH_SETTING_AUTOAIM: return cur_player_get_autoaim();
    case WATCH_SETTING_AIMCONTROL: return cur_player_get_aim_control();
    case WATCH_SETTING_SIGHT: return cur_player_get_sight_onscreen_control();
    case WATCH_SETTING_LOOKAHEAD: return cur_player_get_lookahead();
    case WATCH_SETTING_AMMO: return cur_player_get_ammo_onscreen_setting();
    default: return 0;
    }
}

static void applyValue(enum WatchSettingField field, int value)
{
    if (value < 0) value = 0;
    if (field <= WATCH_SETTING_FX && value > 32767) value = 32767;
    else if (field > WATCH_SETTING_FX) value = !!value;
    switch (field) {
    case WATCH_SETTING_MUSIC: set_mTrack2Vol((u16)value); break;
    case WATCH_SETTING_FX: sub_GAME_7F0A91A0((u16)value); break;
    case WATCH_SETTING_LOOK: set_cur_player_look_vertical_inverted(value); break;
    case WATCH_SETTING_AUTOAIM: cur_player_set_autoaim(value); break;
    case WATCH_SETTING_AIMCONTROL: cur_player_set_aim_control(value); break;
    case WATCH_SETTING_SIGHT: cur_player_set_sight_onscreen_control(value); break;
    case WATCH_SETTING_LOOKAHEAD: cur_player_set_lookahead(value); break;
    case WATCH_SETTING_AMMO: cur_player_set_ammo_onscreen_setting(value); break;
    default: break;
    }
}

void watchSettingsGameTick(void)
{
    struct Command batch[CMD_CAP];
    int n;
    SDL_AtomicLock(&lock);
    n = count;
    for (int i = 0; i < n; i++) batch[i] = cmds[(head + i) % CMD_CAP];
    head = (head + n) % CMD_CAP;
    count = 0;
    SDL_AtomicUnlock(&lock);

    /* This hook runs on the game thread after a gfxFrameMsgQ receive, even
     * during load; do not call GE audio/player APIs until a stage is active. */
    if (!stageActive() || !g_CurrentPlayer) {
        SDL_AtomicLock(&lock);
        snapFolder = -1;
        SDL_AtomicUnlock(&lock);
        return;
    }
    for (int i = 0; i < n; i++) {
        struct Command *c = &batch[i];
        if (c->folder != selected_folder_num ||
            (unsigned)c->field >= WATCH_SETTING_COUNT ||
            !validSave(c->folder)) continue;
        applyValue((enum WatchSettingField)c->field, c->value);
        if (c->commit) watchSettingsPersistField(c->folder, (enum WatchSettingField)c->field);
    }
    int values[WATCH_SETTING_COUNT];
    for (int f = 0; f < WATCH_SETTING_COUNT; f++)
        values[f] = liveValue((enum WatchSettingField)f);
    SDL_AtomicLock(&lock);
    for (int f = 0; f < WATCH_SETTING_COUNT; f++) snapshot[f] = values[f];
    snapFolder = selected_folder_num;
    SDL_AtomicUnlock(&lock);
}
