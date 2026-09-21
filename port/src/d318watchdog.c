/* d318watchdog.c -- D318: port-side deadlock recovery for the Facility
 * (level_34) Ourumov/Trevelyan execution softlock.
 *
 * What this is (full writeup: docs/dev/findings.md, D318):
 *
 * As-authored latent race, faithfully reproduced by the port (NOT a port
 * bug -- every link in the chain is byte-matched ROM logic):
 *
 *   1. Ourumov (chr 78, ai_22) pre-aims at kneeling Trevelyan (chr 67)
 *      during his monologue: actor_aim_at_actor with TARGET_AIM_ONLY|
 *      TARGET_DONTTURN (attacktype 0x64). chrlvInitActAttack picks a RANDOM
 *      variant from the aim-animation group (randomGetNext() % len) and
 *      modelSetAnimation's it with animlooping=0.
 *   2. The aim anim plays to its endframe H (the "hold" pose). With
 *      animlooping==0, modelTickAnim has no wrap logic; when the frame
 *      reaches endframe, modelConstrainOrWrapAnimFrame clamps BOTH framea
 *      and frameb to ceil(H), so framea==frameb, and the tail of
 *      modelSetAnimFrame2WithChrStuff (if (vb==va) animframe1 = va) then
 *      discards all sub-frame progress: the model is PINNED at H. That is
 *      how a hold pose works by design -- normally something else changes
 *      the attack state next.
 *   3. If the gas cascade sets objective bit 0x04 (combat) mid-monologue,
 *      ai_22 derails to section 0x2a and off=307 runs
 *      actor_fire_or_aim_at_target_update, which SUCCEEDS while the pre-aim
 *      is active: attacktype becomes 0x04, entityid becomes Trevelyan, and
 *      chrlvAttackActionRelated sets a NEW endframe from the variant's
 *      recoil/shoot start frame -- but it never calls modelSetAnimation.
 *   4. Branch (determined by the random variant chosen in step 1):
 *        - new endframe <= H: chrlvTickAttackCommon's "endframe <= frame"
 *          block fires next tick and its unk31 path calls modelSetAnimation
 *          again -> pin broken -> anim advances into the shoot window ->
 *          c78 live-fires Trevelyan -> he dies -> attack ends -> ACT_STAND
 *          -> the stop check at ai_22 off=324 passes -> scene continues.
 *        - new endframe > H: nothing ever calls modelSetAnimation again
 *          (verified by probe: zero calls in frozen runs). The frame stays
 *          pinned at H < shoot_start, c78 never fires, Trevelyan never
 *          dies, and ai_22's off=324 stop check (chrHasStoppedOrPatroling,
 *          TRUE only for ACT_STAND/ACT_ANIM/ACT_PATROL) can never pass
 *          while c78 is in ACT_ATTACK -> PERMANENT SOFTLOCK.
 *
 * The N64 has the identical race and the identical softlock; this watchdog
 * is a deliberate port-layer accommodation (same pattern as the D202 M-66b
 * port-side expiration). It detects the exact unrecoverable signature and,
 * after a long margin, re-initializes c78's attack via sub_GAME_7F025560 --
 * the same entry point the game itself uses for a fresh attack (chrlvTickAttack
 * type_of_motion==2 calls it with (attacktype, entityid)). That runs
 * chrlvInitActAttack: random variant pick + modelSetAnimation(start_frame),
 * which breaks the framea==frameb pin. c78 then live-fires Trevelyan exactly
 * like the natural escape path (verified: escape runs kill c67 within ~230
 * ticks of a fresh init), his attack sequence completes, he reaches
 * ACT_STAND, the stop check at off=324 passes, and ai_22 proceeds as
 * authored (the off=333 headshot lands on the corpse -- the same no-op the
 * game already handles when Trevelyan dies of gas before the execution shot).
 *
 * NOTE: merely killing Trevelyan (e.g. via handles_shot_actors) is NOT
 * enough -- a pinned attack has no internal completion event tied to target
 * death, so c78 would stay in ACT_ATTACK and the stop check would still
 * never pass (verified by probe). The attack state itself must be re-seeded.
 *
 * Signature (all must hold for D318_DEADLOCK_TICKS consecutive ticks):
 *   - chr 78 exists, is on AI list 0x0417 (ai_22), offset in [321, 333)
 *     (the sleep/stop-check loop; 333 is the headshot command itself),
 *   - chr 78 actiontype == ACT_ATTACK with act_attack.entityid == 67
 *     (targeting Trevelyan -- the derailed state; legitimate combat at this
 *     offset targets Bond, ent=1),
 *   - chr 67 exists, has a prop, and is not dead.
 *
 * In every non-deadlocked state the watchdog is inert: the escape path
 * kills Trevelyan within ~25-100 ticks (60x under the threshold) and
 * legitimate combat never targets chr 67 from this loop. One-shot per
 * OCCURRENCE: after firing, it re-arms once the signature has been absent
 * for D318_REARM_TICKS consecutive ticks (the deadlock is gone -- recovered
 * or the player moved on), so a later replay of Facility in the same
 * session (AllUnlocked) is still covered. Logs its detection and
 * intervention for playtest audit.
 *
 * Opt out (research only): GE_D318W=0
 */
