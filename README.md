# v7nap

Sub-second sleeps for Unix v7, implemented as a character-mode
device driver and a userspace library.

## Rationale

Although v7 has a tick granularity of 60Hz (nominally), there is no way
for user code to pause less than a second.  `sleep(3)` relies on the
combination of `alarm(2)` and `pause(2)`, and `alarm` has a granularity
of seconds.

Thus, unless you have a nap driver of some sort, you cannot poll
anything with a frequency of less than a second, unless you resort to
busy-waiting.

The ultimate goal here is not actually v7 on the PDP-11; it is UTS for
VM/370. In order to make a highly responsive 3270 terminal application,
polling the user's terminal more frequently than once a second is
required.
