/* SPDX-License-Identifier: Apache-2.0
 * 中文：主机预览的素材校验边界，固件使用本身的mbedTLS实现。
 * English: Host asset-digest adapter; firmware uses its existing mbedTLS implementation.
 */
#include <stddef.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
int mbedtls_sha256(const unsigned char *p,size_t n,unsigned char out[32],int is224){
    return is224||n>0xffffffffu||!CC_SHA256(p,(CC_LONG)n,out);
}
#else
#include <openssl/sha.h>
int mbedtls_sha256(const unsigned char *p,size_t n,unsigned char out[32],int is224){
    return is224||!SHA256(p,n,out);
}
#endif
