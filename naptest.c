/*
 * naptest -- check that /dev/nap actually delays, by how much, and that
 * it sleeps rather than spins.
 *
 * Elapsed time comes from ftime(2) (syscall 35, "formerly sleep"), the
 * only V7 call with sub-second resolution: sys4.c builds its millitm
 * from lbolt, so it genuinely advances within a second.  time(2) would
 * round every nap to a second and "prove" any length correct.  Note
 * ftime's resolution is still one tick -- 1000/HZ = 16 ms steps at
 * HZ 60 -- so a 1-tick nap is measured by a clock no finer than itself
 * and single-tick rows are necessarily noisy.
 *
 * CPU time comes from times(2), and only to tell sleeping from spinning.
 * V7's times() returns nothing useful (sys4.c copies out the four time_t
 * values and never sets u_r), so elapsed time must NOT be derived from
 * its return value -- a correct nap burns no CPU, so the tms_ figures
 * alone cannot show that a delay happened at all.  The two clocks answer
 * two different questions and both are needed.
 *
 *	cc -o naptest naptest.c libnap.c
 *	./naptest
 */

#include <nap.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <sys/times.h>

int tries[] = { 1, 2, 3, 6, 12, 30, 60, 0 };

/* elapsed milliseconds between two ftime samples */
long
msdiff(a, b)
struct timeb *a, *b;
{

	return ((long)(b->time - a->time) * 1000L
		+ (long)b->millitm - (long)a->millitm);
}

main()
{
	struct timeb f0, f1;
	struct tms c0, c1;
	long want_ms, got_ms, cpu_ticks, tick_ms;
	int hz, i, want;
	int fail;

	fail = 0;
	if ((hz = naphz()) < 0) {
		printf("naptest: naphz failed; is /dev/nap present?\n");
		exit(1);
	}
	tick_ms = 1000L / hz;
	printf("HZ = %d (%ld ms/tick)\n", hz, tick_ms);
	printf("ticks\twant\tgot\tcpu\n");

	for (i = 0; tries[i] != 0; i++) {
		want = tries[i];
		want_ms = (long)want * tick_ms;

		ftime(&f0);
		times(&c0);
		if (nap(want) < 0) {
			printf("%d\tnap failed\n", want);
			fail++;
			continue;
		}
		ftime(&f1);
		times(&c1);

		got_ms = msdiff(&f0, &f1);
		cpu_ticks = (c1.tms_utime - c0.tms_utime)
			  + (c1.tms_stime - c0.tms_stime);
		printf("%d\t%ldms\t%ldms\t%ld", want, want_ms, got_ms,
			cpu_ticks);

		/*
		 * A nap waits between want-1 and want ticks (the current
		 * tick is already partly spent), ftime itself is only
		 * tick-accurate, and the process need not be rescheduled the
		 * instant it wakes.  So allow two ticks of slack below and
		 * generous slack above.  Under simh these bounds are a
		 * guess; widen them from observed numbers rather than
		 * assuming the first failure is a driver bug.
		 */
		if (got_ms < want_ms - 2 * tick_ms)
			printf("\tTOO SHORT"), fail++;
		else if (got_ms > want_ms + 3 * tick_ms + want_ms / 4)
			printf("\tTOO LONG"), fail++;
		/*
		 * If CPU time grew with the delay we spun instead of
		 * sleeping -- exactly what this driver exists to avoid.
		 * Only meaningful once the nap is several ticks long.
		 */
		if (want >= 6 && cpu_ticks >= want / 2)
			printf("\tBUSY-WAITED"), fail++;
		printf("\n");
	}

	/* Bounds: both should be refused, not silently clamped. */
	if (nap(-1) >= 0)
		printf("negative nap accepted\n"), fail++;
	if (nap(NAPMAXT + 1) >= 0)
		printf("oversize nap accepted\n"), fail++;

	if (fail)
		printf("FAIL (%d)\n", fail);
	else
		printf("PASS\n");
	exit(fail ? 1 : 0);
}
