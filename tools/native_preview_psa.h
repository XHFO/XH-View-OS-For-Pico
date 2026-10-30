/* 宿主SHA平台边界；内容协议和逐块验证继续使用生产实现。/ Host SHA boundary; retain production content/chunk validation. */
#pragma once
#include <stddef.h>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
typedef CC_SHA256_CTX psa_hash_operation_t;
static inline int psa_hash_setup(psa_hash_operation_t *p,int algorithm){(void)algorithm;return CC_SHA256_Init(p)?0:-1;}
static inline int psa_hash_update(psa_hash_operation_t *p,const void *data,size_t n){return CC_SHA256_Update(p,data,(CC_LONG)n)?0:-1;}
static inline int psa_hash_finish(psa_hash_operation_t *p,void *out,size_t cap,size_t *n){(void)cap;*n=32;return CC_SHA256_Final(out,p)?0:-1;}
#else
#include <openssl/sha.h>
typedef SHA256_CTX psa_hash_operation_t;
static inline int psa_hash_setup(psa_hash_operation_t *p,int algorithm){(void)algorithm;return SHA256_Init(p)?0:-1;}
static inline int psa_hash_update(psa_hash_operation_t *p,const void *data,size_t n){return SHA256_Update(p,data,n)?0:-1;}
static inline int psa_hash_finish(psa_hash_operation_t *p,void *out,size_t cap,size_t *n){(void)cap;*n=32;return SHA256_Final(out,p)?0:-1;}
#endif
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
#define PSA_HASH_OPERATION_INIT {0}
static inline int psa_crypto_init(void){return 0;}
static inline int psa_hash_abort(psa_hash_operation_t *p){(void)p;return 0;}
