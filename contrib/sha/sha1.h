#ifndef CONTRIB_SHA1_H
#define CONTRIB_SHA1_H

#include "sha1/sha1.h"

#define platform_SHA_CTX  blk_SHA_CTX
#define platform_SHA1_Init blk_SHA1_Init
#define platform_SHA1_Update blk_SHA1_Update
#define platform_SHA1_Final blk_SHA1_Final

#endif // CONTRIB_SHA1_H
