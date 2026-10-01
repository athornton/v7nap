/*
 * nap -- sub-second delays for user processes.
 *
 * V7 gives user code no delay shorter than one second: sleep(3) is
 * alarm(2) plus pause(2), and alarm's granularity is seconds
 * (sys4.c sets p_clktim, which clock.c decrements once per second).
 * The kernel itself has had tick resolution all along -- clock.c walks
 * the callout list on every tick, so timeout() counts in 1/HZ seconds.
 * This driver does nothing but expose that to user mode.
 *
 * Usage, sleeping 3 ticks (50 ms at HZ 60):
 *
 *	#include <nap.h>
 *	int fd, ticks;
 *	fd = open("/dev/nap", 0);
 *	ticks = 3;
 *	ioctl(fd, NAPSLP, &ticks);
 *
 * Install as /usr/sys/dev/nap.c; see nap.3 for the user side and
 * INSTALL.md for the conf/c.c and mknod steps.
 */

#include "../h/param.h"
#include "../h/systm.h"
#include "../h/dir.h"
#include "../h/user.h"
#include "../h/conf.h"
#include "../h/nap.h"

/*
 * The nap is deliberately NOT interruptible, and that is the whole
 * reason this driver is as small as it is.
 *
 * sleep() in slp.c takes the interruptible path only when its priority
 * argument is strictly greater than PZERO; that path checks issig() and
 * leaves via resume(u.u_procp->p_addr, u.u_qsav) -- a non-local jump
 * straight out of the driver.  V7 has no untimeout() (clock.c defines
 * timeout() and nothing else), so a callout we have already scheduled
 * cannot be cancelled on the way out.  It would fire later and call
 * wakeup() on a slot the next napper might by then be using, cutting
 * that nap short for no visible reason.
 *
 * Sleeping at PZERO takes the other branch -- plain swtch(), no issig(),
 * no longjmp -- so the callout is guaranteed to fire while we are still
 * on the sleep queue, and we always return normally and tidy up.  The
 * price is that a napping process ignores signals until the nap ends,
 * which is why NAPMAXT (nap.h) keeps the maximum nap short.
 */
#define	NAPPRI	PZERO

/*
 * timeout() panics -- "Timeout table overflow" -- when callout[] fills,
 * and NCALL is only 20 for the whole machine.  /dev/nap is openable by
 * anyone, so without a cap a loop of naps across a handful of processes
 * is a user-triggerable kernel panic.  Each in-flight nap holds exactly
 * one callout entry, so capping in-flight naps caps our share of the
 * table and leaves the rest to the tty and disk drivers.
 *
 * This bounds nap's own demand; it is not a proof that callout[] cannot
 * overflow from other drivers' traffic.
 */
#define	NNAP	4		/* max concurrent naps, << NCALL */

struct	napslot {
	char	n_busy;
};
struct	napslot	napslot[NNAP];

napopen(dev, flag)
{

	if (minor(dev) != 0) {
		u.u_error = ENXIO;
		return;
	}
}

/*
 * Called from clock() at interrupt level when a nap expires.
 * Nothing but the wakeup belongs here.
 */
napwake(sp)
caddr_t sp;
{

	wakeup(sp);
}

/*
 * t is plain int, not register: copyin/copyout take its address.
 */
napioctl(dev, cmd, addr, flag)
caddr_t addr;
{
	register struct napslot *sp;
	int t;
	int s;

	switch (cmd) {

	case NAPGHZ:
		t = HZ;
		if (copyout((caddr_t)&t, addr, sizeof(t)))
			u.u_error = EFAULT;
		return;

	case NAPSLP:
		if (copyin(addr, (caddr_t)&t, sizeof(t))) {
			u.u_error = EFAULT;
			return;
		}
		if (t < 0 || t > NAPMAXT) {
			u.u_error = EINVAL;
			return;
		}
		if (t == 0)		/* nothing to wait for */
			return;
		for (sp = &napslot[0]; sp < &napslot[NNAP]; sp++)
			if (sp->n_busy == 0)
				goto found;
		u.u_error = EAGAIN;	/* too many nappers; see NNAP */
		return;
	found:
		sp->n_busy = 1;
		/*
		 * spl7 so the clock cannot deliver our own callout in the
		 * window between timeout() and sleep() queueing us -- the
		 * wakeup would find nobody and the nap would never end.
		 * sleep() drops to spl0 before swtch(), so the tick still
		 * gets through once we are safely on the queue.
		 */
		s = spl7();
		timeout(napwake, (caddr_t)sp, t);
		sleep((caddr_t)sp, NAPPRI);
		splx(s);
		sp->n_busy = 0;
		return;

	default:
		u.u_error = ENOTTY;
		return;
	}
}
