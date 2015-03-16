/* NetHack 3.5	sys.h	$NHDT-Date: 1426496454 2015/03/16 09:00:54 $  $NHDT-Branch: master $:$NHDT-Revision: 1.12 $ */
/* NetHack 3.5	sys.h	$Date: 2012/01/27 20:15:26 $  $Revision: 1.9 $ */
/* Copyright (c) Kenneth Lorber, Kensington, Maryland, 2008. */
/* NetHack may be freely redistributed.  See license for details. */

#ifndef SYS_H
#define SYS_H

struct sysopt {
	char *support;	/* local support contact */
	char *recover;	/* how to run recover - may be overridden by win port */
	char *wizards;
	char *shellers;	/* like wizards, for ! command (-DSHELL) */
	char *debugfiles; /* files to show debugplines in. '*' is all. */
	int   maxplayers;
		/* record file */
	int persmax;
	int pers_is_uid;
	int entrymax;
	int pointsmin;
	int tt_oname_maxrank;
#ifdef PANICTRACE
		/* panic options */
	char *gdbpath;
	char *greppath;
	int  panictrace_gdb;
# ifdef PANICTRACE_LIBC
	int panictrace_libc;
# endif
#endif
	int seduce;
};

extern  struct sysopt sysopt;

#define SYSOPT_SEDUCE sysopt.seduce

#endif /* SYS_H */
