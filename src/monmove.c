/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "mfndpos.h"
#include "artifact.h"

extern boolean notonhead;

static int disturb(struct monst *);
static void distfleeck(struct monst *, int *, int *, int *);
static int m_arrival(struct monst *);
static void watch_on_duty(struct monst *);
static int vamp_shift(struct monst *, struct permonst *, boolean);

static void make_group_attackers_flee(struct monst* mtmp);
static void share_hp(struct monst* mon1, struct monst* mon2);

/* TRUE if mtmp died */
boolean
mb_trapped(struct monst *mtmp)
{
    if (flags.verbose) {
        if (cansee(mtmp->mx, mtmp->my) && !Unaware) {
            pline("KABOOM!!  You see a door explode.");
        } else if (!Deaf) {
            You_hear("a distant explosion.");
        }
    }
    wake_nearto(mtmp->mx, mtmp->my, 7*7);
    mtmp->mstun = 1;
    mtmp->mhp -= rnd(15);
    if(mtmp->mhp <= 0) {
        mondied(mtmp);
        if (mtmp->mhp > 0) /* lifesaved */
            return(FALSE);
        else
            return(TRUE);
    }
    return(FALSE);
}

/* check whether a monster is carrying a locking/unlocking tool */
boolean
mon_has_key(
    struct monst *mon,
    boolean for_unlocking) /**< TRUE => credit card ok, FALSE => not ok */
{
    if (for_unlocking && m_carrying(mon, CREDIT_CARD)) {
        return TRUE;
    }
    return m_carrying(mon, SKELETON_KEY) || m_carrying(mon, LOCK_PICK);
}

void
mon_yells(struct monst *mon, const char *shout)
{
    if (Deaf) {
        if (canspotmon(mon)) {
            /* Sidenote on "A watchman angrily waves her arms!"
             * Female being called watchman is correct (career name).
             */
            pline("%s angrily %s %s %s!",
                Amonnam(mon),
                nolimbs(mon->data) ? "shakes" : "waves",
                mhis(mon),
                nolimbs(mon->data) ? mbodypart(mon, HEAD) : makeplural(mbodypart(mon, ARM)));
        }
    } else {
        if (canspotmon(mon)) {
            pline("%s yells:", Amonnam(mon));
        } else {
            You_hear("someone yell:");
        }
        verbalize("%s", shout);
    }
}

static void
watch_on_duty(struct monst *mtmp)
{
    int x, y;

    if (mtmp->mpeaceful && in_town(u.ux+u.dx, u.uy+u.dy) &&
        mtmp->mcansee && m_canseeu(mtmp) && !rn2(3)) {
        if (Role_if(PM_CONVICT) && !Upolyd) {
            verbalize("%s yells: Hey!  You are the one from the wanted poster!",
                      Amonnam(mtmp));
            (void) angry_guards(!(flags.soundok));
            stop_occupation();
            return;
        }

        if (picking_lock(&x, &y) && IS_DOOR(levl[x][y].typ) &&
            (levl[x][y].doormask & D_LOCKED)) {

            if (couldsee(mtmp->mx, mtmp->my)) {
                pline("%s yells:", Amonnam(mtmp));
                if (levl[x][y].looted & D_WARNED) {
                    mon_yells(mtmp, "Halt, thief!  You're under arrest!");
                    (void) angry_guards(!(flags.soundok));
                } else {
                    mon_yells(mtmp, "Hey, stop picking that lock!");
                    levl[x][y].looted |= D_WARNED;
                }
                stop_occupation();
            }
        } else if (is_digging()) {
            /* chewing, wand/spell of digging are checked elsewhere */
            watch_dig(mtmp, digging.pos.x, digging.pos.y, FALSE);
        }
    }
}

int
dochugw(struct monst *mtmp)
{
    int x = mtmp->mx, y = mtmp->my;
    boolean already_saw_mon = !occupation ? 0 : canspotmon(mtmp);
    int rd = dochug(mtmp);

    /* a similar check is in monster_nearby() in hack.c */
    /* check whether hero notices monster and stops current activity */
    if (occupation && !rd && !Confusion &&
        (!mtmp->mpeaceful || Hallucination) &&
        /* it's close enough to be a threat */
        distu(x, y) <= (BOLT_LIM+1)*(BOLT_LIM+1) &&
        /* and either couldn't see it before, or it was too far away */
        (!already_saw_mon || !couldsee(x, y) ||
         distu(x, y) > (BOLT_LIM+1)*(BOLT_LIM+1)) &&
        /* can see it now, or sense it and would normally see it */
        (canseemon(mtmp) ||
         (sensemon(mtmp) && couldsee(x, y))) &&
        mtmp->mcanmove &&
        !noattacks(mtmp->data) && !onscary(u.ux, u.uy, mtmp))
        stop_occupation();

    return(rd);
}

boolean
onscary(coordxy x, coordxy y, struct monst *mtmp)
{
    /* creatures who are directly resistant to magical scaring:
     *
     * Rodney, lawful minions, Angels, the Riders, shopkeepers
     * inside their own shop, priests inside their own temple,
     * Humans, Cthulhu, Vlad, the Watcher in the Water,
     * the Quest Nemesis, and unique demons */
    if (mtmp->isshk || mtmp->isgd || mtmp->iswiz || !mtmp->mcansee ||
         mtmp->mpeaceful || mtmp->data->mlet == S_HUMAN ||
         is_lminion(mtmp) || mtmp->data == &mons[PM_ANGEL] ||
         mtmp->data == &mons[PM_CTHULHU] ||
         /* Vlad ignores Elbereth/Scare Monster/Garlic */
         mtmp->data == &mons[PM_VLAD_THE_IMPALER] ||
         mtmp->data == &mons[PM_WATCHER_IN_THE_WATER] ||
         mtmp->mnum == quest_info(MS_NEMESIS) ||
         (mtmp->data->geno & G_UNIQ && is_demon(mtmp->data)) ||
         (mtmp->isshk && inhishop(mtmp)) ||
         (mtmp->ispriest && inhistemple(mtmp)) ||
         is_rider(mtmp->data)) {
        return FALSE;
    }

    /* <0,0> is used by musical scaring to check for the above;
     * it doesn't care about scrolls or engravings or dungeon branch */
    if (x == 0 && y == 0) {
        return TRUE;
    }

    /* should this still be true for defiled/molochian altars? */
    if (IS_ALTAR(levl[x][y].typ) && (is_vampire(mtmp->data) || is_vampshifter(mtmp))) {
        return TRUE;
    }

    return (sobj_at(SCR_SCARE_MONSTER, x, y)
            || ((!flags.elberethignore && sengr_at("Elbereth", x, y)) &&
                 ((u.ux == x && u.uy == y) || (Displaced && mtmp->mux == x && mtmp->muy == y)))
           );
}

/* regenerate lost hit points */
void
mon_regen(struct monst *mon, boolean digest_meal)
{
    /* regeneration not relevant in heaven or hell mode */
    if (heaven_or_hell_mode && !hell_and_hell_mode) {
        mon->mhpmax = 1;
        if (mon->mhp > mon->mhpmax) {
            mon->mhp = 1;
        }
    }

    int freq = 20;
    if (is_blinker(mon->data) && !mon->mflee)
        freq = 1;

    if (mon->mhp < mon->mhpmax && !noregen(mon->data) &&
        (moves % freq == 0 || regenerates(mon->data))) mon->mhp++;
    if (mon->mspec_used) mon->mspec_used--;
    if (digest_meal) {
        if (mon->meating) {
            mon->meating--;
            if (mon->meating <= 0) {
                finish_meating(mon);
            }
        }
    }
}

/*
 * Possibly awaken the given monster.  Return a 1 if the monster has been
 * jolted awake.
 */
static int
disturb(struct monst *mtmp)
{
    /*
     * + Ettins are hard to surprise.
     * + Nymphs, jabberwocks, and leprechauns do not easily wake up.
     *
     * Wake up if:
     *  in direct LOS                                           AND
     *  within 10 squares                                       AND
     *  not stealthy or (mon is an ettin and 9/10)              AND
     *  (mon is not a nymph, jabberwock, or leprechaun) or 1/50 AND
     *  Aggravate or mon is (dog or human) or
     *      (1/7 and mon is not mimicing furniture or object)
     */
    if (couldsee(mtmp->mx, mtmp->my) &&
        distu(mtmp->mx, mtmp->my) <= 100 &&
        (!Stealth || (mtmp->data == &mons[PM_ETTIN] && rn2(10))) &&
        (!(mtmp->data->mlet == S_NYMPH
          || mtmp->data == &mons[PM_JABBERWOCK]
#if 0   /* DEFERRED */
          || mtmp->data == &mons[PM_VORPAL_JABBERWOCK]
#endif
          || mtmp->data->mlet == S_LEPRECHAUN) || !rn2(50)) &&
       (Aggravate_monster
        || (mtmp->data->mlet == S_DOG ||
            mtmp->data->mlet == S_HUMAN)
        || (!rn2(7) && M_AP_TYPE(mtmp) != M_AP_FURNITURE
            && M_AP_TYPE(mtmp) != M_AP_OBJECT))) {
        mtmp->msleeping = 0;
        return(1);
    }
    return(0);
}

/* ungrab/expel held/swallowed hero */
static void
release_hero(struct monst *mon)
{
    if (mon == u.ustuck) {
        if (u.uswallow) {
            expels(mon, mon->data, TRUE);
        } else if (!sticks(youmonst.data)) {
            unstuck(mon); /* let go */
            You("get released!");
        }
    }
}

