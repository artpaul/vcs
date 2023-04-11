/*
 * SHA1 routine optimized to do word accesses rather than byte accesses,
 * and to avoid unnecessary copies into the context array.
 *
 * This was initially based on the Mozilla SHA1 implementation, although
 * none of the original Mozilla code remains.
 */

#ifndef BLOCK_SHA1_SHA1_H
#define BLOCK_SHA1_SHA1_H

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
	unsigned long long size;
	unsigned int H[5];
	unsigned int W[16];
} blk_SHA_CTX;

void blk_SHA1_Init(blk_SHA_CTX *ctx);
void blk_SHA1_Update(blk_SHA_CTX *ctx, const void *dataIn, size_t len);
void blk_SHA1_Final(unsigned char hashout[20], blk_SHA_CTX *ctx);

#if defined(__cplusplus)
}
#endif

#endif // BLOCK_SHA1_SHA1_H
