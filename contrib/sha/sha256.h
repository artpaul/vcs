#ifndef CONTRIB_SHA256_H
#define CONTRIB_SHA256_H

#include "sha256/sha256.h"

#define platform_SHA256_CTX blk_SHA256_CTX
#define platform_SHA256_Init blk_SHA256_Init
#define platform_SHA256_Update blk_SHA256_Update
#define platform_SHA256_Final blk_SHA256_Final

#endif // CONTRIB_SHA256_H
