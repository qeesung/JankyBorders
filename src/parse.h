#pragma once
#include "border.h"

#define BORDER_UPDATE_MASK_ACTIVE   (1 << 0)
#define BORDER_UPDATE_MASK_INACTIVE (1 << 1)
#define BORDER_UPDATE_MASK_ALL      (BORDER_UPDATE_MASK_ACTIVE \
                                     | BORDER_UPDATE_MASK_INACTIVE)

#define BORDER_UPDATE_MASK_RECREATE_ALL (1 << 2)
#define BORDER_UPDATE_MASK_SETTING  (1 << 3)


uint32_t parse_settings(struct settings* settings, int count, char** arguments);
uint32_t parse_settings_override(struct settings* settings,
                                 int count,
                                 char** arguments);
bool parse_settings_contains_global_filter(int count, char** arguments);
bool parse_settings_contains_global_control(int count, char** arguments);
uint32_t parse_settings_apply_target(int count, char** arguments);
bool parse_settings_scope_is_valid(int count, char** arguments);
