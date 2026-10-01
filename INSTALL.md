# Installing the `/dev/nap` driver

Sub-second sleeps for V7.

V7 has no loadable modules, so this means editing the kernel source,
relinking `unix`, and rebooting. Budget an hour the first time.
You will want the `ed` man page open.

## Why this exists

V7 user code has no delay shorter than a second. `sleep(3)` is `alarm(2)`
plus `pause(2)`, and `alarm` is seconds-granular — `sys4.c` stores
`p_clktim`, which `clock.c` decrements once per second. There is no
`select`, `nap`, or `gettimeofday` in V7.

V7 *can* already **measure** sub-second time: `ftime(2)` (syscall 35,
"formerly sleep") returns `millitm` derived from `lbolt`, so it advances
within the second. It is undocumented in `man2`/`man3` — `time.2` covers
it — which is easy to miss. But measuring is not sleeping; there is still
no way to *wait* for less than a second without spinning, which is what
this driver fixes. `naptest.c` uses `ftime` as its reference clock.

The kernel has had tick resolution all along: `clock.c` walks the callout
list on *every* tick, so `timeout(fun, arg, n)` counts in 1/HZ seconds,
and `param.h` sets `HZ 60`. This driver is a window onto that and nothing
more.

## Files

| Source here | Goes to | Notes |
|---|---|---|
| `nap.c` | `/usr/sys/dev/nap.c` | the driver |
| `nap.h` | `/usr/sys/h/nap.h` **and** `/usr/include/nap.h` | deliberately kernel-header-free |
| `libnap.c` | wherever you build user programs | `nap()` / `naphz()` |
| `naptest.c` | same | verification |
| `nap.3` | `/usr/man/man3/nap.3` | |

## Steps

Do this on a **copy** of the disk image. A bad `cdevsw[]` edit yields a
kernel that panics or hangs at boot, and your only way back is the
previous image.

```sh
cp rp06-0.disk rp06-0.disk.bak
```

### 1. Install the sources

Upload `nap.c` to `/usr/sys/dev/nap.c`, and `nap.h` to
`/usr/sys/h/nap.h`.

The header file must also be at `/usr/include/nap.h`.

If `/usr/sys` and `/usr/include` are on the same filesytem, which
they very likely are, make the user header file `/usr/include/nap.h`
a link to the kernel header file `/usr/sys/h/nap.h`.

v7 does not have symbolic links, but a hardlink will do just fine.

If somehow those directories are on different filesystems you must copy
the file and keep the copies synchronized manually.

The two locations are intentional: the kernel includes
`"../h/nap.h"`, while user code includes `<nap.h>`, and V7 has no
shared location.

### 2. Add the new device to the device switch

Edit `/usr/sys/conf/c.c`. Add to the declarations near the other
character drivers:

```c
int	napopen(), napioctl();
```

Then append an entry to the **end** of `cdevsw[]`, immediately before the
terminating `0`. In the stock V7 `c.c` the last entry is `rl = 18`, so
nap becomes major device 19:

```c
	nodev, nodev, nodev, nodev, nodev, nulldev, 0, /* rl = 18 */
	napopen, nulldev, nodev, nodev, napioctl, nulldev, 0,	/* nap = 19 */
	0
};
```

Field order is `d_open, d_close, d_read, d_write, d_ioctl, d_stop,
d_ttys` (`h/conf.h`). `nulldev` succeeds silently, `nodev` sets `ENODEV`
— so read and write on `/dev/nap` fail, which is intended; the delay is
an ioctl.

**Append, do not insert.** Major numbers are positions in this array, and
every `/dev` entry on the disk already encodes one. Inserting a row
renumbers every device below it and your existing nodes then point at the
wrong drivers.

This is the part you will want the `ed` man page open for.  Don't fret:
after all, `ed` is the standard editor.

Confirm the slot you actually landed on rather than trusting the count
above — if your `c.c` has been modified, 19 may be wrong:

```sh
grep -n 'nodev\|open' /usr/sys/conf/c.c | sed -n '/cdevsw/,$p'
```

### 3. Build the device driver and relink the kernel to include it

```sh
cd /usr/sys/dev
cc -c -O nap.c
mklib                     # or: ar r LIB2 nap.o
cd ../conf
make unix
```

`make unix` runs `ld -o unix -X -i l.o mch.o c.o ../sys/LIB1 ../dev/LIB2`.
Undefined symbols at this stage mean a typo in the `c.c` declarations.

### 4. Install the kernel

