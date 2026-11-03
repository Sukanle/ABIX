#ifndef AUE_COUNTER_API_H
#define AUE_COUNTER_API_H

// The public ABI of the demo native module, described to AMC so that the
// generated Lua/Aue contract is derived from real metadata rather than a
// hand-written table.
#ifdef __cplusplus
extern "C" {
#endif

struct Counter;

Counter *counter_new(int initial);
int counter_add(Counter *counter, int delta);
int counter_get(const Counter *counter);

#ifdef __cplusplus
}
#endif

#endif
