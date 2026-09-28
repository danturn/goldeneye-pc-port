/* d318bprobe.c -- D318/D320 boundary probe (M-199 step 2).
 *
 * Purpose: capture per-tick ground truth of the aim-hold / derail /
 * anim-pin state machine so the D318/D320/D321 writeups can be rewritten
 * on verified evidence and each watchdog entry judged for excision on its
 * own data. Static analysis (this session) established:
 *
 *   - The hold-pose pin is topologically stable, not a float knife-edge:
 *     once modelConstrainOrWrapAnimFrame clamps both framea and frameb to
 *     ceil(endframe), the tail of modelSetAnimFrame2WithChrStuff re-emits
 *     the stale va==vb pair every tick (the per-frame loop only runs when
 *     floor(frame) crosses an integer boundary, which a pinned animframe1
 *     never does). All comparisons in the frozen state are exact integers.
 *   - Post-derail, escape requires a modelSetAnimation re-init from one of
 *     exactly three paths: chrlvTickAttack's unk36==0 branch (auto
 *     weapons, endframe<=frame), chrlvTickAttackCommon re-init block
 *     branch 1 (unk37!=0, i.e. auto weapon -> kneel-stop) or branch 3
 *     (unk31 armed by the fire gate while frame sits in the recoil/shoot
 *     window). None is reachable in the observed pins (auto weapons,
 *     endframe unbounded) -- BUT the pin itself only persists at 1 sim
 *     tick per rendered frame: a >=2-tick frame advances >=1.0 and
 *     crosses the integer boundary, so no escape path is needed (D329,
 *     lockstep A/B via GE_D318B_CLK below). Not a platform-invariant lock.
 *
 * This probe logs, for env-selected chrs, every operand those decisions
 * read: the drawn variant (identified by pointer against all six firing-
 * group sets), its frame fields, the unk30..unk44 gate bytes, the weapon
 * auto-firing rates, full-precision model frame/endframe/speed state, and
 * each branch condition recomputed from the same fields. Read-only;
 * inert without GE_D318B.
 *
 * Usage: GE_D318B="5,9"   (comma-separated chrnums, max 4)
 *        GE_D318B_ALL=1   (log every ACT_ATTACK tick, not just changes)
 *        GE_D318B_CLK=N   (lockstep: exactly N sim ticks per rendered frame)
 * Combined with the D320R forced repro, e.g.:
 *   GE_D320R="900:jump=5,list=0x0414,off=341" GE_D318B="5"
 *
 * TEMP: investigation probe, remove when D318/D320 conclude.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "PR/os.h"
#include "bondtypes.h"
#include "bondaicommands.h"
#include "chr.h"
#include "chraction.h"
#include "gun.h"
#include "lv.h"
#include "model.h"

#ifdef PORT

#define D318B_MAX_CHRS 4
#define D318B_LINE_CAP 6000

typedef struct
{
    s16  chr;
    /* last-logged state (change detection) */
    int  act;
    u32  atk;
    u32  ent;
    char animid[8];
    u32  framebits;
    s32  stable;   /* consecutive ticks with unchanged frame bits */
    s32  hb;       /* ticks since last heartbeat log */
} D318BState;

static int      s_on = -1;
static int      s_all = 0; /* GE_D318B_ALL=1: log every attack tick */
static int      s_n  = 0;
static D318BState s_s[D318B_MAX_CHRS];
static long     s_lines = 0;
static int      s_capped = 0;

#define d318bLog osSyncPrintf

