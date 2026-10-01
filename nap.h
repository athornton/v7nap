/*
 * nap - sub-second delays, measured in clock ticks (1/HZ second).
 *
 * This header is shared by the kernel driver (/usr/sys/dev/nap.c) and by
 * user programs.  Install a copy in both /usr/sys/h/nap.h and
 * /usr/include/nap.h.  It must therefore not depend on any kernel
 * header: HZ lives in /usr/sys/h/param.h, which user code cannot
 * include, so the constant below is spelled out and NAPGHZ exists to
 * report the real HZ at run time.
 *
 * The letter 'n' is unused by V7's other ioctl groups ('t' tty, 'd'
 * datakit, 'f' file, 'x' mpx -- see /usr/sys/h/tty.h).
 */

#define	NAPSLP	(('n'<<8)|1)	/* arg: int *, number of ticks to sleep */
#define	NAPGHZ	(('n'<<8)|2)	/* arg: int *, gets ticks per second (HZ) */

/*
 * Longest nap accepted by NAPSLP, in ticks.  The sleep is deliberately
 * not interruptible (see nap.c), so this bound is also the longest a
 * napping process can ignore a signal.  Keep it short.  120 ticks is two
 * seconds at the HZ 60 this kernel is built with; call naphz() rather
 * than assuming that ratio.
 */
#define	NAPMAXT	120