#define flees_light(mon) ((mon)->data == &mons[PM_GREMLIN] \
                          && (uwep && artifact_light(uwep) && uwep->lamplit))
/* we could include this in the above macro, but probably overkill/overhead */
/*      && (!(which_armor((mon), W_ARMC) != 0                               */
/*            && which_armor((mon), W_ARMH) != 0))                          */

/* monster begins fleeing for the specified time, 0 means untimed flee
 * if first, only adds fleetime if monster isn't already fleeing
 * if fleemsg, prints a message about new flight, otherwise, caller should */
void
monflee(struct monst *mtmp, int fleetime, boolean first, boolean fleemsg)
{
    /* shouldn't happen; maybe warrants impossible()? */
    if (DEADMONSTER(mtmp)) {
        return;
    }

    if (mtmp == u.ustuck) {
        release_hero(mtmp); /* expels/unstuck */
    }

    if (!first || !mtmp->mflee) {
        /* don't lose untimed scare */
        if (!fleetime)
            mtmp->mfleetim = 0;
        else if (!mtmp->mflee || mtmp->mfleetim) {
            fleetime += mtmp->mfleetim;
            /* ensure monster flees long enough to visibly stop fighting */
            if (fleetime == 1) fleetime++;
            mtmp->mfleetim = min(fleetime, 127);
        }
        if (!mtmp->mflee && fleemsg && canseemon(mtmp)
            && M_AP_TYPE(mtmp) != M_AP_FURNITURE
            && M_AP_TYPE(mtmp) != M_AP_OBJECT
            && !mtmp->mfrozen && !is_vegetation(mtmp->data)) {
            /* unfortunately we can't distinguish between temporary
               sleep and temporary paralysis, so both conditions
               receive the same alternate message */
            if (!mtmp->mcanmove || !mtmp->data->mmove) {
                pline("%s seems to flinch.", Adjmonnam(mtmp, "immobile"));
            } else if (flees_light(mtmp)) {
                if (rn2(10) || Deaf) {
                    pline("%s flees from the painful light of %s.", Monnam(mtmp), bare_artifactname(uwep));
                } else {
                    verbalize("Bright light!");
                }
            } else {
                if (mtmp->data->mlet != S_MIMIC)
                    pline("%s turns to flee!", (Monnam(mtmp)));
                else
                    pline("%s mimics a chicken for a moment!", (Monnam(mtmp)));
            }
        }
        mtmp->mflee = 1;
    }

    /* ignore recently-stepped spaces when made to flee */
    memset(mtmp->mtrack, 0, sizeof(mtmp->mtrack));
}

static void
distfleeck(struct monst *mtmp, int *inrange, int *nearby, int *scared)
{
    int seescaryx, seescaryy;
    boolean sawscary = FALSE, bravegremlin = (rn2(5) == 0);

    *inrange = (dist2(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy) <=
                (BOLT_LIM * BOLT_LIM));
    *nearby = *inrange && monnear(mtmp, mtmp->mux, mtmp->muy);

    /* Note: if your image is displaced, the monster sees the Elbereth
     * at your displaced position, thus never attacking your displaced
     * position, but possibly attacking you by accident.  If you are
     * invisible, it sees the Elbereth at your real position, thus never
     * running into you by accident but possibly attacking the spot
     * where it guesses you are.
     */
    if (!mtmp->mcansee || (Invis && !perceives(mtmp->data))) {
        seescaryx = mtmp->mux;
        seescaryy = mtmp->muy;
    } else {
        seescaryx = u.ux;
        seescaryy = u.uy;
    }
    *scared = (*nearby && (onscary(seescaryx, seescaryy, mtmp) ||
                           (flees_light(mtmp) && !bravegremlin) ||
                           (!mtmp->mpeaceful &&
                            in_your_sanctuary(mtmp, 0, 0) &&
                            /* don't warn due to fleeing monsters about
                             * the right temple on Astral */
                            !Is_astralevel(&u.uz))));

    if (*scared) {
        if (rn2(7))
            monflee(mtmp, rnd(10), TRUE, TRUE);
        else
            monflee(mtmp, rnd(100), TRUE, TRUE);
    }
}

#undef flees_light

/* perform a special one-time action for a monster; returns -1 if nothing
   special happened, 0 if monster uses up its turn, 1 if monster is killed */
static int
m_arrival(struct monst *mon)
{
    mon->mstrategy &= ~STRAT_ARRIVE;/* always reset */

    return -1;
}

/* returns 1 if monster died moving, 0 otherwise */
/* The whole dochugw/m_move/distfleeck/mfndpos section is serious spaghetti
 * code. --KAA
 */
