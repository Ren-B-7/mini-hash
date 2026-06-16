#ifndef MINIHASH_HASHING_H
#define MINIHASH_HASHING_H

#include <stddef.h>

#include "set.h"

/* Computes the hash of `data` (length `len`) using the named kernel crypto
 * algorithm (e.g. "sha256", "sha1", "md5", "sha512") via AF_ALG.
 *
 * `out` must be large enough to hold the digest. `out_len` is set to the
 * actual digest size on success.
 *
 * Returns 0 on success, -1 on error (errno / perror reports the cause).
 */
int hash_compute(const char* algo, const unsigned char* data, size_t len,
 unsigned char* out, size_t* out_len);

/* Maximum digest size we support buffering (SHA-512 = 64 bytes). */
#define HASH_MAX_DIGEST_SIZE 64

/* Return value from hash_file when path is not a regular file (directory,
 * symlink, etc.) and should be silently skipped by the caller. */
#define HASH_SKIP 1

/* Populates `set` with the names of all hash algorithms (type "shash" or
 * "ahash") currently registered in the kernel crypto API, as reported by
 * /proc/crypto.
 *
 * `set` must already be initialized with set_init() before calling this.
 *
 * Returns 0 on success, -1 if /proc/crypto could not be read.
 */
int hash_list_algos(SimpleSet* set);

/* Returns 1 if `algo` is an available hash algorithm in the kernel crypto
 * API (per /proc/crypto), 0 if not, -1 if /proc/crypto could not be read.
 */
int hash_is_available(const char* algo);

/* Returns 1 if the AF_ALG socket interface is usable on this kernel
 * (CONFIG_CRYPTO_USER_API_HASH), 0 if AF_ALG is unsupported, -1 on an
 * unexpected error.
 */
int hash_afalg_available(void);

/* Reads the file at `path` and computes its hash using `algo` via AF_ALG.
 *
 * `out` must be large enough to hold the digest. `out_len` is set to the
 * actual digest size on success.
 *
 * Returns 0 on success, -1 on error (the file could not be read, or hashing
 * failed; errno / perror reports the cause).
 */
int hash_file(const char* algo, const char* path, unsigned char* out,
 size_t* out_len);

#endif
