/* d320repro.c -- D320: forced-repro harness for the three high-risk
 * D318-class aim-hold softlock lists (Facility ai_19, Control ai_9,
 * Depot ai_12; findings D320).
 *
 * These beats are scripted cutscene moments that do not play in a bare
 * headless boot (the scene is static with no input), so -- exactly like
 * D318's mode-5 repro -- the probe forces them: at chosen ticks it can
 * dump the chr roster, hijack a guard's AI pointer to a loaded list at a
 * given byte offset, and/or set objective-register bits. The in-tree
 * D320T pin detector (d318watchdog.c, GE_D318T=1) then reports any
 * live-fire attack whose anim frame stays byte-stable for 600 ticks --
 * the exact frozen shape.
 *
 * TEMP: investigation probe, remove when D320 concludes. Inert without
 * the env var (zero cost).
 *
 * Usage: GE_D320R="tick:action;tick:action;..." where action is one of
 *   roster                          -- dump every active chr (num, act,
 *                                      list id, offset, alive)
 *   jump=<chr>,list=<hexid>,off=<n> -- set the chr's ailist to
 *                                      ailistFindById(hexid) at byte
 *                                      offset n, sleep=0
 *   bit=<hex>                       -- objectiveregisters1 |= hex
 *   rename=<chr>,<newnum>           -- reassign a chr's chrnum (used to
 *                                      materialize an absent target, e.g.
 *                                      Control ai_9 aims at 0xFC which is
 *                                      not spawned in a bare boot)
 *   seed=<hi32>,<lo32>              -- overwrite g_randomSeed (the boot
 *                                      seed is deterministic, so vary this
 *                                      between runs to sample different
 *                                      aim-variant branches)
 *   trace=<chr>                     -- from this tick on, log the chr's
 *                                      off/act/atk/ent/frame/endframe on
 *                                      any change + a 120-tick heartbeat
 *                                      while it is in ACT_ATTACK
 *
 * Example (Facility ai_19 hold block):
 *   GE_D320R="60:roster;900:jump=5,list=0x0414,off=341;900:trace=5"
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

#ifdef PORT

/* chrai.c defines these but chrai.h does not declare them. */
extern s32 chraiGetAIListID(AIRecord *AIList, bool *isGlobalAIList);

/* port/src/random.c; not declared in a shared header. */
extern u64 g_randomSeed;

#define D320R_MAX_ACTIONS 16
#define D320R_MAX_TRACES  4

typedef struct
{
    s32  tick;
    int  kind; /* 0 roster, 1 jump, 2 bit, 3 trace */
    s16  chr;
    s32  listid;
    u16  off;
    u32  bit;   /* also: new chrnum for rename */
    int  fired;
} D320RAction;

typedef struct
{
    s16  chr;
    s32  off;
    s32  act;
    s32  atk;
    s32  ent;
    u32  framebits;
    s32  hb;
} D320RTrace;

