#ifndef __MAC_RESOLVING_H__
#define __MAC_RESOLVING_H__

#include <stdint.h>
#include <stdbool.h>

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

// Parses exactly "aa:bb:cc:dd:ee:ff" (either case). Returns false on anything else.
bool parseMAC(const char *str, uint8_t *mac);

#endif
