#include "rig_tests.h"
#include <stdio.h>
void rig_emit(const char *s) { puts(s); }
void rig_stage(unsigned stage) { (void)stage; }
int main(void) { return rig_run() ? 1 : 0; }