static ChrRecord *d320rFindChr(s16 chrnum)
{
    s32 i;

    for (i = 0; i <g_NumChrSlots; i++)
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

static void d320rDumpRoster(void)
{
    s32 i;

    osSyncPrintf("D320R: t=%d roster slots=%d bgchrs=%d\n", (int)g_GlobalTimer,
                 (int)g_NumChrSlots, (int)g_ActiveChrsCount);
    for (i = 0; i < g_NumChrSlots; i++)
    {
        ChrRecord *c = &g_ChrSlots[i];
        bool       g = FALSE;
        s32        aid;

        if (c->model == NULL)
        {
            continue;
        }
        aid = c->ailist ? chraiGetAIListID(c->ailist, &g) : -1;
        osSyncPrintf("D320R:   S c%d act=%d aiid=0x%04x%s off=%d atk=0x%x ent=%d %s\n",
                     (int)c->chrnum, (int)c->actiontype, (unsigned)aid,
                     g ? "G" : "", (int)c->aioffset,
                     (unsigned)c->act_attack.attacktype,
                     (int)c->act_attack.entityid,
                     chrIsDead(c) ? "DEAD" : "");
    }
    for (i = 0; i < g_ActiveChrsCount; i++)
    {
        ChrRecord *c = &g_ActiveChrs[i];
        bool       g = FALSE;
        s32        aid = c->ailist ? chraiGetAIListID(c->ailist, &g) : -1;

        osSyncPrintf("D320R:   B c%d act=%d aiid=0x%04x%s off=%d\n",
                     (int)c->chrnum, (int)c->actiontype, (unsigned)aid,
                     g ? "G" : "", (int)c->aioffset);
    }
}

void d320ReproTick(void)
{
    static int        s_on = -1; /* -1 uncached, 0 off, 1 on              */
    static D320RAction s_a[D320R_MAX_ACTIONS];
    static int         s_na = 0;
    static D320RTrace  s_tr[D320R_MAX_TRACES];
    static int         s_nt = 0;
    s32 i;

    if (s_on < 0)
    {
        const char *e = getenv("GE_D320R");

        s_on = (e && e[0]) ? 1 : 0;
        if (s_on)
        {
            /* Parse "tick:action;..." -- one pass, fixed budget. */
            char buf[512];
            char *p, *save;

            strncpy(buf, e, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = 0;
            p = buf;
            while (p && *p && s_na < D320R_MAX_ACTIONS)
            {
                char        *semi = strchr(p, ';');
                char        *colon;
                D320RAction *a    = &s_a[s_na];

                if (semi)
                {
                    *semi = 0;
                }
                colon = strchr(p, ':');
                if (!colon)
                {
                    break;
                }
                *colon = 0;
                a->tick = (s32)strtol(p, NULL, 10);
                a->kind = -1;
                a->chr  = -1;
                a->listid = 0;
                a->off    = 0;
                a->bit    = 0;
                a->fired  = 0;

                if (strcmp(colon + 1, "roster") == 0)
                {
                    a->kind = 0;
                }
                else if (strncmp(colon + 1, "jump=", 5) == 0)
                {
                    const char *q = colon + 6;

                    /* documented form "jump=<chr>,list=..": a leading bare
                     * number (no '=' before the first ',') is the chr */
                    {
                        char *eq0 = strchr(q, '=');
                        char *cm0 = strchr(q, ',');

                        if (cm0 && (!eq0 || cm0 < eq0))
                        {
                            *cm0   = 0;
                            a->chr = (s16)strtol(q, NULL, 0);
                            q      = cm0 + 1;
                        }
                    }

                    while (q && *q)
                    {
                        char       *eq = strchr(q, '=');
                        char       *cm  = strchr(q, ',');
                        const char *key;

                        if (!eq)
                        {
                            break;
                        }
                        if (cm && cm < eq)
                        {
                            break; /* malformed */
                        }
                        *eq = 0;
                        key = q; /* const-cast: parsed in place below */
                        if (strcmp(key, "chr") == 0)      { a->chr    = (s16)strtol(eq + 1, NULL, 0); }
                        else if (strcmp(key, "list") == 0){ a->listid = (s32)strtol(eq + 1, NULL, 0); }
                        else if (strcmp(key, "off") == 0) { a->off    = (u16)strtol(eq + 1, NULL, 0); }
                        q = cm ? cm + 1 : NULL;
                    }
                    if (a->chr >= 0 && a->listid)
                    {
                        a->kind = 1;
                    }
                }
                else if (strncmp(colon + 1, "bit=", 4) == 0)
                {
                    a->bit  = (u32)strtol(colon + 5, NULL, 0);
                    a->kind = 2;
                }
                else if (strncmp(colon + 1, "trace=", 6) == 0)
                {
                    a->chr  = (s16)strtol(colon + 7, NULL, 0);
                    a->kind = 3;
                }
                else if (strncmp(colon + 1, "rename=", 7) == 0)
                {
                    const char *q  = colon + 8;
                    char       *cm = strchr(q, ',');

                    if (cm)
                    {
                        *cm         = 0;
                        a->chr      = (s16)strtol(q, NULL, 0);
                        a->bit      = (u32)strtol(cm + 1, NULL, 0);
                        a->kind     = 4;
                    }
                }
                else if (strncmp(colon + 1, "seed=", 5) == 0)
                {
                    const char *q  = colon + 6;
                    char       *cm = strchr(q, ',');

                    if (cm)
                    {
                        *cm         = 0;
                        a->listid   = (s32)(u32)strtol(q, NULL, 0);      /* hi */
                        a->bit      = (u32)strtol(cm + 1, NULL, 0);      /* lo */
                        a->kind     = 5;
                    }
                }
                if (a->kind >= 0)
                {
                    s_na++;
                }
                p = semi ? semi + 1 : NULL;
            }
            osSyncPrintf("D320R: armed %d actions\n", s_na);
        }
    }
    if (!s_on)
    {
        return;
    }

    for (i = 0; i < s_na; i++)
    {
        D320RAction *a = &s_a[i];

        if (a->fired || (s32)g_GlobalTimer < a->tick)
        {
            continue;
        }
        a->fired = 1;

        switch (a->kind)
        {
            case 0: /* roster */
                d320rDumpRoster();
                break;
            case 1: /* jump */
            {
                ChrRecord *c = d320rFindChr(a->chr);

                if (c)
                {
                    AIRecord *list = ailistFindById(a->listid);

                    if (list)
                    {
                        c->ailist   = list;
                        c->aioffset = a->off;
                        c->sleep    = 0;
                        osSyncPrintf("D320R: t=%d JUMP c%d -> aiid=0x%04x off=%d\n",
                                     (int)g_GlobalTimer, (int)c->chrnum,
                                     (unsigned)a->listid, (int)a->off);
                    }
                    else
                    {
                        osSyncPrintf("D320R: t=%d JUMP FAILED c%d aiid=0x%04x not found\n",
                                     (int)g_GlobalTimer, (int)c->chrnum,
                                     (unsigned)a->listid);
                    }
                }
                else
                {
                    osSyncPrintf("D320R: t=%d JUMP FAILED c%d not active\n",
                                 (int)g_GlobalTimer, (int)a->chr);
                }
                break;
            }
            case 2: /* bit */
                objectiveregisters1 |= a->bit;
                osSyncPrintf("D320R: t=%d BIT |= 0x%08x (now 0x%08x)\n",
                             (int)g_GlobalTimer, (unsigned)a->bit,
                             (unsigned)objectiveregisters1);
                break;
            case 3: /* trace */
                if (s_nt < D320R_MAX_TRACES)
                {
                    s_tr[s_nt].chr       = a->chr;
                    s_tr[s_nt].off       = -1;
                    s_tr[s_nt].act       = -1;
                    s_tr[s_nt].atk       = -1;
                    s_tr[s_nt].ent       = -1;
                    s_tr[s_nt].framebits = 0;
                    s_tr[s_nt].hb        = 0;
                    s_nt++;
                    osSyncPrintf("D320R: t=%d TRACE c%d armed\n",
                                 (int)g_GlobalTimer, (int)a->chr);
                }
                break;
            case 4: /* rename */
            {
                ChrRecord *c = d320rFindChr(a->chr);

                if (c)
                {
                    osSyncPrintf("D320R: t=%d RENAME c%d -> c%d\n",
                                 (int)g_GlobalTimer, (int)c->chrnum,
                                 (int)a->bit);
                    c->chrnum = (s16)a->bit;
                }
                else
                {
                    osSyncPrintf("D320R: t=%d RENAME FAILED c%d not found\n",
                                 (int)g_GlobalTimer, (int)a->chr);
                }
                break;
            }
            case 5: /* seed */
                g_randomSeed = ((u64)(u32)a->listid << 32) | (u32)a->bit;
                osSyncPrintf("D320R: t=%d SEED := %08x%08x\n",
                             (int)g_GlobalTimer, (unsigned)(u32)(g_randomSeed >> 32),
                             (unsigned)(u32)g_randomSeed);
                break;
            default:
                break;
        }
    }

    for (i = 0; i < s_nt; i++)
    {
        D320RTrace *t = &s_tr[i];
        ChrRecord  *c = d320rFindChr(t->chr);
        u32         fb = 0;
        f32         fr = 0.0f, ef = -1.0f;

        if (!c)
        {
            continue;
        }
        if (c->model)
        {
            union { f32 f; u32 u; } x;

            x.f    = modelGetAnimFrame(c->model);
            fb     = x.u;
            fr     = x.f;
            ef     = modelGetAnimEndFrame(c->model);
        }

        if (t->off != (s32)c->aioffset || t->act != (s32)c->actiontype
            || t->atk != (s32)c->act_attack.attacktype
            || t->ent != (s32)c->act_attack.entityid)
        {
            osSyncPrintf("D320R: t=%d c%d off=%d act=%d atk=0x%x ent=%d frame=%.2f endf=%.2f\n",
                         (int)g_GlobalTimer, (int)c->chrnum, (int)c->aioffset,
                         (int)c->actiontype, (unsigned)c->act_attack.attacktype,
                         (int)c->act_attack.entityid, (double)fr, (double)ef);
            t->off = (s32)c->aioffset;
            t->act = (s32)c->actiontype;
            t->atk = (s32)c->act_attack.attacktype;
            t->ent = (s32)c->act_attack.entityid;
        }
        else if (c->actiontype == ACT_ATTACK && fb != t->framebits)
        {
            /* frame is moving: note the range so a later stall is visible */
            osSyncPrintf("D320R: t=%d c%d frame MOVED to %.2f (endf=%.2f off=%d)\n",
                         (int)g_GlobalTimer, (int)c->chrnum, (double)fr,
                         (double)ef, (int)c->aioffset);
        }
        t->framebits = fb;

        if (c->actiontype == ACT_ATTACK && ++t->hb >= 120)
        {
            t->hb = 0;
            osSyncPrintf("D320R: t=%d c%d hb off=%d act=%d atk=0x%x ent=%d frame=%.2f endf=%.2f\n",
                         (int)g_GlobalTimer, (int)c->chrnum, (int)c->aioffset,
                         (int)c->actiontype, (unsigned)c->act_attack.attacktype,
                         (int)c->act_attack.entityid, (double)fr, (double)ef);
        }
    }
}

#endif /* PORT */