#include <stdlib.h>
#include <string.h>

#include "PR/os.h"
#include "bondtypes.h"
#include "chr.h"
#include "chrai.h"
#include "chraction.h"
#include "lv.h"
#include "model.h"
#include "random.h"

#ifdef PORT

/* chrai.c defines these but chrai.h does not declare them. */
extern s32 chraiGetAIListID(AIRecord *AIList, bool *isGlobalAIList);

/* src/bondaicommands.h:467 -- kept local so this port file doesn't pull in
 * the AI-command header just for one bit constant. */
#define D320_TARGET_AIM_ONLY 0x0020 /* "Aim at target instead of firing"   */

/* chraction.c internal (not in chraction.h): the fresh-attack entry point.
 * Signature from the definition; call form mirrors chrlvTickAttack's own
 * re-init at chraction.c:7401. */
extern void sub_GAME_7F025560(ChrRecord *self, s32 attack_type, s32 arg2);

#define D318_LIST_AI22       0x0417 /* Ourumov's execution list (ai_22)      */
#define D318_OFF_LOOP_MIN    321    /* label 0x01: sleep/stop-check loop     */
#define D318_OFF_LOOP_MAX    333    /* exclusive: the headshot command       */
#define D318_OURUMOV_CHR     78
#define D318_TREVELYAN_CHR   67
#define D318_DEADLOCK_TICKS  600    /* 10 s at 60 Hz; escape takes < 2 s     */
#define D318_REARM_TICKS     600    /* signature-absent ticks before re-arm  */

static ChrRecord *d318wFindChr(s16 chrnum)
{
    s32 i;

    for (i = 0; i < g_NumChrSlots; i++)
    {
        if (g_ChrSlots[i].chrnum == chrnum)
        {
            return &g_ChrSlots[i];
        }
    }
    for (i = 0; i < g_ActiveChrsCount; i++)
    {
        if (g_ActiveChrs[i].chrnum == chrnum)
        {
            return &g_ActiveChrs[i];
        }
    }
    return NULL;
}

