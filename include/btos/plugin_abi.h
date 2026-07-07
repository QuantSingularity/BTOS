#ifndef BTOS_PLUGIN_ABI_H
#define BTOS_PLUGIN_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTOS_PLUGIN_ABI_VERSION 1u

typedef enum btos_plugin_kind {
    BTOS_PLUGIN_SLIPPAGE = 1,
    BTOS_PLUGIN_COMMISSION = 2
} btos_plugin_kind;

typedef struct btos_slippage_vtable {
    double (*apply)(void* self, double reference_price, double quantity, int side, double adv,
                    double sigma);
} btos_slippage_vtable;

typedef struct btos_commission_vtable {
    double (*commission)(void* self, double quantity, double price, double multiplier);
} btos_commission_vtable;

typedef struct btos_plugin_manifest {
    uint32_t abi_version;
    btos_plugin_kind kind;
    const char* name;
    void* (*create)(const char* json_config);
    void (*destroy)(void* self);
    const void* vtable;
} btos_plugin_manifest;

typedef const btos_plugin_manifest* (*btos_plugin_entry_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
