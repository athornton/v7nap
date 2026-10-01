/*
 * nap(), naphz() -- user-space wrappers for /dev/nap.  K&R C, V7 libc.
 * See nap.3.
 */

#include <nap.h>

#define	FIOCLEX	(('f'<<8)|1)	/* tty.h is a kernel header; see BUGS */

extern int errno;

static int napfd = -1;

/*
 * Open /dev/nap once and cache it.  Marked close-on-exec so children do
 * not silently inherit a descriptor they did not ask for -- V7 has only
 * NOFILE (20) per process.
 */
static
napinit()
{

	if (napfd >= 0)
		return(0);
	if ((napfd = open("/dev/nap", 0)) < 0)
		return(-1);
	ioctl(napfd, FIOCLEX, (char *)0);
	return(0);
}

nap(ticks)
int ticks;
{
	int t;

	if (napinit() < 0)
		return(-1);
	t = ticks;
	return(ioctl(napfd, NAPSLP, &t));
}

naphz()
{
	int hz;

	if (napinit() < 0)
		return(-1);
	if (ioctl(napfd, NAPGHZ, &hz) < 0)
		return(-1);
	return(hz);
}
