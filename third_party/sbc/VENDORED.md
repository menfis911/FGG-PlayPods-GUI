# libsbc

The SBC codec from BlueZ: <https://git.kernel.org/pub/scm/bluetooth/sbc.git>,
commit `b3deb8a5dcfb42d8c10ba1f2f1ac9bd7bf7271cc`, licensed LGPL-2.1-or-later
(see `COPYING.LIB`).

Only the encoder core and the generic C primitives are built.

One local change, in `sbc_primitives.c`: `sbc_init_primitives_x86()` no longer
probes the CPU with `__builtin_cpu_init()` / `__builtin_cpu_supports()`, which
need runtime support a PS5 payload does not have, so the generic C primitives
are always used.
