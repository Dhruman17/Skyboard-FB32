#ifndef SKYACRES_SD_STUB_H
#define SKYACRES_SD_STUB_H

// The Firebase client includes <SD.h> unconditionally, but the C6 gateway
// disables all SD-backed storage. This header exists only to satisfy that
// include; no SD API is used by the gateway.

#endif  // SKYACRES_SD_STUB_H
