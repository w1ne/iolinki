#ifndef IOLINK_RIG_TESTS_H
#define IOLINK_RIG_TESTS_H
#ifdef __cplusplus
extern "C" {
#endif
int rig_run(void);
void rig_emit(const char *text);
void rig_stage(unsigned stage);
#ifdef __cplusplus
}
#endif
#endif
