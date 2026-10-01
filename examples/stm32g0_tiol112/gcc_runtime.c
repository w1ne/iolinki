/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ST GCC startup calls newlib __libc_init_array; no board constructors needed. */
void _init(void) {}
void _fini(void) {}
