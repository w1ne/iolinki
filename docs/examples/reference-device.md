# Reference device

Use [the runnable counter/button/LED example](../../examples/reference_device/README.md)
as the common application for a new platform port. The host executable asserts
cyclic frames and data, and the unit tests cover counter wrap, button notification
and LED output. Hardware ports and their independent validation remain separate.
