/* shim-sea's sources use <stdint.h>; under EDK2 the same types come from Base.h */
#ifndef SEA_COMPAT_STDINT_H
#define SEA_COMPAT_STDINT_H
#include <Base.h>
typedef UINT8   uint8_t;
typedef UINT16  uint16_t;
typedef UINT32  uint32_t;
typedef UINT64  uint64_t;
typedef UINTN   uintptr_t;
#endif
