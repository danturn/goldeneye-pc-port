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

static int frontScreen(void);   /* defined below; the choose-file guard uses it */

static const char *const keys[WATCH_SETTING_COUNT] = {
    "Bond.Music", "Bond.FX", "Bond.Look", "Bond.AutoAim",
    "Bond.AimControl", "Bond.Sight", "Bond.LookAhead", "Bond.Ammo"
};
static const u16 bits[WATCH_SETTING_COUNT] = {
    0, 0, OPTION_INVERTLOOK, OPTION_AUTOAIM, OPTION_AIMCONTROL,
    OPTION_SIGHTONSCREEN, OPTION_LOOKAHEAD, OPTION_DISPLAYAMMO
};

/* Explicit chooser is separate from selected_folder_num (which frontoptions
 * assigns FOLDER1 merely to draw its dossier background). -1 means none.
 * D352: guarded by uiLock. Today these are game-thread-only (front options
 * screen); the lock plus the frontScreen() guard in watchSettingsChooseFile
 * keep a future scheduler-thread caller (e.g. an F10 file indicator) from
 * racing the front screen. */
static int chosen = -1;
static int staged[WATCH_SETTING_COUNT];
static int stagedField = -1, stagedFolder = -1;
static SDL_SpinLock uiLock;

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

int watchSettingsFolder(void)
{
    SDL_AtomicLock(&uiLock);
    int f = chosen;
    SDL_AtomicUnlock(&uiLock);
    return f;
}

/* D352: game-thread only. F10 hides the chooser row, so a call from
 * elsewhere signals a broken UI invariant -- refuse it loudly rather than
 * race the front screen's unlocked readers. */
void watchSettingsChooseFile(int dir)
{
    if (!frontScreen()) {
        sysLogPrintf(LOG_WARNING, "watchsettings: choose-file outside the front options screen; ignored");
        return;
    }
    /* Include 'none' so selection is always intentional; skip empty slots.
     * Do not load the file here: GE's loader would change front-end audio. */
    SDL_AtomicLock(&uiLock);
    int next = chosen;
    for (int n = 0; n <= MAX_FOLDER_COUNT; n++) {
        next += dir < 0 ? -1 : 1;
        if (next < -1) next = MAX_FOLDER_COUNT - 1;
        if (next >= MAX_FOLDER_COUNT) next = -1;
        if (next == -1 || validSave(next)) break;
    }
    chosen = next;
    stagedField = -1;
    int sel = chosen;
    SDL_AtomicUnlock(&uiLock);
    sysLogPrintf(LOG_INFO, "watchsettings: selected Bond file %d", sel < 0 ? 0 : sel + 1);
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
    if (frontScreen()) {
        SDL_AtomicLock(&uiLock);
        int f = chosen;
        SDL_AtomicUnlock(&uiLock);
        return validSave(f) != NULL;
    }
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
        int f = -1, stagedVal = -1;
        SDL_AtomicLock(&uiLock);
        f = chosen;
        if (stagedField == (int)field && stagedFolder == f)
            stagedVal = staged[field];
        SDL_AtomicUnlock(&uiLock);
        if (stagedVal >= 0) return stagedVal;
        save_data *save = validSave(f);
        return save ? savedValue(save, field) : 0;
    }
    int value = 0;
    SDL_AtomicLock(&lock);
    if (snapFolder >= FOLDER1 && snapFolder < MAX_FOLDER_COUNT)
        value = snapshot[field];
    SDL_AtomicUnlock(&lock);
    return value;
}

/* D352: game-thread only (watchSettingsGameTick, after the setter was
 * applied). temp serialization must NOT overwrite unrelated, watch-only
 * edits or the port-forced controller type in the Bond file. */
