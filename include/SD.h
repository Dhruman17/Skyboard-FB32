#ifndef SKYACRES_SD_STUB_H
#define SKYACRES_SD_STUB_H

// Firebase Arduino Client includes SD.h even when DISABLE_SD is set. The C6
// gateway has no SD storage; this project header intentionally satisfies only
// that unconditional include.

#endif  // SKYACRES_SD_STUB_H