int
dochug(struct monst *mtmp)
{
    struct permonst *mdat;
    int tmp=0;
    int inrange, nearby, scared;
    struct obj *ygold = 0, *lepgold = 0;
    struct monst* currmon;

    /*  Pre-movement adjustments */

    mdat = mtmp->data;

    if (mtmp->mstrategy & STRAT_ARRIVE) {
        int res = m_arrival(mtmp);
        if (res >= 0) return res;
    }

    /* check for waitmask status change */
    if ((mtmp->mstrategy & STRAT_WAITFORU) &&
        (m_canseeu(mtmp) || mtmp->mhp < mtmp->mhpmax))
        mtmp->mstrategy &= ~STRAT_WAITFORU;

    /* update quest status flags */
    quest_stat_check(mtmp);

    if (!mtmp->mcanmove || (mtmp->mstrategy & STRAT_WAITMASK)) {
        if (Hallucination) newsym(mtmp->mx, mtmp->my);
        if (mtmp->mcanmove && (mtmp->mstrategy & STRAT_CLOSE) &&
            !mtmp->msleeping && monnear(mtmp, u.ux, u.uy))
            quest_talk(mtmp); /* give the leaders a chance to speak */
        return(0);  /* other frozen monsters can't do anything */
    }

    /* there is a chance we will wake it */
    if (mtmp->msleeping && !disturb(mtmp)) {
        if (Hallucination) newsym(mtmp->mx, mtmp->my);
        return(0);
    }

    /* not frozen or sleeping: wipe out texts written in the dust */
    wipe_engr_at(mtmp->mx, mtmp->my, 1);

    /* confused monsters get unconfused with small probability */
    if (mtmp->mconf && !rn2(50)) mtmp->mconf = 0;

    /* stunned monsters get un-stunned with larger probability */
    if (mtmp->mstun && !rn2(10)) mtmp->mstun = 0;

    /* some monsters teleport */
    if (mtmp->mflee && !rn2(40) && can_teleport(mdat) && !mtmp->iswiz &&
        !level.flags.noteleport) {
        (void) rloc(mtmp, TRUE);
        return(0);
    }
    if (mdat->msound == MS_SHRIEK && !um_dist(mtmp->mx, mtmp->my, 1))
        m_respond(mtmp);
    if (mdat == &mons[PM_MEDUSA] && couldsee(mtmp->mx, mtmp->my))
        m_respond(mtmp);
    if (mtmp->mhp <= 0) return(1); /* m_respond gaze can kill medusa */

    /* group attackers rapidly attack back. */
    if (is_groupattacker(mdat))
    {
        if (mtmp->mhpmax / 2 > mtmp->mhp) {
            make_group_attackers_flee(mtmp);
        }

        if ((monstermoves - mtmp->mgrlastattack) >= rn2(10)+5)
            /* collect monsters around and make them ALL attack. */
            /* only from same species */
            for (currmon = fmon; currmon; currmon = currmon->nmon) {
                if (is_groupattacker(currmon->data) &&
                    currmon->data == mdat) {
                    currmon->mflee = 0;
                    currmon->mgrlastattack = monstermoves;
                }
            }
    }

    /* blinkers can share damage and also keep fleeing */
    if (is_blinker(mdat)) {
        for (currmon = fmon; currmon; currmon = currmon->nmon) {
            /* various conditions before sharing
             * is allowed.
             *
             * Both parts have to be actually alive. (that is,
             * hp is greater than 0).
             *
             * Peacefuls can share hp with hostiles.
             * They are "symphathetic" with the hostiles.
             *
             * Hostiles can also share with peacefuls.
             *
             * But tames can only share between themselves. */
            if (is_blinker(currmon->data) &&
                currmon->data == mdat &&
                currmon != mtmp &&
                currmon->mhp > 0 &&
                mtmp->mhp > 0 &&
                ((!currmon->mtame && !mtmp->mtame) ||
                 (currmon->mtame && mtmp->mtame)) &&
                abs(currmon->mx - mtmp->mx) <= 1 &&
                abs(currmon->my - mtmp->my) <= 1) {
                share_hp(mtmp, currmon);
                break;
            }
        }
        /* Can gain these statuses from sharing. */
        if (!mtmp->mcanmove || mtmp->msleeping)
            return(0);
    }


    /* fleeing monsters might regain courage */
    if (mtmp->mflee && !mtmp->mfleetim
        && mtmp->mhp == mtmp->mhpmax && !rn2(25)) mtmp->mflee = 0;

    /* cease conflict-induced swallow/grab if conflict has ended */
    if (mtmp == u.ustuck && mtmp->mpeaceful && !mtmp->mconf && !Conflict) {
        release_hero(mtmp);
        return 0; /* uses up monster's turn */
    }

    set_apparxy(mtmp);
    /* Must be done after you move and before the monster does.  The
     * set_apparxy() call in m_move() doesn't suffice since the variables
     * inrange, etc. all depend on stuff set by set_apparxy().
     */

    /* Monsters that want to acquire things */
    /* may teleport, so do it before inrange is set */
    if (is_covetous(mdat)) {
        (void) tactics(mtmp);
    }

    /* check distance and scariness of attacks */
    distfleeck(mtmp, &inrange, &nearby, &scared);

    if (find_defensive(mtmp)) {
        if (use_defensive(mtmp) != 0)
            return 1;
    } else if (find_misc(mtmp)) {
        if (use_misc(mtmp) != 0)
            return 1;
    }

    /* Demonic Blackmail! */
    if (nearby && mdat->msound == MS_BRIBE &&
         (monsndx(mdat) != PM_PRISON_GUARD) &&
         mtmp->mpeaceful && !mtmp->mtame && !u.uswallow) {
        if (mtmp->mux != u.ux || mtmp->muy != u.uy) {
            pline("%s whispers at thin air.",
                  cansee(mtmp->mux, mtmp->muy) ? Monnam(mtmp) : "It");

            if (is_demon(youmonst.data)) {
                /* "Good hunting, brother" */
                if (!tele_restrict(mtmp)) {
                    (void) rloc(mtmp, TRUE);
                }
            } else {
                mtmp->minvis = mtmp->perminvis = 0;
                /* Why?  For the same reason in real demon talk */
                pline("%s gets angry!", Amonnam(mtmp));
                mtmp->mpeaceful = 0;
                set_malign(mtmp);
                /* since no way is an image going to pay it off */
            }
        } else if(demon_talk(mtmp)) return(1);  /* you paid it off */
    }

    /* Prison guard extortion */
    if(nearby && (monsndx(mdat) == PM_PRISON_GUARD) && !mtmp->mpeaceful
       && !mtmp->mtame && !u.uswallow && (!mtmp->mspec_used)) {
        long gdemand = 500 * u.ulevel;
        long goffer = 0;

        pline("%s demands %ld %s to avoid re-arrest.", Amonnam(mtmp),
              gdemand, currency(gdemand));
        if ((goffer = bribe(mtmp)) >= gdemand) {
            verbalize("Good.  Now beat it, scum!");
            mtmp->mpeaceful = 1;
            set_malign(mtmp);
        } else {
            pline("I said %ld!", gdemand);
            mtmp->mspec_used = 1000;
        }
    }

    /* the watch will look around and see if you are up to no good :-) */
    if (is_watch(mdat)) {
        watch_on_duty(mtmp);

    /* [DS] Cthulhu also uses psychic blasts */
    } else if ((is_mind_flayer(mdat) || mdat == &mons[PM_CTHULHU]) && !rn2(20)) {
        struct monst *m2, *nmon = (struct monst *)0;

        if (canseemon(mtmp))
            pline("%s concentrates.", Monnam(mtmp));

        if (BTelepat) {
            if (uarmh)
                You("sense something being blocked by %s.", yname(uarmh));
            goto toofar;
        }
        if (distu(mtmp->mx, mtmp->my) > BOLT_LIM * BOLT_LIM) {
            You("sense a faint wave of psychic energy.");
            goto toofar;
        }
        pline("A wave of psychic energy pours over you!");
        if (mtmp->mpeaceful &&
            (!Conflict || resist(mtmp, RING_CLASS, 0, 0))) {
            pline("It feels quite soothing.");
        } else if (!u.uinvulnerable) {
            boolean m_sen = sensemon(mtmp);

            if (m_sen || (Blind_telepat && rn2(2)) || !rn2(10)) {
                int dmg;
                pline("It locks on to your %s!",
                      m_sen ? "telepathy" :
                      Blind_telepat ? "latent telepathy" : "mind");
                dmg = (mdat == &mons[PM_CTHULHU]) ?
                      rn1(10, 10) :
                      rn1(4, 4);
                if (Half_spell_damage) dmg = (dmg+1) / 2;
                if (heaven_or_hell_mode) {
                    You_feel("as if something protected you.");
                }
                else
                    losehp(dmg, "psychic blast", KILLED_BY_AN);
            }
        }
toofar:
        for (m2=fmon; m2; m2 = nmon) {
            nmon = m2->nmon;
            if (DEADMONSTER(m2)) continue;
            if (m2->mpeaceful == mtmp->mpeaceful) continue;
            if (mindless(m2->data)) continue;
            if (m2 == mtmp) continue;
            if (dist2(mtmp->mx, mtmp->my, m2->mx, m2->my) > BOLT_LIM * BOLT_LIM)
                continue;
            if (which_armor(m2, W_ARMH) &&
                which_armor(m2, W_ARMH)->otyp == TINFOIL_HAT) continue;
            if ((telepathic(m2->data) &&
                 (rn2(2) || m2->mblinded)) || !rn2(10)) {
                if (cansee(m2->mx, m2->my))
                    pline("It locks on to %s.", mon_nam(m2));
                m2->mhp -= rnd(15);
                if (m2->mhp <= 0)
                    monkilled(m2, "", AD_DRIN);
                else
                    m2->msleeping = 0;
            }
        }
    }

    /* If monster is nearby you, and has to wield a weapon, do so.   This
     * costs the monster a move, of course.
     */
    if ((!mtmp->mpeaceful || Conflict) && inrange &&
         dist2(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy) <= 8
         && attacktype(mdat, AT_WEAP)) {
        struct obj *mw_tmp;

        /* The scared check is necessary.  Otherwise a monster that is
         * one square near the player but fleeing into a wall would keep
         * switching between pick-axe and weapon.  If monster is stuck
         * in a trap, prefer ranged weapon (wielding is done in thrwmu).
         * This may cost the monster an attack, but keeps the monster
         * from switching back and forth if carrying both.
         */
        mw_tmp = MON_WEP(mtmp);
        if (!(scared && mw_tmp && is_pick(mw_tmp)) &&
            mtmp->weapon_check == NEED_WEAPON &&
            !(IS_TRAPPED(mtmp) && !nearby && select_rwep(mtmp))) {
            mtmp->weapon_check = NEED_HTH_WEAPON;
            if (mon_wield_item(mtmp) != 0) return(0);
        }
    }

    /*  Now the actual movement phase   */

    if (mdat->mlet == S_LEPRECHAUN) {
        ygold = findgold(invent);
        lepgold = findgold(mtmp->minvent);
    }

    if (!nearby || mtmp->mflee || scared ||
        mtmp->mconf || mtmp->mstun || (mtmp->minvis && !rn2(3)) ||
        (mdat->mlet == S_LEPRECHAUN && !ygold && (lepgold || rn2(2))) ||
        (is_wanderer(mdat) && !rn2(4)) || (Conflict && !mtmp->iswiz) ||
        (!mtmp->mcansee && !rn2(4)) || mtmp->mpeaceful) {
        /* Possibly cast an undirected spell if not attacking you */
        /* note that most of the time castmu() will pick a directed
           spell and do nothing, so the monster moves normally */
        /* arbitrary distance restriction to keep monster far away
           from you from having cast dozens of sticks-to-snakes
           or similar spells by the time you reach it */
        if (dist2(mtmp->mx, mtmp->my, u.ux, u.uy) <= 49 && !mtmp->mspec_used) {
            struct attack *a;

            for (a = &mdat->mattk[0]; a < &mdat->mattk[NATTK]; a++) {
                if (a->aatyp == AT_MAGC && (a->adtyp == AD_SPEL || a->adtyp == AD_CLRC || a->adtyp == AD_PUNI)) {
                    if (castmu(mtmp, a, FALSE, FALSE)) {
                        tmp = 3;
                        break;
                    }
                }
            }
        }

        tmp = m_move(mtmp, 0);
        if (tmp != 2) {
            distfleeck(mtmp, &inrange, &nearby, &scared);  /* recalc */
        }

        switch (tmp) {
        case 0:     /* no movement, but it can still attack you */
        case 3:     /* absolutely no movement */
            /* for pets, case 0 and 3 are equivalent */
            /* vault guard might have vanished */
            if (mtmp->isgd && (mtmp->mhp < 1 ||
                               (mtmp->mx == 0 && mtmp->my == 0)))
                return 1;   /* behave as if it died */
            /* During hallucination, monster appearance should
             * still change - even if it doesn't move.
             */
            if(Hallucination) newsym(mtmp->mx, mtmp->my);
            break;
        case 1:     /* monster moved */
            /* Maybe it stepped on a trap and fell asleep... */
            if (mtmp->msleeping || !mtmp->mcanmove) return(0);
            /* Monsters can move and then shoot on same turn;
               our hero can't.  Is that fair? */
            if (!nearby &&
                 (ranged_attk(mdat) || find_offensive(mtmp))) {
                break;
            }
            /* engulfer/grabber checks */
            if (mtmp == u.ustuck) {
                /* a monster that's digesting you can move at the
                 * same time -dlc
                 */
                if (u.uswallow) {
                    return mattacku(mtmp);
                }
                /* if confused grabber has wandered off, let go */
                if (distu(mtmp->mx, mtmp->my) > 2) {
                    unstuck(mtmp);
                }
            }
            return 0;

        case 2: /* monster died */
            return(1);
        }
    }

    /* Now, attack the player if possible - one attack set per monst */

    if (!mtmp->mpeaceful ||
        /* Attacks work even when engulfed by tame monsters */
        (u.uswallow && mtmp == u.ustuck) ||
        (Conflict && !resist(mtmp, RING_CLASS, 0, 0))) {
        if (inrange && !noattacks(mdat) &&
             (Upolyd ? u.mh : u.uhp) > 0 && !scared && tmp != 3) {
            if (mattacku(mtmp)) {
                return 1; /* monster died (e.g. exploded) */
            }

            /* group attacker AI */
            if (is_groupattacker(mdat)) {
                mtmp->mgrlastattack = monstermoves;
            }
        }
        if (mtmp->wormno) {
            wormhitu(mtmp);
        }
    }
    /* special speeches for quest monsters */
    if (!mtmp->msleeping && mtmp->mcanmove && nearby)
        quest_talk(mtmp);
    /* extra emotional attack for vile monsters */
    if (inrange && mtmp->data->msound == MS_CUSS && !mtmp->mpeaceful &&
        couldsee(mtmp->mx, mtmp->my) && !mtmp->minvis && !rn2(5))
        cuss(mtmp);

    return(tmp == 2);
}