```sh
cp /unix /ounix          # keep a bootable fallback
cp /usr/sys/conf/unix /unix
sync; sync
```

Then halt the simulator and reboot. If the new kernel misbehaves, boot
`hp(0,0)ounix` from the `:` prompt.

### 5. Make the device node

Major 19 (or whatever step 2 gave you), minor 0:

```sh
/etc/mknod /dev/nap c 19 0
chmod 666 /dev/nap
```

`chmod 666` lets any user nap. That is a deliberate choice and it is why
`NNAP` in the driver caps concurrent naps — see **Safety** below.

### 6. Verify

```sh
cc -o naptest naptest.c libnap.c
./naptest
```

Expect `got` within a tick or two of `want`, and `cpu` near zero for the
longer naps. `naptest` fails loudly on a nap that returns too fast (no
delay) or burns CPU proportional to its length (busy-waiting — the thing
this driver exists to avoid).

## Safety

Two kernel hazards shaped the driver; both are commented at length in
`nap.c`.

**No `untimeout()`.** V7's `clock.c` defines `timeout()` and no way to
cancel a scheduled callout. So the nap sleeps at `PZERO`, the
non-interruptible priority: `sleep()` in `slp.c` only checks `issig()`
and escapes via `resume(..., u.u_qsav)` when the priority is *greater*
than `PZERO`. A signal arriving mid-nap would otherwise jump out of the
driver leaving a live callout aimed at a slot about to be reused, cutting
some later nap short. The cost is that a nap cannot be interrupted, which
is why `NAPMAXT` caps it at two seconds.

**`NCALL` is 20.** `timeout()` panics with `"Timeout table overflow"`
when `callout[]` fills, for the whole machine — tty delays and disk
drivers share it. Since `/dev/nap` is world-accessible, an uncapped
driver would let any user panic the kernel with a loop of naps across a
few processes. `NNAP` (4) bounds nap's in-flight callouts to one each.
This bounds *nap's* share; it is not a proof that `callout[]` cannot
overflow from other traffic.

## Status

**Tested**

The above procedure has been run, and the test reports that it passed
on v7 Unix on a PDP-11/70 simulated under simh.

It has not yet been tested on UTS under VM/370.  UTS is actually the
primary target; `/dev/nap` will be used to allow unsolicited reads from
a 3270 terminal device at a frequency higher than 1Hz, which will allow
extremely responsive terminal applications under UTS (at the cost of a
great deal of (presumably emulated) CPU).

Everything about the *kernel interfaces* was read out of the V7 sources
on tuhs.org rather than recalled — `timeout()`/`clock()` in `sys/clock.c`,
`sleep()`/`wakeup()` in `sys/slp.c`, the `PZERO 25` / `NCALL 20` /
`HZ 60` constants in `h/param.h`, `struct cdevsw`'s field order in
`h/conf.h`, the `d_ioctl(dev, cmd, addr, flag)` signature in
`dev/sys.c`, the `copyin`/`copyout`-then-`EFAULT` idiom in `dev/tty.c`,
the errno values in `h/user.h`, the `timeout(vptimo, ..., HZ/10)` and
`sleep((caddr_t)&vp11, VPPRI)` style in `dev/vp.c`, and the link rule in
`conf/makefile`.

What remains genuinely unverified:

- Whether `naptest`'s tolerances hold under simh.  The simulated PDP-11
  runs far faster than real hardware between clock ticks, and `set cpu
  idle` in `boot.ini` changes how simh schedules host time. Timings may
  be tighter or looser than on a real 11/70; widen the bounds from
  observed data rather than assuming the first failure is a driver bug.
  The reported values seem accurate, but whether those match reality is
  difficult to say.  At any rate they are not so grossly wrong that
  they are perceptible to the author.
- Likewise, whether the tolerances are close to reality on UTS.  In
  addition to the emulated S/370 being far faster than a 370/168, UTS
  runs under VM, and the VM scheduler will bring in its own timing
  distortions.  Along with everything else, it appears that the CP
  timer frequency is 100Hz and UTS claims 60Hz, and 60 does not
  evenly divide 100.
- Whether `FIOCLEX` works on a non-tty fd in V7. `libnap.c` ignores its
  return value, so a failure there costs an un-cleared close-on-exec bit
  and nothing else, but the `nap.3` claim about `exec` depends on it.
  `FIOCLEX` is handled in `ioctl()` itself rather than by the driver, so
  it should apply to any descriptor, but this is unverified.
- `NNAP` 4 and `NAPMAXT` 120 are judgement calls, not measurements.
