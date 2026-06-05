#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void register_system(void);
void register_system_common(void);
void register_system_deep_sleep(void);
void register_system_light_sleep(void);

#ifdef __cplusplus
}
#endif
