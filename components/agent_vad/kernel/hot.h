#ifndef TEN_HOT_H
#define TEN_HOT_H

/* Only the isolated C3 probe opts in. No weights or buffers move into IRAM. */
#if defined(TEN_HOT_CODE) && defined(TEN_DEVICE)
#include "esp_attr.h"
#define TEN_HOT IRAM_ATTR
#else
#define TEN_HOT
#endif

#endif