static NEARDATA const char practical[] = { WEAPON_CLASS, ARMOR_CLASS, GEM_CLASS, FOOD_CLASS, 0 };
static NEARDATA const char magical[] = {
    AMULET_CLASS, POTION_CLASS, SCROLL_CLASS, WAND_CLASS, RING_CLASS,
    SPBOOK_CLASS, 0
};
static NEARDATA const char indigestion[] = { BALL_CLASS, ROCK_CLASS, 0 };
static NEARDATA const char boulder_class[] = { ROCK_CLASS, 0 };
static NEARDATA const char gem_class[] = { GEM_CLASS, 0 };

boolean
itsstuck(struct monst *mtmp)
{
    if (sticks(youmonst.data) && mtmp == u.ustuck && !u.uswallow) {
        pline("%s cannot escape from you!", Monnam(mtmp));
        return(TRUE);
    }
    return(FALSE);
}

/*
 * should_displace()
 *
 * Displacement of another monster is a last resort and only
 * used on approach. If there are better ways to get to target,
 * those should be used instead. This function does that evaluation.
 */
boolean
should_displace(struct monst *mtmp, coord *poss, long int *info, int cnt, coordxy gx, coordxy gy)

             /* coord poss[9] */
             /* long info[9] */


{
    int shortest_with_displacing = -1;
    int shortest_without_displacing = -1;
    int count_without_displacing = 0;
    int i, nx, ny;
    int ndist;

    for (i = 0; i < cnt; i++) {
        nx = poss[i].x;
        ny = poss[i].y;
        ndist = dist2(nx, ny, gx, gy);
        if (MON_AT(nx, ny) && (info[i] & ALLOW_MDISP) && !(info[i] & ALLOW_M) &&
             !undesirable_disp(mtmp, nx, ny)) {
            if ((shortest_with_displacing == -1) ||
                 (ndist < shortest_with_displacing)) {
                shortest_with_displacing = ndist;
            }
        } else {
            if ((shortest_without_displacing == -1) ||
                 (ndist < shortest_without_displacing)) {
                shortest_without_displacing = ndist;
            }
            count_without_displacing++;
        }
    }
    if ((shortest_with_displacing > -1) &&
         (shortest_with_displacing < shortest_without_displacing || !count_without_displacing)) {
        return TRUE;
    }
    return FALSE;
}