void d318WatchdogTick(void)
{
    static int  s_enabled   = -1; /* -1 uncached, 0 off (GE_D318W=0), 1 on   */
    static s32  s_run       = 0;  /* consecutive ticks the signature has held */
    static s32  s_absent    = 0;  /* consecutive ticks signature absent       */
    static bool s_fired     = FALSE;
    ChrRecord  *c78;
    ChrRecord  *c67;
    bool        g = FALSE;
    bool        sig;

    if (s_enabled < 0)
    {
        const char *e = getenv("GE_D318W");

        s_enabled = (e && e[0] == '0') ? 0 : 1;
    }
    if (!s_enabled)
    {
        return;
    }

    c78 = d318wFindChr(D318_OURUMOV_CHR);
    c67 = d318wFindChr(D318_TREVELYAN_CHR);

    sig = (c78 && c78->ailist
        && chraiGetAIListID(c78->ailist, &g) == D318_LIST_AI22
        && c78->aioffset >= D318_OFF_LOOP_MIN && c78->aioffset < D318_OFF_LOOP_MAX
        && c78->actiontype == ACT_ATTACK
        && (s32)c78->act_attack.entityid == D318_TREVELYAN_CHR
        && c67 && c67->prop && !chrIsDead(c67));

    if (sig)
    {
        s_absent = 0;
        if (!s_fired)
        {
            if (s_run == 1)
            {
                osSyncPrintf("D318W: t=%d deadlock signature detected (c78 ai_22 off=%d "
                             "ACT_ATTACK ent=67, c67 alive) -- starting %d-tick confirmation window\n",
                             (int)g_GlobalTimer, (int)c78->aioffset, D318_DEADLOCK_TICKS);
            }
            s_run++;
            if (s_run >= D318_DEADLOCK_TICKS)
            {
                osSyncPrintf("D318W: t=%d D318 execution deadlock confirmed -- re-initializing "
                             "c78's attack (sub_GAME_7F025560 atk=0x%x ent=%d) to break the anim pin\n",
                             (int)g_GlobalTimer, (unsigned)c78->act_attack.attacktype,
                             (int)c78->act_attack.entityid);
                sub_GAME_7F025560(c78, (s32)c78->act_attack.attacktype, (s32)c78->act_attack.entityid);
                s_fired = TRUE;
            }
        }
    }
    else
    {
        s_run = 0;
        /* One-shot per occurrence: once the deadlock signature has been
         * clear for a while after a fire, re-arm so a later replay of
         * Facility in the same process (AllUnlocked) is still covered. */
        if (s_fired && ++s_absent >= D318_REARM_TICKS)
        {
            s_fired = FALSE;
            s_absent = 0;
            osSyncPrintf("D318W: t=%d signature clear for %d ticks -- watchdog re-armed\n",
                         (int)g_GlobalTimer, D318_REARM_TICKS);
        }
    }
}

/* ------------------------------------------------------------------ */
/* D318T: diagnostic timeline for the "Ourumov shoots Trevelyan the   */
/* moment his lines begin" report (2026-09-20). Env-gated, OFF by     */
/* default; enable with GE_D318T=1. Port-side read-only: logs state   */
/* transitions so a single playtest captures which combat trigger     */
/* (gas bit 0x04 vs Bond near-miss flag) derailed ai_22, and how many */
/* ticks after the monologue started it landed. No game state is      */
/* touched.                                                            */
/*                                                                     */
/* Background: the ONLY path by which c78 live-fires Trevelyan        */
/* (entityid 67) in this scene is section 0x2a -> off=307             */
/* actor_fire_or_aim_at_target_update succeeding while the pre-aim    */
/* (ACT_ATTACK + AIM_ONLY|DONTTURN) is active. That requires a combat */
/* trigger inside the monologue loop: gas bit 0x04 (off=75) or        */
/* chrIfNearMiss (off=87, CHRFLAG_NEAR_MISS -- set by Bond bullets    */
/* passing c78's bounds, cleared on c78's next tick). The freeze      */
/* (D318) and the immediate-fire are the two branches of the same     */
/* derailed state (random aim-variant endframe vs the hold frame H).  */
/* ------------------------------------------------------------------ */

/* Manual hex parse: MinGW's strtoull returns the sign-extended low 32 bits
 * for values above LLONG_MAX (verified: "deadbeefcafe0123" ->
 * ffffffffcafe0123). Parse by hand; stops at '@' or non-hex. */
static u64 d318wParseHex(const char *s)
{
    u64 v = 0;

    while (*s && *s != '@')
    {
        char c = *s++;

        v <<= 4;
        if (c >= '0' && c <= '9')      { v |= (u64)(c - '0'); }
        else if (c >= 'a' && c <= 'f') { v |= (u64)(c - 'a' + 10); }
        else if (c >= 'A' && c <= 'F') { v |= (u64)(c - 'A' + 10); }
        else                           { break; }
    }
    return v;
}

