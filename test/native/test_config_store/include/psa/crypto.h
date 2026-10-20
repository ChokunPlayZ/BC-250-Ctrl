#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int psa_status_t;
typedef int psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT 0
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
static inline int psa_crypto_init(void) { return 0; }
static inline int psa_hash_setup(psa_hash_operation_t *op, int alg) { (void)op; (void)alg; return 0; }
static inline int psa_hash_update(psa_hash_operation_t *op, const uint8_t *data, size_t size) { (void)op; (void)data; (void)size; return 0; }
static inline int psa_hash_finish(psa_hash_operation_t *op, uint8_t *output, size_t size, size_t *length) { (void)op; memset(output, 0x24, size); *length = size; return 0; }
static inline int psa_hash_abort(psa_hash_operation_t *op) { (void)op; return 0; }