boolean
m_digweapon_check(struct monst *mtmp, coordxy nix, coordxy niy)
{
    boolean can_tunnel = 0;
    struct obj *mw_tmp = MON_WEP(mtmp);

    if (!Is_rogue_level(&u.uz)) {
        can_tunnel = tunnels(mtmp->data);
    }

    if (can_tunnel && needspick(mtmp->data) && !mwelded(mw_tmp) &&
        (may_dig(nix, niy) || closed_door(nix, niy))) {
        /* may_dig() is either IS_STWALL or IS_TREE */
        if (closed_door(nix, niy)) {
            if (!mw_tmp ||
                 !is_pick(mw_tmp) ||
                 !is_axe(mw_tmp)) {
                mtmp->weapon_check = NEED_PICK_OR_AXE;
            }
        } else if (IS_TREE(levl[nix][niy].typ)) {
            if (!(mw_tmp = MON_WEP(mtmp)) || !is_axe(mw_tmp)) {
                mtmp->weapon_check = NEED_AXE;
            }
        } else if (IS_STWALL(levl[nix][niy].typ)) {
            if (!(mw_tmp = MON_WEP(mtmp)) || !is_pick(mw_tmp)) {
                mtmp->weapon_check = NEED_PICK_AXE;
            }
        }
        if (mtmp->weapon_check >= NEED_PICK_AXE && mon_wield_item(mtmp)) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Return values:
 * 0: did not move, but can still attack and do other stuff.
 * 1: moved, possibly can attack.
 * 2: monster died.
 * 3: did not move, and can't do anything else either.
 */
int
m_move(struct monst *mtmp, int after)
{
    int appr;
    coordxy gx, gy, nix, niy;
    xint16 chcnt;
    int chi; /* could be schar except for stupid Sun-2 compiler */
    boolean likegold=0, likegems=0, likeobjs=0, likemagic=0, conceals=0;
    boolean likerock=0, can_tunnel=0, breakrock=0;
    boolean can_open=0, can_unlock=0, doorbuster=0;
    boolean uses_items=0, setlikes=0;
    boolean avoid=FALSE;
    boolean better_with_displacing = FALSE;
    boolean sawmon = canspotmon(mtmp); /* before it moved */
    struct permonst *ptr;
    struct monst *mtoo;
    schar mmoved = 0; /* not strictly nec.: chi >= 0 will do */
    long info[9];
    long flag;
    int omx = mtmp->mx, omy = mtmp->my;
    struct obj *mw_tmp;

    if (is_swamp(mtmp->mx, mtmp->my) && rn2(3) &&
        !is_flyer(mtmp->data) && !is_floater(mtmp->data) &&
        !is_clinger(mtmp->data) && !is_swimmer(mtmp->data) &&
        !amphibious(mtmp->data) && !likes_swamp(mtmp->data))
        return(0);

    if (stationary(mtmp->data)) return(0);

    if (mtmp->mtrapped) {
        int i = mintrap(mtmp, NO_TRAP_FLAGS);
        if(i >= 2) { newsym(mtmp->mx, mtmp->my); return(2); }/* it died */
        if(i == 1) return(0);   /* still in trap, so didn't move */
    }
    if (mtmp->mfeetfrozen) {
        --mtmp->mfeetfrozen;
        if (mtmp->mfeetfrozen) return(0);
    }
    ptr = mtmp->data; /* mintrap() can change mtmp->data -dlc */

    if (mtmp->meating) {
        mtmp->meating--;
        if (mtmp->meating <= 0) {
            finish_meating(mtmp);
        }
        return 3;           /* still eating */
    }
    if (hides_under(ptr) && OBJ_AT(mtmp->mx, mtmp->my) && rn2(10))
        return 0;       /* do not leave hiding place */

    set_apparxy(mtmp);
    /* where does mtmp think you are? */
    /* Not necessary if m_move called from this file, but necessary in
     * other calls of m_move (ex. leprechauns dodging)
     */
#ifdef REINCARNATION
    if (!Is_rogue_level(&u.uz))
#endif
    can_tunnel = tunnels(ptr);
    can_open = !(nohands(ptr) || verysmall(ptr));
    can_unlock = ((can_open && mon_has_key(mtmp, TRUE)) ||
                  mtmp->iswiz || is_rider(ptr));
    doorbuster = is_giant(ptr);
    if(mtmp->wormno) goto not_special;
    /* my dog gets special treatment */
    if (mtmp->mtame) {
        mmoved = dog_move(mtmp, after);
        goto postmov;
    }

    /* likewise for shopkeeper */
    if (mtmp->isshk) {
        mmoved = shk_move(mtmp);
        if(mmoved == -2) return(2);
        if(mmoved >= 0) goto postmov;
        mmoved = 0;     /* follow player outside shop */
    }

    /* and for the guard */
    if (mtmp->isgd) {
        mmoved = gd_move(mtmp);
        if(mmoved == -2) return(2);
        if(mmoved >= 0) goto postmov;
        mmoved = 0;
    }

    /* and the acquisitive monsters get special treatment */
    if (is_covetous(ptr)) {
        coordxy tx = STRAT_GOALX(mtmp->mstrategy),
              ty = STRAT_GOALY(mtmp->mstrategy);
        struct monst *intruder = m_at(tx, ty);
        /*
         * if there's a monster on the object or in possesion of it,
         * attack it.
         */
        if ((dist2(mtmp->mx, mtmp->my, tx, ty) < 2) &&
             intruder && (intruder != mtmp)) {
            notonhead = (intruder->mx != tx || intruder->my != ty);
            if(mattackm(mtmp, intruder) == 2) return(2);
            mmoved = 1;
        } else mmoved = 0;
        goto postmov;
    }

    /* and for the priest */
    if (mtmp->ispriest) {
        mmoved = pri_move(mtmp);
        if(mmoved == -2) return(2);
        if(mmoved >= 0) goto postmov;
        mmoved = 0;
    }

#ifdef MAIL
    if (ptr == &mons[PM_MAIL_DAEMON]) {
        if (!Deaf && canseemon(mtmp)) {
            verbalize("I'm late!");
        }
        mongone(mtmp);
        return(2);
    }
#endif

    /* teleport if that lies in our nature */
    if (ptr == &mons[PM_TENGU] && !rn2(5) && !mtmp->mcan &&
         !tele_restrict(mtmp)) {
        if(mtmp->mhp < 7 || mtmp->mpeaceful || rn2(2))
            (void) rloc(mtmp, TRUE);
        else
            mnexto(mtmp);
        mmoved = 1;
        goto postmov;
    }
not_special:
    if(u.uswallow && !mtmp->mflee && u.ustuck != mtmp) return(1);
    omx = mtmp->mx;
    omy = mtmp->my;
    gx = mtmp->mux;
    gy = mtmp->muy;
    appr = mtmp->mflee ? -1 : 1;
    if (mtmp->mconf || (u.uswallow && mtmp == u.ustuck))
        appr = 0;
    else {
        struct obj *lepgold, *ygold;
        boolean should_see = (couldsee(omx, omy) &&
                              (levl[gx][gy].lit ||
                               !levl[omx][omy].lit) &&
                              (dist2(omx, omy, gx, gy) <= 36));

        if (!mtmp->mcansee ||
            (should_see && Invis && !perceives(ptr) && rn2(11)) ||
            (is_obj_mappear(&youmonst,STRANGE_OBJECT)) || u.uundetected ||
            (is_obj_mappear(&youmonst,GOLD_PIECE) && !likes_gold(ptr)) ||
            (mtmp->mpeaceful && !mtmp->isshk) ||  /* allow shks to follow */
            ((monsndx(ptr) == PM_STALKER || ptr->mlet == S_BAT ||
              ptr->mlet == S_LIGHT) && !rn2(3)))
            appr = 0;

        if (monsndx(ptr) == PM_LEPRECHAUN && (appr == 1) &&
           ( (lepgold = findgold(mtmp->minvent)) &&
             (lepgold->quan > ((ygold = findgold(invent)) ? ygold->quan : 0L)) ))
            appr = -1;

        if (!should_see && can_track(ptr)) {
            coord *cp;

            cp = gettrack(omx, omy);
            if (cp) {
                gx = cp->x;
                gy = cp->y;
            }
        }
    }

    if ((!mtmp->mpeaceful || !rn2(10))
#ifdef REINCARNATION
        && (!Is_rogue_level(&u.uz))
#endif
        ) {
        boolean in_line = lined_up(mtmp) &&
                          (distmin(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy) <=
                           (throws_rocks(youmonst.data) ? 20 : ACURRSTR/2+1)
                          );

        if (appr != 1 || !in_line) {
            /* Monsters in combat won't pick stuff up, avoiding the
             * situation where you toss arrows at it and it has nothing
             * better to do than pick the arrows up.
             */
            int pctload = (curr_mon_load(mtmp) * 100) /
                                   max_mon_load(mtmp);

            /* look for gold or jewels nearby */
            likegold = (likes_gold(ptr) && pctload < 95);
            likegems = (likes_gems(ptr) && pctload < 85);
            uses_items = (!mindless(ptr) && !is_animal(ptr)
                          && pctload < 75);
            likeobjs = (likes_objs(ptr) && pctload < 75);
            likemagic = (likes_magic(ptr) && pctload < 85);
            likerock = (throws_rocks(ptr) && pctload < 50 && !In_sokoban(&u.uz));
            breakrock = is_rockbreaker(ptr);
            conceals = hides_under(ptr);
            setlikes = TRUE;
        }
    }

#define SQSRCHRADIUS    5

    { int minr = SQSRCHRADIUS;     /* not too far away */
      struct obj *otmp;
      int xx, yy;
      int oomx, oomy, lmx, lmy;

      /* cut down the search radius if it thinks character is closer. */
      if(distmin(mtmp->mux, mtmp->muy, omx, omy) < SQSRCHRADIUS &&
         !mtmp->mpeaceful) minr--;
      /* guards shouldn't get too distracted */
      if(!mtmp->mpeaceful && (is_mercenary(ptr)
#ifdef BLACKMARKET
                              || is_blkmktstaff(ptr))
#endif /* BLACKMARKET */
         ) minr = 1;

      if((likegold || likegems || likeobjs || likemagic || likerock || breakrock || conceals)
         && (!*in_rooms(omx, omy, SHOPBASE) || (!rn2(25) && !mtmp->isshk && !Is_blackmarket(&u.uz)))) {
look_for_obj:
          oomx = min(COLNO-1, omx+minr);
          oomy = min(ROWNO-1, omy+minr);
          lmx = max(1, omx-minr);
          lmy = max(0, omy-minr);
          for(otmp = fobj; otmp; otmp = otmp->nobj) {
              /* monsters may pick rocks up, but won't go out of their way
                 to grab them; this might hamper sling wielders, but it cuts
                 down on move overhead by filtering out most common item */
              if (otmp->otyp == ROCK) continue;
              xx = otmp->ox;
              yy = otmp->oy;
              /* Nymphs take everything.  Most other creatures should not
               * pick up corpses except as a special case like in
               * searches_for_item().  We need to do this check in
               * mpickstuff() as well.
               */
              if (xx >= lmx && xx <= oomx && yy >= lmy && yy <= oomy) {
                  /* don't get stuck circling around an object that's underneath
                     an immobile or hidden monster; paralysis victims excluded */
                  if ((mtoo = m_at(xx, yy)) != 0 &&
                      (mtoo->msleeping || mtoo->mundetected ||
                       (mtoo->mappearance && !mtoo->iswiz) ||
                       !mtoo->data->mmove)) continue;

                  /* ignore an object if the monster cannot reach it */
                  if (is_pool(xx, yy) && !is_swimmer(ptr) &&
                      !amphibious(ptr)) continue;
                  /* ignore sokoban prize */
                  if (Is_sokoprize(otmp)) continue;

                  if(((likegold && otmp->oclass == COIN_CLASS) ||
                      (likeobjs && index(practical, otmp->oclass) &&
                       (otmp->otyp != CORPSE || (ptr->mlet == S_NYMPH
                                                 && !is_rider(&mons[otmp->corpsenm])))) ||
                      (likemagic && index(magical, otmp->oclass)) ||
                      (uses_items && searches_for_item(mtmp, otmp)) ||
                      (likerock && otmp->otyp == BOULDER) ||
                      (likegems && otmp->oclass == GEM_CLASS &&
                       objects[otmp->otyp].oc_material != MINERAL) ||
                      (conceals && !cansee(otmp->ox, otmp->oy)) ||
                      (ptr == &mons[PM_GELATINOUS_CUBE] &&
                       !index(indigestion, otmp->oclass) &&
                       !(otmp->otyp == CORPSE &&
                         touch_petrifies(&mons[otmp->corpsenm])))
                      ) && touch_artifact(otmp, mtmp)) {
                      if(can_carry(mtmp, otmp) &&
                         (throws_rocks(ptr) ||
                          !sobj_at(BOULDER, xx, yy)) &&
                         (!is_unicorn(ptr) ||
                          objects[otmp->otyp].oc_material == GEMSTONE) &&
                         /* Don't get stuck circling an Elbereth */
                         !(onscary(xx, yy, mtmp))) {
                          minr = distmin(omx, omy, xx, yy);
                          oomx = min(COLNO-1, omx+minr);
                          oomy = min(ROWNO-1, omy+minr);
                          lmx = max(1, omx-minr);
                          lmy = max(0, omy-minr);
                          gx = otmp->ox;
                          gy = otmp->oy;
                          if (gx == omx && gy == omy) {
                              mmoved = 3; /* actually unnecessary */
                              goto postmov;
                          }
                      }
                  }
              }
          }
      } else if (likegold) {
          /* don't try to pick up anything else, but use the same loop */
          uses_items = 0;
          likegems = likeobjs = likemagic = likerock = breakrock = conceals = 0;
          goto look_for_obj;
      }

      if (minr < SQSRCHRADIUS && appr == -1) {
          if (distmin(omx, omy, mtmp->mux, mtmp->muy) <= 3) {
              gx = mtmp->mux;
              gy = mtmp->muy;
          } else
              appr = 1;
      } }

    /* don't tunnel if hostile and close enough to prefer a weapon */
    if (can_tunnel && needspick(ptr) &&
        ((!mtmp->mpeaceful || Conflict) &&
         dist2(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy) <= 8))
        can_tunnel = FALSE;

    nix = omx;
    niy = omy;
    flag = 0L;
    if (mtmp->mpeaceful && (!Conflict || resist(mtmp, RING_CLASS, 0, 0)))
        flag |= (ALLOW_SANCT | ALLOW_SSM);
    else flag |= ALLOW_U;
    if (is_minion(ptr) || is_rider(ptr)) flag |= ALLOW_SANCT;
    /* unicorn may not be able to avoid hero on a noteleport level */
    if (is_unicorn(ptr) && !level.flags.noteleport) flag |= NOTONL;
    if (passes_walls(ptr)) flag |= (ALLOW_WALL | ALLOW_ROCK);
    if ((passes_bars(ptr) || metallivorous(ptr)) && !In_sokoban(&u.uz)) flag |= ALLOW_BARS;
    if (can_tunnel) flag |= ALLOW_DIG;
    if (is_human(ptr) || ptr == &mons[PM_MINOTAUR]) flag |= ALLOW_SSM;
    if ((is_undead(ptr) && ptr->mlet != S_GHOST) || is_vampshifter(mtmp)) {
        flag |= NOGARLIC;
    }
    if (throws_rocks(ptr) || is_rockbreaker(ptr)) flag |= ALLOW_ROCK;
    if (can_open) flag |= OPENDOOR;
    if (can_unlock) flag |= UNLOCKDOOR;
    if (doorbuster) flag |= BUSTDOOR;
    {
        int i, j, nx, ny, nearer;
        int jcnt, cnt;
        int ndist, nidist;
        coord *mtrk;
        coord poss[9];

        cnt = mfndpos(mtmp, poss, info, flag);
        chcnt = 0;
        jcnt = min(MTSZ, cnt-1);
        chi = -1;
        nidist = dist2(nix, niy, gx, gy);
        /* allow monsters be shortsighted on some levels for balance */
        if(!mtmp->mpeaceful && level.flags.shortsighted &&
           nidist > (couldsee(nix, niy) ? 144 : 36) && appr == 1) appr = 0;
        if (is_unicorn(ptr) && level.flags.noteleport) {
            /* on noteleport levels, perhaps we cannot avoid hero */
            for(i = 0; i < cnt; i++)
                if(!(info[i] & NOTONL)) avoid=TRUE;
        }

        better_with_displacing = should_displace(mtmp, poss, info, cnt, gx, gy);

        for (i=0; i < cnt; i++) {
            if (avoid && (info[i] & NOTONL)) continue;
            nx = poss[i].x;
            ny = poss[i].y;

            if (MON_AT(nx, ny) &&
                 (info[i] & ALLOW_MDISP) &&
                 !(info[i] & ALLOW_M) &&
                 !better_with_displacing) {
                continue;
            }

            if (appr != 0) {
                /* avoid stepping back onto recently-stepped spaces */
                mtrk = &mtmp->mtrack[0];
                for(j=0; j < jcnt; mtrk++, j++)
                    if(nx == mtrk->x && ny == mtrk->y)
                        if(rn2(4*(cnt-j)))
                            goto nxti;
            }

            nearer = ((ndist = dist2(nx, ny, gx, gy)) < nidist);

            if ((appr == 1 && nearer) || (appr == -1 && !nearer) ||
                (!appr && !rn2(++chcnt)) || !mmoved) {
                nix = nx;
                niy = ny;
                nidist = ndist;
                chi = i;
                mmoved = 1;
            }
nxti:       ;
        }
    }

    if(mmoved) {
        int j;

        if (mmoved==1 && (u.ux != nix || u.uy != niy) && itsstuck(mtmp)) {
            return(3);
        }

        if (mmoved == 1 && m_digweapon_check(mtmp, nix,niy)) {
            return 3;
        }

        /* Can't go through crystal wall no matter what */
        if (IS_CRYSTALICEWALL(levl[nix][niy].typ))
            return(3);

        if ((((IS_ROCK(levl[nix][niy].typ) ||
               levl[nix][niy].typ == ICEWALL) &&
              may_dig(nix, niy)) ||
             closed_door(nix, niy)) &&
            mmoved==1 && can_tunnel && needspick(ptr)) {
            if (closed_door(nix, niy)) {
                if (!(mw_tmp = MON_WEP(mtmp)) ||
                    !is_pick(mw_tmp) || !is_axe(mw_tmp))
                    mtmp->weapon_check = NEED_PICK_OR_AXE;
            } else if (IS_TREE(levl[nix][niy].typ)) {
                if (!(mw_tmp = MON_WEP(mtmp)) || !is_axe(mw_tmp))
                    mtmp->weapon_check = NEED_AXE;
            } else if (!(mw_tmp = MON_WEP(mtmp)) || !is_pick(mw_tmp)) {
                mtmp->weapon_check = NEED_PICK_AXE;
            }
            if (mtmp->weapon_check >= NEED_PICK_AXE && mon_wield_item(mtmp))
                return(3);
        }
        /* If ALLOW_U is set, either it's trying to attack you, or it
         * thinks it is.  In either case, attack this spot in preference to
         * all others.
         */
        /* Actually, this whole section of code doesn't work as you'd expect.
         * Most attacks are handled in dochug().  It calls distfleeck(), which
         * among other things sets nearby if the monster is near you--and if
         * nearby is set, we never call m_move unless it is a special case
         * (confused, stun, etc.)  The effect is that this ALLOW_U (and
         * mfndpos) has no effect for normal attacks, though it lets a confused
         * monster attack you by accident.
         */
        if(info[chi] & ALLOW_U) {
            nix = mtmp->mux;
            niy = mtmp->muy;
        }
        if (nix == u.ux && niy == u.uy) {
            mtmp->mux = u.ux;
            mtmp->muy = u.uy;
            return(0);
        }
        /* The monster may attack another based on 1 of 2 conditions:
         * 1 - It may be confused.
         * 2 - It may mistake the monster for your (displaced) image.
         * Pets get taken care of above and shouldn't reach this code.
         * Conflict gets handled even farther away (movemon()).
         */
        if ((info[chi] & ALLOW_M) || (nix == mtmp->mux && niy == mtmp->muy)) {
            return m_move_aggress(mtmp, nix, niy);
        }

        if ((info[chi] & ALLOW_MDISP)) {
            struct monst *mtmp2;
            int mstatus;

            mtmp2 = m_at(nix, niy);
            mstatus = mdisplacem(mtmp, mtmp2, FALSE);
            if ((mstatus & MM_AGR_DIED) || (mstatus & MM_DEF_DIED)) {
                return 2;
            }
            if (mstatus & MM_HIT) {
                return 1;
            }
            return 3;
        }

        if (!m_in_out_region(mtmp, nix, niy))
            return 3;

        remove_monster(omx, omy);
        place_monster(mtmp, nix, niy);
        for(j = MTSZ-1; j > 0; j--)
            mtmp->mtrack[j] = mtmp->mtrack[j-1];
        mtmp->mtrack[0].x = omx;
        mtmp->mtrack[0].y = omy;
        /* Place a segment at the old position. */
        if (mtmp->wormno) worm_move(mtmp);
    } else {
        if (is_unicorn(ptr) && rn2(2) && !tele_restrict(mtmp)) {
            (void) rloc(mtmp, TRUE);
            return(1);
        }
        if(mtmp->wormno) worm_nomove(mtmp);
    }
postmov:
    if (mmoved == 1 || mmoved == 3) {
        boolean canseeit = cansee(mtmp->mx, mtmp->my);

        if (mmoved == 1) {
            int trapret;

            /* normal monster move will already have <nix,niy>,
               but pet dog_move() with 'goto postmov' won't */
            nix = mtmp->mx, niy = mtmp->my;
            /* sequencing issue:  when monster movement decides that a
               monster can move to a door location, it moves the monster
               there before dealing with the door rather than after;
               so a vampire/bat that is going to shift to fog cloud and
               pass under the door is already there but transformation
               into fog form--and its message, when in sight--has not
               happened yet; we have to move monster back to previous
               location before performing the vamp_shift() to make the
               message happen at right time, then back to the door again
               [if we did the shift above, before moving the monster,
               we would need to duplicate it in dog_move()...] */
            if (is_vampshifter(mtmp) && !amorphous(mtmp->data) &&
                 IS_DOOR(levl[nix][niy].typ) &&
                 ((levl[nix][niy].doormask & (D_LOCKED | D_CLOSED)) != 0) &&
                 can_fog(mtmp)) {
                if (sawmon) {
                    remove_monster(nix, niy);
                    place_monster(mtmp, omx, omy);
                    newsym(nix, niy), newsym(omx, omy);
                }
                if (vamp_shift(mtmp, &mons[PM_FOG_CLOUD], sawmon)) {
                    ptr = mtmp->data; /* update cached value */
                }
                if (sawmon) {
                    remove_monster(omx, omy);
                    place_monster(mtmp, nix, niy);
                    newsym(omx, omy), newsym(nix, niy);
                }
            }

            newsym(omx, omy); /* update the old position */
            trapret = mintrap(mtmp, NO_TRAP_FLAGS);
            if (trapret == Trap_Killed_Mon || trapret == Trap_Moved_Mon) {
                if (mtmp->mx) {
                    newsym(mtmp->mx, mtmp->my);
                }
                return 2; /* it died */
            }
            ptr = mtmp->data; /* in case mintrap() caused polymorph */

            /* open a door, or crash through it, if you can */
            if (IS_DOOR(levl[mtmp->mx][mtmp->my].typ)
                && !passes_walls(ptr) /* doesn't need to open doors */
                && !can_tunnel /* taken care of below */
               ) {
                struct rm *here = &levl[mtmp->mx][mtmp->my];
                boolean btrapped = (here->doormask & D_TRAPPED);
                boolean observeit = canseeit && canspotmon(mtmp);

                /* if mon has MKoT, disarm door trap; no message given */
                if (btrapped && has_magic_key(mtmp)) {
                    /* BUG: this lets a vampire or blob or a doorbuster
                       holding the Key disarm the trap even though it isn't
                       using that Key when squeezing under or smashing the
                       door.  Not significant enough to worry about; perhaps
                       the Key's magic is more powerful for monsters? */
                    here->doormask &= ~D_TRAPPED;
                    btrapped = FALSE;
                }
                if (here->doormask & (D_LOCKED|D_CLOSED) && amorphous(ptr)) {
                    if (flags.verbose && canseemon(mtmp))
                        pline("%s %s under the door.", Monnam(mtmp),
                              (ptr == &mons[PM_FOG_CLOUD] ||
                               ptr->mlet == S_LIGHT)
                              ? "flows" : "oozes");
                } else if (here->doormask & D_LOCKED && can_unlock) {
                    if (btrapped) {
                        here->doormask = D_NODOOR;
                        newsym(mtmp->mx, mtmp->my);
                        unblock_point(mtmp->mx, mtmp->my); /* vision */
                        if(mb_trapped(mtmp)) return(2);
                    } else {
                        if (flags.verbose) {
                            if (observeit) {
                                pline("%s unlocks and opens a door.", Monnam(mtmp));
                            } else if (canseeit) {
                                You_see("a door unlock and open.");
                            } else if (!Deaf) {
                                You_hear("a door unlock and open.");
                            }
                        }
                        here->doormask = D_ISOPEN;
                        /* newsym(mtmp->mx, mtmp->my); */
                        unblock_point(mtmp->mx, mtmp->my); /* vision */
                    }
                } else if (here->doormask == D_CLOSED && can_open) {
                    if (btrapped) {
                        here->doormask = D_NODOOR;
                        newsym(mtmp->mx, mtmp->my);
                        unblock_point(mtmp->mx, mtmp->my); /* vision */
                        if(mb_trapped(mtmp)) return(2);
                    } else {
                        if (flags.verbose) {
                            if (observeit) {
                                pline("%s opens a door.", Monnam(mtmp));
                            } else if (canseeit) {
                                You_see("a door open.");
                            } else if (!Deaf) {
                                You_hear("a door open.");
                            }
                        }
                        here->doormask = D_ISOPEN;
                        /* newsym(mtmp->mx, mtmp->my); */  /* done below */
                        unblock_point(mtmp->mx, mtmp->my); /* vision */
                    }
                } else if (here->doormask & (D_LOCKED|D_CLOSED)) {
                    /* mfndpos guarantees this must be a doorbuster */
                    if (btrapped) {
                        here->doormask = D_NODOOR;
                        newsym(mtmp->mx, mtmp->my);
                        unblock_point(mtmp->mx, mtmp->my); /* vision */
                        if(mb_trapped(mtmp)) return(2);
                    } else {
                        if (flags.verbose) {
                            if (observeit) {
                                pline("%s smashes down a door.", Monnam(mtmp));
                            } else if (canseeit) {
                                You_see("a door crash open.");
                            } else if (!Deaf) {
                                You_hear("a door crash open.");
                            }
                        }
                        if (here->doormask & D_LOCKED && !rn2(2))
                            here->doormask = D_NODOOR;
                        else here->doormask = D_BROKEN;
                        /* newsym(mtmp->mx, mtmp->my); */ /* done below */
                        unblock_point(mtmp->mx, mtmp->my); /* vision */
                    }
                    /* if it's a shop door, schedule repair */
                    if (*in_rooms(mtmp->mx, mtmp->my, SHOPBASE))
                        add_damage(mtmp->mx, mtmp->my, 0L);
                }
            }
            if (levl[mtmp->mx][mtmp->my].typ == IRONBARS) {
                boolean is_diggable = !(levl[mtmp->mx][mtmp->my].wall_info & W_NONDIGGABLE);
                if (dmgtype(ptr, AD_DISN)) {
                    if (canseeit)
                        pline("%s dissolves the iron bars.",
                              Monnam(mtmp));
                    dissolve_bars(mtmp->mx, mtmp->my);
                } else if (metallivorous(ptr) && is_diggable) {
                    if (mdig_tunnel(mtmp)) {
                        return 2;
                    }
                    if (canseeit) {
                        pline("%s chews through the iron bars.", Monnam(mtmp));
                    } else {
                        You_hear("a crunching noise.");
                    }
                }
                else if (flags.verbose && canseemon(mtmp))
                    Norep("%s %s %s the iron bars.", Monnam(mtmp),
                          /* pluralization fakes verb conjugation */
                          makeplural(locomotion(ptr, "pass")),
                          passes_walls(ptr) ? "through" : "between");
            } else if (can_tunnel && mdig_tunnel(mtmp)) /* Possibly dig */
                return(2); /* mon died (position already updated) */

            /* set also in domove(), hack.c */
            if (u.uswallow && mtmp == u.ustuck &&
                (mtmp->mx != omx || mtmp->my != omy)) {
                /* If the monster moved, then update */
                u.ux0 = u.ux;
                u.uy0 = u.uy;
                u.ux = mtmp->mx;
                u.uy = mtmp->my;
                swallowed(0);
            } else
                newsym(mtmp->mx, mtmp->my);
        }
        if(OBJ_AT(mtmp->mx, mtmp->my) && mtmp->mcanmove) {
            /* recompute the likes tests, in case we polymorphed
             * or if the "likegold" case got taken above */
            if (setlikes) {
                int pctload = (curr_mon_load(mtmp) * 100) /
                                       max_mon_load(mtmp);

                /* look for gold or jewels nearby */
                likegold = (likes_gold(ptr) && pctload < 95);
                likegems = (likes_gems(ptr) && pctload < 85);
                uses_items = (!mindless(ptr) && !is_animal(ptr)
                              && pctload < 75);
                likeobjs = (likes_objs(ptr) && pctload < 75);
                likemagic = (likes_magic(ptr) && pctload < 85);
                likerock = (throws_rocks(ptr) && pctload < 50 &&
                            !In_sokoban(&u.uz));
                breakrock = is_rockbreaker(ptr);
                conceals = hides_under(ptr);
            }

            /* Maybe a rock mole just ate some metal object */
            if (metallivorous(ptr)) {
                if (meatmetal(mtmp) == 2) return 2; /* it died */
            }

            if  (g_at(mtmp->mx, mtmp->my) && likegold) {
                /* don't let hostile monsters pick up gold in shops to prevent credit cloning abuse */
                if (!*in_rooms(mtmp->mx, mtmp->my, SHOPBASE)) {
                    mpickgold(mtmp);
                }
            }

            /* Maybe a cube ate just about anything */
            if (ptr == &mons[PM_GELATINOUS_CUBE]) {
                if (meatobj(mtmp) == 2) return 2; /* it died */
            }

            /* Because of the overloading of ALLOW_ROCK, we need
             * to ensure that for a rockbreaker mpickupstuff() gets
             * called, otherwise the monster might only walk over
             * the boulder. */
            if (breakrock) {
                boolean picked = FALSE;
                picked |= mpickstuff(mtmp, boulder_class);
                if(picked) mmoved = 3;
            }

            if(!*in_rooms(mtmp->mx, mtmp->my, SHOPBASE) || !rn2(25)) {
                boolean picked = FALSE;

                if(likeobjs) picked |= mpickstuff(mtmp, practical);
                if(likemagic) picked |= mpickstuff(mtmp, magical);
                if(likerock || breakrock) picked |= mpickstuff(mtmp, boulder_class);
                if(likegems) picked |= mpickstuff(mtmp, gem_class);
                if(uses_items) picked |= mpickstuff(mtmp, (char *)0);
                if(picked) mmoved = 3;
            }

            if(mtmp->minvis) {
                newsym(mtmp->mx, mtmp->my);
                if (mtmp->wormno) see_wsegs(mtmp);
            }
        }

        if(hides_under(ptr) || ptr->mlet == S_EEL) {
            /* Always set--or reset--mundetected if it's already hidden
               (just in case the object it was hiding under went away);
               usually set mundetected unless monster can't move.  */
            if (mtmp->mundetected ||
                (mtmp->mcanmove && !mtmp->msleeping && rn2(5)))
                mtmp->mundetected = (ptr->mlet != S_EEL) ?
                                    OBJ_AT(mtmp->mx, mtmp->my) :
                                    (is_pool(mtmp->mx, mtmp->my) && !Is_waterlevel(&u.uz));
            newsym(mtmp->mx, mtmp->my);
        }
        if (mtmp->isshk) {
            after_shk_move(mtmp);
        }
    }
    return mmoved;
}

/* The part of m_move that deals with a monster attacking another monster (and
 * that monster possibly retaliating).
 * Extracted into its own function so that it can be called with monsters that
 * have special move patterns (shopkeepers, priests, etc) that want to attack
 * other monsters but aren't just roaming freely around the level (so allowing
 * m_move to run fully for them could select an invalid move).
 * x and y are the coordinates mtmp wants to attack.
 * Return values are the same as for m_move, but this function only return 2
 * (mtmp died) or 3 (mtmp made its move).
 */
int
m_move_aggress(struct monst* mtmp, coordxy x, coordxy y)
{
    struct monst *mtmp2;
    int mstatus;

    mtmp2 = m_at(x, y);

    notonhead = mtmp2 && (x != mtmp2->mx || y != mtmp2->my);
    /* note: mstatus returns 0 if mtmp2 is nonexistent */
    mstatus = mattackm(mtmp, mtmp2);

    if (mstatus & MM_AGR_DIED) {
        /* aggressor died */
        return 2;
    }

    if ((mstatus & MM_HIT) &&
         !(mstatus & MM_DEF_DIED) &&
         rn2(4) &&
         mtmp2->movement >= NORMAL_SPEED) {
        mtmp2->movement -= NORMAL_SPEED;
        notonhead = 0;
        mstatus = mattackm(mtmp2, mtmp); /* return attack */
        if (mstatus & MM_DEF_DIED) {
            return 2;
        }
    }
    return 3;
}

boolean
closed_door(coordxy x, coordxy y)
{
    return((boolean)(IS_DOOR(levl[x][y].typ) &&
                     (levl[x][y].doormask & (D_LOCKED | D_CLOSED))));
}

boolean
accessible(coordxy x, coordxy y)
{
    int levtyp = levl[x][y].typ;

    /* use underlying terrain in front of closed drawbridge */
    if (levtyp == DRAWBRIDGE_UP) {
        levtyp = db_under_typ(&levl[x][y]);
    }
    return((boolean)(ACCESSIBLE(levl[x][y].typ) && !closed_door(x, y)));
}

/* decide where the monster thinks you are standing */
void
set_apparxy(struct monst *mtmp)
{
    boolean notseen, gotu;
    int disp, mx = mtmp->mux, my = mtmp->muy;
    long umoney = money_cnt(invent);

    /*
     * do cheapest and/or most likely tests first
     */

    /* pet knows your smell; grabber still has hold of you */
    if (mtmp->mtame || mtmp == u.ustuck) goto found_you;

    /* monsters which know where you are don't suddenly forget,
       if you haven't moved away */
    if (mx == u.ux && my == u.uy) goto found_you;

    notseen = (!mtmp->mcansee || (Invis && !perceives(mtmp->data)));
    /* add cases as required.  eg. Displacement ... */
    if (notseen || Underwater) {
        /* Xorns can smell quantities of valuable metal
            like that in solid gold coins, treat as seen */
        if ((mtmp->data == &mons[PM_XORN]) && umoney && !Underwater) {
            disp = 0;
        } else {
            disp = 1;
        }
    } else if (Displaced) {
        disp = couldsee(mx, my) ? 2 : 1;
    } else disp = 0;
    if (!disp) goto found_you;

    /* without something like the following, invis. and displ.
       are too powerful */
    gotu = notseen ? !rn2(3) : Displaced ? !rn2(4) : FALSE;

    if (!gotu) {
        int try_cnt = 0;
        do {
            if (++try_cnt > 200) goto found_you;    /* punt */
            mx = u.ux - disp + rn2(2*disp+1);
            my = u.uy - disp + rn2(2*disp+1);
        } while (!isok(mx, my)
                 || (disp != 2 && mx == mtmp->mx && my == mtmp->my)
                 || ((mx != u.ux || my != u.uy) &&
                     !passes_walls(mtmp->data) &&
                     (!ACCESSIBLE(levl[mx][my].typ) ||
                      (closed_door(mx, my) && !can_ooze(mtmp))))
                 || !couldsee(mx, my));
    } else {
found_you:
        mx = u.ux;
        my = u.uy;
    }

    mtmp->mux = mx;
    mtmp->muy = my;
}

/*
 * mon-to-mon displacement is a deliberate "get out of my way" act,
 * not an accidental bump, so we don't consider mstun or mconf in
 * undesired_disp().
 *
 * We do consider many other things about the target and its
 * location however.
 */
boolean
undesirable_disp(
    struct monst *mtmp, /**< barging creature */
    coordxy x, coordxy y) /**< spot 'mtmp' is considering moving to */
{
    boolean is_pet = (mtmp && mtmp->mtame && !mtmp->isminion);
    struct trap *trap = t_at(x, y);

    if (is_pet) {
        /* Pets avoid a trap if you've seen it usually. */
        if (trap && trap->tseen && rn2(40)) {
            return TRUE;
        }
        /* Pets avoid cursed locations */
        if (cursed_object_at(x, y)) {
            return TRUE;
        }

    /* Monsters avoid a trap if they've seen that type before */
    } else if (trap && rn2(40) &&
               (mtmp->mtrapseen & (1 << (trap->ttyp - 1))) != 0) {
        return TRUE;
    }

    /* oversimplification:  creatures that bargethrough can't swap places
       when target monster is in rock or closed door or water (in particular,
       avoid moving to spots where mondied() won't leave a corpse; doesn't
       matter whether barger is capable of moving to such a target spot if
       it were unoccupied) */
    if (!accessible(x, y) &&
        /* mondied() allows is_pool() as an exception to !accessible(),
           but we'll only do that if 'mtmp' is already at a water location
           so that we don't swap a water critter onto land */
         !(is_pool(x, y) && is_pool(mtmp->mx, mtmp->my))) {
        return TRUE;
    }

    return FALSE;
}

/*
 * Inventory prevents passage under door.
 * Used by can_ooze() and can_fog().
 */
static boolean
stuff_prevents_passage(struct monst *mtmp)
{
    struct obj *chain, *obj;

    if (mtmp == &youmonst) {
        chain = invent;
    } else {
        chain = mtmp->minvent;
    }
    for (obj = chain; obj; obj = obj->nobj) {
        int typ = obj->otyp;

        if (typ == COIN_CLASS && obj->quan > 100L) {
            return TRUE;
        }

        if (obj->oclass != GEM_CLASS &&
                !(typ >= ARROW && typ <= BOOMERANG) &&
                !(typ >= DAGGER && typ <= CRYSKNIFE) &&
                typ != SLING &&
                !is_cloak(obj) &&
                typ != FEDORA &&
                !is_gloves(obj) &&
                typ != LEATHER_JACKET &&
                typ != CREDIT_CARD &&
                !is_shirt(obj) &&
                !(typ == CORPSE && verysmall(&mons[obj->corpsenm])) &&
                typ != FORTUNE_COOKIE &&
                typ != CANDY_BAR &&
                typ != PANCAKE &&
                typ != LEMBAS_WAFER &&
                typ != LUMP_OF_ROYAL_JELLY &&
                obj->oclass != AMULET_CLASS &&
                obj->oclass != RING_CLASS &&
                obj->oclass != VENOM_CLASS &&
                typ != SACK &&
                typ != BAG_OF_HOLDING &&
                typ != BAG_OF_TRICKS &&
                !Is_candle(obj) &&
                typ != OILSKIN_SACK &&
                typ != LEASH &&
                typ != STETHOSCOPE &&
                typ != BLINDFOLD &&
                typ != TOWEL &&
                typ != TIN_WHISTLE &&
                typ != MAGIC_WHISTLE &&
                typ != MAGIC_MARKER &&
                typ != TIN_OPENER &&
                typ != SKELETON_KEY &&
                typ != LOCK_PICK) {
            return TRUE;
        }
        if (Is_container(obj) && obj->cobj) {
            return TRUE;
        }
    }
    return FALSE;
}

boolean
can_ooze(struct monst *mtmp)
{
    if (!amorphous(mtmp->data) || stuff_prevents_passage(mtmp)) {
        return FALSE;
    }
    return TRUE;
}

/* monster can change form into a fog if necessary */
boolean
can_fog(struct monst *mtmp)
{
    if (!(mvitals[PM_FOG_CLOUD].mvflags & G_GENOD) &&
            is_vampshifter(mtmp) &&
            !Protection_from_shape_changers &&
            !stuff_prevents_passage(mtmp)) {
        return TRUE;
    }
    return FALSE;
}

static int
vamp_shift(struct monst *mon, struct permonst *ptr, boolean domsg)
{
    int reslt = 0;
    char oldmtype[BUFSZ];

    /* remember current monster type before shapechange */
    Strcpy(oldmtype, domsg ? noname_monnam(mon, ARTICLE_THE) : "");

    if (mon->data == ptr) {
        /* already right shape */
        reslt = 1;
        domsg = FALSE;
    } else if (is_vampshifter(mon)) {
        reslt = newcham(mon, ptr, FALSE, FALSE);
    }

    if (reslt && domsg) {
        pline("You %s %s where %s was.",
              !canseemon(mon) ? "now detect" : "observe",
              noname_monnam(mon, ARTICLE_A), oldmtype);
        /* this message is given when it turns into a fog cloud
           in order to move under a closed door */
        display_nhwindow(WIN_MESSAGE, FALSE);
    }

    return reslt;
}

static void
make_group_attackers_flee(struct monst *mtmp)
{
    struct monst* currmon;

    /* group attackers flee together */
    /* this includes all groupers on the level but maybe it's
     * approximate enough. */
    for (currmon = fmon; currmon; currmon = currmon->nmon) {
        if (is_groupattacker(currmon->data) &&
            currmon->data == mtmp->data) {
            currmon->mflee = 1;
            currmon->mfleetim = 0;
        }
    }
}

static void
share_hp(struct monst *mon1, struct monst *mon2)
{
    struct monst *tmp;
    char nam[BUFSZ];
    int oldhp;

    Strcpy(nam, makeplural(mon1->data->mname));

    /* Share same damage with another monster. */

    if (mon1->mhp == mon2->mhp)
        return;

    if (mon1->mhp < mon2->mhp)
    {
        tmp = mon1;
        mon1 = mon2;
        mon2 = tmp;
    }

    /* Some statuses block sharing, but only if
     * that's on the contributing side.
     *
     * The contributing side does the action, the other
     * one is just a passive receiver. */
    if (mon1->mfrozen || mon1->msleeping)
        return;

    /* It doesn't always happen, only 1/2 of time. */
    if (rn2(2))
        return;

    /* If the other monster is frozen or sleeping, the
     * status is exchanged. After all, the monster with more
     * hp is more likely to survive. The woken up creature
     * is put into fleeing. */
    if (!mon2->mcanmove) {
        mon1->mcanmove = mon2->mcanmove;
        mon1->mfrozen = mon2->mfrozen;
        mon2->mcanmove = 1;
        mon2->mfrozen = 0;
        mon2->mflee = 1;
        mon2->mfleetim = 0;
        /* TODO: come up with a better message for frozen/sleeping
         * status exchange. */
        if (cansee(mon1->mx, mon1->my) &&
            cansee(mon2->mx, mon2->my)) {
            pline("The %s appear to share their wounds.", nam);
        }
        return;
    }
    if (mon2->msleeping) {
        mon1->msleeping = mon2->msleeping;
        mon2->msleeping = 0;
        mon2->mflee = 1;
        mon2->mfleetim = 0;
        if (cansee(mon1->mx, mon1->my) &&
            cansee(mon2->mx, mon2->my)) {
            pline("The %s appear to share their wounds.", nam);
        }
        return;
    }

    oldhp = mon2->mhp;
    mon2->mhp = (mon2->mhp + mon1->mhp) / 2;
    if (mon2->mhp > mon2->mhpmax)
        mon2->mhp = mon2->mhpmax;

    mon1->mhp -= (mon2->mhp - oldhp);
    if (oldhp != mon2->mhp &&
        cansee(mon1->mx, mon1->my) &&
        cansee(mon2->mx, mon2->my)) {
        pline("The %s appear to share their wounds.", nam);
    }
}

/*monmove.c*/