void d318TimelineTick(void)
{
    /* E-B seed injection: GE_RSEED_LV=<hex64>[@tick] -- set g_randomSeed to
     * the value read from N64 RAM (0x80024460) at a canonical moment, so the
     * port's stream can be A/B-compared against the original from that point
     * on. Test-only; inert without the env var. The DET line logs the running
     * seed every 60 ticks for verifying stream parity afterwards. */
    {
        static int  s_seedArmed = 1;
        static u64  s_seedVal   = 0;
        static s32  s_seedTick  = 0;

        if (s_seedArmed)
        {
            const char *e = getenv("GE_RSEED_LV");

            s_seedArmed = 0;
            if (e && e[0])
            {
                const char *at = strchr(e, '@');

                s_seedVal  = d318wParseHex(e); /* MinGW strtoull mangles >LLONG_MAX */
                s_seedTick = at ? (s32)strtol(at + 1, NULL, 10) : 0;
            }
        }
        if (s_seedVal && (s32)g_GlobalTimer >= s_seedTick)
        {
            u64 v = s_seedVal;

            g_randomSeed = v;
            s_seedVal    = 0;
            osSyncPrintf("D318T: t=%d RSEED_LV -> %016llx\n",
                         (int)g_GlobalTimer, (unsigned long long)v);
        }
    }

    static int  s_on      = -1; /* -1 uncached, 0 off, 1 on (GE_D318T=1)   */
    static s32  s_obj     = -1;  /* last logged objectiveregisters1        */
    static s32  s_off     = -1;  /* last logged c78 aioffset               */
    static s32  s_act     = -1;  /* last logged c78 actiontype             */
    static s32  s_atk     = -1;  /* last logged c78 act_attack.attacktype  */
    static s32  s_ent     = -1;  /* last logged c78 act_attack.entityid    */
    static s32  s_nm      = -1;  /* last logged c78 NEAR_MISS flag state   */
    static f32  s_dmg     = -1.0f;
    static bool s_dead    = FALSE;
    static s32  s_hb      = 0;
    static s32  s_det     = 0;  /* E1 determinism fingerprint counter       */
    ChrRecord  *c78;
    ChrRecord  *c67;

    if (s_on < 0)
    {
        const char *e = getenv("GE_D318T");

        s_on = (e && e[0] == '1') ? 1 : 0;
    }
    if (!s_on)
    {
        return;
    }

    c78 = d318wFindChr(D318_OURUMOV_CHR);
    c67 = d318wFindChr(D318_TREVELYAN_CHR);

    /* Objective bitfield transitions (the derail triggers live here:     */
    /* 0x04 gas/combat, 0x20 surrender/monologue, 0x40 flee).             */
    if (s_obj != objectiveregisters1)
    {
        osSyncPrintf("D318T: t=%d obj bits 0x%08x -> 0x%08x\n",
                 (int)g_GlobalTimer, (unsigned)s_obj, (unsigned)objectiveregisters1);
        s_obj = objectiveregisters1;
    }

    if (c78)
    {
        s32 nm = (c78->chrflags & CHRFLAG_NEAR_MISS) != 0;

        /* D318 anim-internals line: catches the kneel-attack route          */
        /* (unk54==0 => sub_GAME_7F0256F0 "chrAttackKneel" re-inits) and the */
        /* hold/pin frame values, for the "bending down too far" report.     */
        {
            static s32 s_tom  = -1;
            static u32 s_u54  = 0xFFFFFFFFu;
            static f32 s_endf = -1.0f;

            s32 tom  = (s32)c78->act_attack.type_of_motion;
            u32 u54  = c78->act_attack.unk54;
            f32 endf = c78->model ? modelGetAnimEndFrame(c78->model) : -1.0f;

            if (s_tom != tom || s_u54 != u54 || s_endf != endf)
            {
                osSyncPrintf("D318T: t=%d c78 anim mot=%d unk54=%u endframe=%.2f frame1=%.2f (off=%d)\n",
                             (int)g_GlobalTimer, tom, (unsigned)u54,
                             (double)endf,
                             (double)(c78->model ? modelGetAnimFrame(c78->model) : 0.0f),
                             (int)c78->aioffset);
                s_tom  = tom;
                s_u54  = u54;
                s_endf = endf;
            }
        }

        if (s_off != (s32)c78->aioffset || s_act != (s32)c78->actiontype)
        {
            const char *phase = "?";

            if (c78->aioffset >= 38 && c78->aioffset < 44)      { phase = "pre-aim(off38)"; }
            else if (c78->aioffset >= 44 && c78->aioffset < 69) { phase = "wait-loop(off44)"; }
            else if (c78->aioffset >= 69 && c78->aioffset < 272){ phase = "MONOLOGUE(off69+)"; }
            else if (c78->aioffset >= 272 && c78->aioffset < 295){ phase = "execution(0x13)"; }
            else if (c78->aioffset >= 302 && c78->aioffset < 333){ phase = "COMBAT-0x2a"; }

            osSyncPrintf("D318T: t=%d c78 off=%d [%s] act=%d atk=0x%x ent=%d\n",
                     (int)g_GlobalTimer, (int)c78->aioffset, phase,
                     (int)c78->actiontype, (unsigned)c78->act_attack.attacktype,
                     (int)c78->act_attack.entityid);
            s_off = (s32)c78->aioffset;
            s_act = (s32)c78->actiontype;
        }
        if (s_atk != (s32)c78->act_attack.attacktype || s_ent != (s32)c78->act_attack.entityid)
        {
            osSyncPrintf("D318T: t=%d c78 ATTACK CHANGE atk=0x%x ent=%d (off=%d)\n",
                     (int)g_GlobalTimer, (unsigned)c78->act_attack.attacktype,
                     (int)c78->act_attack.entityid, (int)c78->aioffset);
            s_atk = (s32)c78->act_attack.attacktype;
            s_ent = (s32)c78->act_attack.entityid;
        }
        if (s_nm != nm)
        {
            osSyncPrintf("D318T: t=%d c78 NEAR_MISS flag %s (Bond bullet passed his bounds)\n",
                     (int)g_GlobalTimer, nm ? "SET" : "cleared");
            s_nm = nm;
        }
    }

    if (c67)
    {
        bool dead = chrIsDead(c67);

        if (s_dmg != c67->damage || s_dead != dead)
        {
            osSyncPrintf("D318T: t=%d c67(Trevelyan) dmg=%.2f/%.2f %s\n",
                     (int)g_GlobalTimer, (double)c67->damage, (double)c67->maxdamage,
                     dead ? "DEAD" : "");
            s_dmg = c67->damage;
            s_dead = dead;
        }
    }

    /* Heartbeat while c78 is on his execution list, so gaps are visible. */
    if (c78 && c78->ailist)
    {
        bool g = FALSE;

        if (chraiGetAIListID(c78->ailist, &g) == D318_LIST_AI22 && (++s_hb >= 60))
        {
            s_hb = 0;
            osSyncPrintf("D318T: t=%d c78 hb off=%d act=%d atk=0x%x ent=%d obj=0x%08x\n",
                     (int)g_GlobalTimer, (int)c78->aioffset, (int)c78->actiontype,
                     (unsigned)c78->act_attack.attacktype, (int)c78->act_attack.entityid,
                     (unsigned)objectiveregisters1);
        }
    }

    /* E1 determinism fingerprint: one line per second over ALL active chrs  */
    /* + the PRNG seed + objective bits. Two no-input runs that differ here */
    /* are non-deterministic in the port (foundational); identical logs     */
    /* mean run-to-run variation is input-timing-driven.                    */
    if (++s_det >= 60)
    {
        s32 h = 0x811C9DC5;
        s32 i;

        s_det = 0;
        for (i = 0; i < g_ActiveChrsCount; i++)
        {
            ChrRecord *c = &g_ActiveChrs[i];
            union { f32 f; u32 u; } xf, yf, zf, df;

            xf.f = c->prop ? c->prop->pos.x : 0.0f;
            yf.f = c->prop ? c->prop->pos.y : 0.0f;
            zf.f = c->prop ? c->prop->pos.z : 0.0f;
            df.f = c->damage;
            h ^= c->chrnum;          h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= (s32)c->actiontype; h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= (s32)c->aioffset;   h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= xf.u;               h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= yf.u;               h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= zf.u;               h = (h * 0x01000193) ^ 0x9E3779B9;
            h ^= df.u;               h = (h * 0x01000193) ^ 0x9E3779B9;
        }
        osSyncPrintf("D318T: DET t=%d seed=%08x%08x obj=0x%08x h=%08x nchrs=%d\n",
                     (int)g_GlobalTimer,
                     (unsigned)(u32)(g_randomSeed >> 32), (unsigned)(u32)g_randomSeed,
                     (unsigned)objectiveregisters1, (unsigned)h, (int)g_ActiveChrsCount);
    }

    /* D320: generic D318-class pin DETECTION for the sweep's flagged lists
     * (Facility ai_19, Control ai_9, Depot ai_12 -- see findings D320).
     * Detection only: it logs, it never intervenes; the recovery watchdog
     * above stays hardcoded to ai_22 by design until a second list actually
     * freezes in play (D320's explicit instruction).
     *
     * Signature: a chr in ACT_ATTACK whose attacktype is LIVE FIRE (no
     * TARGET_AIM_ONLY bit -- it is supposed to be shooting) and whose
     * modelGetAnimFrame is byte-stable for 600 consecutive ticks (10 s). A
     * guard that should be firing but whose animation never advances is the
     * exact D318 frozen shape on any list. The two legitimate long-stable
     * states are excluded or self-clearing: monologue pre-aims keep the
     * AIM_ONLY bit (excluded), and a working fire cycle re-inits through
     * chrlvTickAttackCommon's "endframe <= frame" block, so its frame does
     * not stay byte-stable for 10 s. One line at first detection, then one
     * per further 600 stable ticks while it persists; a frozen run keeps
     * printing, an escaped run stops after the single line (or none). */
    {
        typedef struct { s16 chrnum; u32 framebits; s32 stable; s32 nextlog; }
                D320Pin;
        static D320Pin s_pins[8];
        static int     s_npins = 0;
        s32 i, j;

        for (i = 0; i < g_ActiveChrsCount; i++)
        {
            ChrRecord *c = &g_ActiveChrs[i];
            union { f32 f; u32 u; } fr;
            D320Pin   *p = NULL;

            if (c->actiontype != ACT_ATTACK || c->model == NULL)
            {
                continue;
            }
            if ((s32)c->act_attack.attacktype & D320_TARGET_AIM_ONLY)
            {
                continue; /* legitimate hold pose (monologue pre-aim) */
            }

            fr.f = modelGetAnimFrame(c->model);
            for (j = 0; j < s_npins; j++)
            {
                if (s_pins[j].chrnum == c->chrnum)
                {
                    p = &s_pins[j];
                    break;
                }
            }
            if (!p)
            {
                if (s_npins < 8)
                {
                    p = &s_pins[s_npins++];
                }
                else
                {
                    continue; /* table full: drop the newest, keep tracking */
                }
                p->chrnum    = c->chrnum;
                p->framebits = fr.u;
                p->stable    = 0;
                p->nextlog   = 600;
            }

            if (p->framebits == fr.u)
            {
                p->stable++;
            }
            else
            {
                p->framebits = fr.u;
                p->stable    = 0;
                p->nextlog   = 600;
            }

            if (p->stable >= p->nextlog)
            {
                bool g = FALSE;
                s32 aid = c->ailist ? chraiGetAIListID(c->ailist, &g) : -1;

                osSyncPrintf("D320T: t=%d PIN? c%d aiid=0x%04x%s off=%d atk=0x%x ent=%d "
                             "mot=%d unk54=%u frame=%.2f stable=%d ticks (D318-class pin candidate)\n",
                             (int)g_GlobalTimer, (int)c->chrnum,
                             (unsigned)aid, g ? "G" : "", (int)c->aioffset,
                             (unsigned)c->act_attack.attacktype,
                             (int)c->act_attack.entityid,
                             (s32)c->act_attack.type_of_motion,
                             (unsigned)c->act_attack.unk54,
                             (double)fr.f, p->stable);
                p->nextlog = p->stable + 600;
            }
        }

        /* Drop entries for chrs that left the tracked state so a later
         * re-entry starts a fresh confirmation window. */
        for (i = 0; i < s_npins; i++)
        {
            bool alive = FALSE;

            for (j = 0; j < g_ActiveChrsCount; j++)
            {
                ChrRecord *c = &g_ActiveChrs[j];

                if (c->chrnum == s_pins[i].chrnum && c->actiontype == ACT_ATTACK
                    && c->model != NULL
                    && !((s32)c->act_attack.attacktype & D320_TARGET_AIM_ONLY))
                {
                    alive = TRUE;
                    break;
                }
            }
            if (!alive)
            {
                s_pins[i] = s_pins[--s_npins];
                i--;
            }
        }
    }
}

#endif /* PORT */