static int watchSettingsPersistField(int folder, enum WatchSettingField field)
{
    if (folder < FOLDER1 || folder >= MAX_FOLDER_COUNT ||
        (unsigned)field >= WATCH_SETTING_COUNT) return 0;
    if (!g_CurrentPlayer) {
        sysLogPrintf(LOG_WARNING, "watchsettings: persist field %d skipped: no current player", field);
        return 0;
    }
    save_data *save = validSave(folder);
    if (!save) {
        sysLogPrintf(LOG_WARNING, "watchsettings: persist field %d in Bond file %d: no valid save (CRC?)", field, folder + 1);
        return 0;
    }
    if (!fileGamePakProbe()) {
        sysLogPrintf(LOG_WARNING, "watchsettings: persist field %d in Bond file %d: EEPROM absent, edit not saved", field, folder + 1);
        return 0;
    }
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
static void saveFrontField(int folder, enum WatchSettingField field, int value)
{
    save_data *save = validSave(folder);
    if (!save) {
        sysLogPrintf(LOG_WARNING, "watchsettings: save field %d in Bond file %d: no valid save (CRC?)", field, folder + 1);
        return;
    }
    if (!fileGamePakProbe()) {
        sysLogPrintf(LOG_WARNING, "watchsettings: save field %d in Bond file %d: EEPROM absent, edit not saved", field, folder + 1);
        return;
    }
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
    sysLogPrintf(LOG_INFO, "watchsettings: persisted field %d in Bond file %d", field, folder + 1);
}

void watchSettingsSet(enum WatchSettingField field, int value, int commit)
{
    if ((unsigned)field >= WATCH_SETTING_COUNT || !watchSettingsAvailable()) return;
    if (frontScreen()) {
        SDL_AtomicLock(&uiLock);
        if (commit) {
            int f = chosen;
            stagedField = -1;
            SDL_AtomicUnlock(&uiLock);
            saveFrontField(f, field, value);
        } else {
            stagedField = field; stagedFolder = chosen; staged[field] = value;
            SDL_AtomicUnlock(&uiLock);
        }
        return;
    }
    SDL_AtomicLock(&lock);
    /* D352: coalesce to one pending command per field. A fast slider drag
     * enqueues many detents per frame; the game thread then applies only
     * the latest value (commit = OR of the pending commits), so the audio
     * setters fire once per frame instead of once per detent. */
    for (int i = 0; i < count; i++) {
        struct Command *c = &cmds[(head + i) % CMD_CAP];
        if (c->field == (int)field) {
            c->value = value;
            c->commit |= commit;
            snapshot[field] = value;
            SDL_AtomicUnlock(&lock);
            return;
        }
    }
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
    if ((unsigned)field >= WATCH_SETTING_COUNT) return;
    if (frontScreen()) {
        /* D352: a commit is meaningful only if a drag staged an edit; a
         * plain click with no file selected stays a silent no-op. */
        int pending;
        SDL_AtomicLock(&uiLock);
        pending = stagedField == (int)field && stagedFolder == chosen && chosen >= FOLDER1;
        SDL_AtomicUnlock(&uiLock);
        if (!pending) return;
    } else if (!watchSettingsAvailable()) {
        /* D352: the UI showed the new value (snapshot) but the stage is not
         * active, so the persist cannot run -- make the drop visible. */
        sysLogPrintf(LOG_WARNING, "watchsettings: commit dropped, no active Bond target (field %d)", field);
        return;
    }
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
    /* This hook runs on the game thread after a gfxFrameMsgQ receive, even
     * during load. D352: HOLD pending commands (do not drain) until a stage
     * is active -- a slider-release commit must not be silently discarded
     * during a level/menu transition. Each command's folder is re-checked
     * against selected_folder_num at apply time, so a stale cross-file edit
     * is still dropped, never misapplied. */
    if (!stageActive() || !g_CurrentPlayer) {
        SDL_AtomicLock(&lock);
        snapFolder = -1;
        SDL_AtomicUnlock(&lock);
        return;
    }
    struct Command batch[CMD_CAP];
    int n;
    SDL_AtomicLock(&lock);
    n = count;
    for (int i = 0; i < n; i++) batch[i] = cmds[(head + i) % CMD_CAP];
    head = (head + n) % CMD_CAP;
    count = 0;
    SDL_AtomicUnlock(&lock);
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