static void d318bInit(void)
{
    const char *e;
    char buf[128];
    char *p, *save;

    if (s_on != -1)
    {
        return;
    }
    s_on = 0;
    e = getenv("GE_D318B");
    if (!e || !e[0])
    {
        return;
    }
    s_on = 1;
    s_all = (getenv("GE_D318B_ALL") != NULL);
    strncpy(buf, e, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    p = buf;
    while (p && *p && s_n < D318B_MAX_CHRS)
    {
        save = strchr(p, ',');
        if (save)
        {
            *save = 0;
        }
        memset(&s_s[s_n], 0, sizeof(D318BState));
        s_s[s_n].chr = (s16)strtol(p, NULL, 0);
        s_s[s_n].act = -1;
        s_n++;
        p = save ? save + 1 : NULL;
    }
    d318bLog("D318B: probe active for %d chr(s): %s\n", s_n, buf);
}

static ChrRecord *d318bFindChr(s16 chrnum)
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

/* Identify act_attack.animfloats by pointer against all six firing-group
 * sets. Returns 1 and fills id (e.g. "P3.2" = pistol set, bearing group
 * index 3, variant row 2) on success. */
static int d318bIdentifyAnim(struct weapon_firing_animation_table *row, char *id, int idsz)
{
    static const struct
    {
        const char                          tag;
        struct anim_group_info              *const *set;
    } sets[] = {
        { 'P', ptr_pistol_firing_animation_groups },
        { 'R', ptr_rifle_firing_animation_groups },
        { 'D', ptr_doubles_firing_animation_groups },
        { 'C', ptr_crouched_rifle_firing_animation_groups },
        { 'K', ptr_crouched_pistol_firing_animation_groups },
        { 'L', ptr_crouched_doubles_firing_animation_groups },
    };
    s32 si, gi, ri;

    for (si = 0; si < 6; si++)
    {
        for (gi = 0; gi < 32; gi++)
        {
            struct anim_group_info *g = sets[si].set[gi];

            if (g == NULL)
            {
                continue;
            }
            for (ri = 0; ri < g->len; ri++)
            {
                if (&(*g->table)[ri] == row)
                {
                    snprintf(id, idsz, "%c%d.%d", sets[si].tag, gi, ri);
                    return 1;
                }
            }
        }
    }
    snprintf(id, idsz, "-");
    return 0;
}

static s32 d318bAutoRate(ChrRecord *self, GUNHAND hand)
{
    PropRecord *prop;
    s32 item;

    prop = chrGetEquippedWeaponPropWithCheck(self, hand);
    if (prop == NULL || prop->chr == NULL)
    {
        return -99;
    }
    item = (s32)prop->chr->act_attack.attack_item;
    if (item < 0)
    {
        return -98;
    }
    return (s32)bondwalkItemGetAutomaticFiringRate((ITEM_IDS)item);
}

/* GE_D318B_CLK=N (test-only): force exactly N sim ticks per rendered frame,
 * independent of host render speed -- a lockstep model of a console frame
 * rate of 60/N fps. Rewrites the three values lvlManageMpGame just derived
 * from speedgraphframes (the hook runs after them, before any chr tick).
 * Pause/lock frames (g_ClockTimer == 0) are left alone. */
static void d318bClockLock(void)
{
    static s32 s_clk = -1;

    if (s_clk < 0)
    {
        const char *e = getenv("GE_D318B_CLK");

        s_clk = (e && strtol(e, NULL, 10) > 0) ? (s32)strtol(e, NULL, 10) : 0;
        if (s_clk)
        {
            d318bLog("D318B: clock lock active, %d tick(s)/frame\n", s_clk);
        }
    }
    if (s_clk && g_ClockTimer > 0 && g_ClockTimer != s_clk)
    {
        g_GlobalTimer += s_clk - g_ClockTimer;
        g_ClockTimer = s_clk;
        g_GlobalTimerDelta = (f32)s_clk;
    }
}

void d318bProbeTick(void)
{
    s32 i;

    d318bClockLock();
    d318bInit();
    if (!s_on || s_capped)
    {
        return;
    }

    for (i = 0; i < s_n; i++)
    {
        D318BState *st = &s_s[i];
        ChrRecord  *c;
        Model      *m;
        struct act_attack *a;
        struct weapon_firing_animation_table *f;
        f32 fr, ef;
        u32 framebits;
        int chg, atpin, dolog, gate = 0, k;
        char animid[8];

        c = d318bFindChr(st->chr);
        if (c == NULL || c->model == NULL)
        {
            continue;
        }
        m = c->model;
        a = &c->act_attack;
        f = a->animfloats;

        if (c->actiontype != ACT_ATTACK && c->actiontype != ACT_ATTACKROLL)
        {
            /* reset change-detection so the next attack logs its entry */
            st->act = -1;
            continue;
        }

        fr = m->animframe1;
        ef = m->endframe;
        memcpy(&framebits, &fr, sizeof(framebits));

        d318bIdentifyAnim(f, animid, sizeof(animid));

        chg = (st->act != (int)c->actiontype) || (st->atk != a->attacktype)
             || (st->ent != a->entityid) || (strcmp(st->animid, animid) != 0);

        if (framebits == st->framebits)
        {
            st->stable++;
        }
        else
        {
            st->stable = 0;
        }
        st->hb++;

        atpin = ((ef >= 0.0f) && (ef <= fr + 1e-6f));
        dolog = s_all || chg || atpin || (st->stable >= 20) || (st->hb >= 60);
        if (!dolog)
        {
            goto update;
        }
        if (++s_lines >= D318B_LINE_CAP)
        {
            s_capped = 1;
            d318bLog("D318B: line cap (%ld) reached, probe silent\n",
                     (long) D318B_LINE_CAP);
            return;
        }

        /* per-hand fire-loop outcome in chrlvTickAttackCommon (source
         * order): bit0/1 = unk3a[k]==0 arm, frame in shoot window
         * [ss,se) -> ToggleHidden(1) (gun fires) + unk44=now;
         * bit4/5 = unk3a[k]!=0 arm, unk31 re-arm conditions met
         * (unk31==0, unk32 turn, frame in recoil window or >=ss). */
        for (k = 0; k < 2; k++)
        {
            if (a->unk38[k] == 0)
            {
                continue;
            }
            if (a->unk3a[k] == 0)
            {
                if (f->shoot_start_frame <= fr && fr < f->shoot_end_frame)
                {
                    gate |= (1 << k);
                }
            }
            else if (a->unk31 == 0
                     && (k == a->unk32 || a->unk3a[(s32)a->unk32] == 0)
                     && ((f->recoil_start_frame >= 0.0f && f->recoil_start_frame <= fr
                          && fr <= f->recoil_end_frame)
                         || (f->recoil_start_frame < 0.0f && f->shoot_start_frame <= fr)))
            {
                gate |= (0x10 << k);
            }
        }

        d318bLog(
            "D318B: t=%d clk=%d c%d off=%d act=%d atk=0x%x ent=%u toM=%d | v=%s "
            "st=%.9g end=%.9g ss=%.9g se=%.9g rs=%.9g re=%.9g as=%.9g ae=%.9g | "
            "31=%d 32=%d 33=%d 34=%d 36=%d 37=%d 38=%d/%d 3a=%d/%d 40=%u 44=%d atkT=%d | "
            "fr=%.9g ef=%.9g fa=%d fb=%d spd=%.9g ps=%.9g nn=%.9g oo=%.9g ts=%.9g es=%.9g "
            "b0=%.9g b4=%.9g ac=%.9g loop=%d a2=%d a2len=%u wr=%d wl=%d | "
            "aim=%d dnt=%d efle=%d trm=%d b1=%d b2=%d b3=%d gate=%d cf1=%d\n",
            (int)g_GlobalTimer, (int)g_ClockTimer, st->chr, (int)c->aioffset, (int)c->actiontype,
            (unsigned)a->attacktype, (unsigned)a->entityid, (int)a->type_of_motion,
            animid,
            (double)f->start_frame, (double)f->end_frame,
            (double)f->shoot_start_frame, (double)f->shoot_end_frame,
            (double)f->recoil_start_frame, (double)f->recoil_end_frame,
            (double)f->aim_start_frame, (double)f->aim_end_frame,
            (int)a->unk31, (int)a->unk32, (int)a->unk33, (int)a->unk34,
            (int)a->unk36, (int)a->unk37, (int)a->unk38[0], (int)a->unk38[1],
            (int)a->unk3a[0], (int)a->unk3a[1], (unsigned)a->unk40,
            (int)a->unk44, (int)a->attack_time,
            (double)fr, (double)ef, (int)m->framea, (int)m->frameb,
            (double)m->speed, (double)m->playspeed, (double)m->newspeed,
            (double)m->oldspeed, (double)m->timespeed, (double)m->elapsespeed,
            (double)m->unkb0, (double)m->unkb4, (double)m->unkac,
            (int)m->animlooping, (int)(m->anim2 != NULL),
            m->anim2 ? (unsigned)m->anim2->unk04 : 0u,
            d318bAutoRate(c, GUNRIGHT), d318bAutoRate(c, GUNLEFT),
            (int)((a->attacktype & TARGET_AIM_ONLY) != 0),
            (int)((a->attacktype & TARGET_DONTTURN) != 0),
            (int)((ef >= 0.0f) && (ef <= fr)),
            (int)(a->unk36 == 0 && f->recoil_end_frame > 0.0f && fr <= f->recoil_end_frame
                  && (ef >= 0.0f) && (ef <= fr)),
            (int)(a->unk37 != 0 || a->unk34 < a->unk33),
            (int)(a->unk33 == a->unk34),
            (int)(a->unk31 != 0),
            gate,
            (int)(a->attack_time < a->unk44 - 30 && m->anim2 == NULL
                  && f->shoot_start_frame + 10.0f < fr && fr < f->shoot_end_frame));

    update:
        st->act = (int)c->actiontype;
        st->atk = a->attacktype;
        st->ent = a->entityid;
        strncpy(st->animid, animid, sizeof(st->animid) - 1);
        st->animid[sizeof(st->animid) - 1] = 0;
        st->framebits = framebits;
        if (dolog)
        {
            st->hb = 0;
        }
    }
}

#endif /* PORT */
