/* d318bprobe.h -- D318/D320 boundary probe (M-199 step 2).
 *
 * Env-gated per-tick ground-truth logger for the aim-hold / derail /
 * anim-pin state machine of chrlvTickAttack + chrlvTickAttackCommon.
 * Read-only; inert without GE_D318B. TEMP: remove when D318/D320
 * conclude (same class as d320repro.c).
 */
#ifndef D318BPROBE_H
#define D318BPROBE_H

void d318bProbeTick(void);

#endif /* D318BPROBE_H */
