#include "btos/plugin_abi.h"

#include <stdlib.h>
#include <string.h>

typedef struct spread_state {
    double half_spread_bps;
} spread_state;

static double parse_bps(const char* json_config) {
    if (json_config == NULL) return 5.0;
    const char* key = strstr(json_config, "half_spread_bps");
    if (key == NULL) return 5.0;
    const char* colon = strchr(key, ':');
    if (colon == NULL) return 5.0;
    char* end = NULL;
    double v = strtod(colon + 1, &end);
    if (end == colon + 1) return 5.0;
    return v;
}

static double spread_apply(void* self, double reference_price, double quantity, int side,
                           double adv, double sigma) {
    spread_state* s = (spread_state*)self;
    (void)quantity;
    (void)adv;
    (void)sigma;
    return reference_price * (1.0 + (double)side * s->half_spread_bps * 1e-4);
}

static void* spread_create(const char* json_config) {
    spread_state* s = (spread_state*)malloc(sizeof(spread_state));
    if (s == NULL) return NULL;
    s->half_spread_bps = parse_bps(json_config);
    return s;
}

static void spread_destroy(void* self) { free(self); }

static const btos_slippage_vtable kSpreadVtable = {spread_apply};

static const btos_plugin_manifest kSpreadManifest = {
    BTOS_PLUGIN_ABI_VERSION, BTOS_PLUGIN_SLIPPAGE, "spread_slippage",
    spread_create, spread_destroy, &kSpreadVtable};

const btos_plugin_manifest* btos_plugin_manifest_v1(void) { return &kSpreadManifest; }
