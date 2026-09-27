#ifndef PORT_WATCHSETTINGS_H
#define PORT_WATCHSETTINGS_H

/* D349: one source of truth for Bond-file-backed GE settings. All GE state and
 * EEPROM access runs on the game thread. F10 sends requests to that thread. */
enum WatchSettingField {
    WATCH_SETTING_MUSIC,
    WATCH_SETTING_FX,
    WATCH_SETTING_LOOK,
    WATCH_SETTING_AUTOAIM,
    WATCH_SETTING_AIMCONTROL,
    WATCH_SETTING_SIGHT,
    WATCH_SETTING_LOOKAHEAD,
    WATCH_SETTING_AMMO,
    WATCH_SETTING_COUNT
};

int watchSettingsFieldForKey(const char *key); /* -1 for non-watch row */
int watchSettingsFolder(void);                /* explicit front chooser; -1 = none */
void watchSettingsChooseFile(int dir);        /* game thread only */
int watchSettingsAvailable(void);
int watchSettingsRead(enum WatchSettingField field); /* front: saved file, F10: snapshot */
void watchSettingsSet(enum WatchSettingField field, int value, int commit);
void watchSettingsCommit(enum WatchSettingField field); /* slider release */
void watchSettingsGameTick(void);             /* game thread; after gfxFrameMsgQ receive */

#endif
