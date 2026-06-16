#include "include/hashing.h"

#include <errno.h>
#include <linux/if_alg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "include/fileutils.h"

int hash_afalg_available(void)
{
	int fd = socket(AF_ALG, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		if (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT) {
			return 0;
		}
		return -1;
	}
	close(fd);
	return 1;
}

int hash_compute(const char* algo, const unsigned char* data, size_t len,
 unsigned char* out, size_t* out_len)
{
	struct sockaddr_alg sa = {0};
	sa.salg_family = AF_ALG;
	strncpy((char*) sa.salg_type, "hash", sizeof(sa.salg_type) - 1);
	strncpy((char*) sa.salg_name, algo, sizeof(sa.salg_name) - 1);

	int tfmfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
	if (tfmfd < 0) {
		perror("mini-hash: socket(AF_ALG)");
		return -1;
	}

	if (bind(tfmfd, (struct sockaddr*) &sa, sizeof(sa)) != 0) {
		fprintf(stderr, "mini-hash: unknown or unavailable algorithm '%s'\n",
		 algo);
		close(tfmfd);
		return -1;
	}

	int opfd = accept(tfmfd, NULL, 0);
	if (opfd < 0) {
		perror("mini-hash: accept");
		close(tfmfd);
		return -1;
	}

	if (len == 0) {
		/* Zero-length write signals an empty message to hash */
		if (write(opfd, "", 0) < 0) {
			perror("mini-hash: write (empty)");
			close(opfd);
			close(tfmfd);
			return -1;
		}
	} else {
		size_t remaining = len;
		const unsigned char* ptr = data;
		while (remaining > 0) {
			ssize_t n = write(opfd, ptr, remaining);
			if (n < 0) {
				perror("mini-hash: write");
				close(opfd);
				close(tfmfd);
				return -1;
			}
			ptr += (size_t) n;
			remaining -= (size_t) n;
		}
	}

	ssize_t n = read(opfd, out, HASH_MAX_DIGEST_SIZE);
	if (n < 0) {
		perror("mini-hash: read");
		close(opfd);
		close(tfmfd);
		return -1;
	}

	*out_len = (size_t) n;

	close(opfd);
	close(tfmfd);
	return 0;
}

/* Parse /proc/crypto, calling `cb` for each hash-type algorithm name found
 * (type == "shash" or "ahash"). Returns 0 on success, -1 if the file
 * couldn't be opened.
 */
static int
proc_crypto_for_each_hash(void (*cb)(const char* name, void* ctx), void* ctx)
{
	FILE* f = fopen("/proc/crypto", "r");
	if (!f) {
		perror("mini-hash: fopen(/proc/crypto)");
		return -1;
	}

	char line[256];
	char name[128] = {0};
	char type[64] = {0};

	while (fgets(line, sizeof(line), f)) {
		/* strip trailing newline */
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\n') {
			line[len - 1] = '\0';
		}

		if (line[0] == '\0') {
			/* blank line: end of a block, flush if it was a hash */
			if (name[0] != '\0' &&
			 (strcmp(type, "shash") == 0 || strcmp(type, "ahash") == 0)) {
				cb(name, ctx);
			}
			name[0] = '\0';
			type[0] = '\0';
			continue;
		}

		char key[64];
		char value[192];
		if (sscanf(line, "%63[^:] : %191[^\n]", key, value) == 2) {
			/* trim trailing whitespace from key */
			size_t klen = strlen(key);
			while (klen > 0 &&
			 (key[klen - 1] == ' ' || key[klen - 1] == '\t')) {
				key[--klen] = '\0';
			}
			if (strcmp(key, "name") == 0) {
				strncpy(name, value, sizeof(name) - 1);
			} else if (strcmp(key, "type") == 0) {
				strncpy(type, value, sizeof(type) - 1);
			}
		}
	}

	/* handle a final block with no trailing blank line */
	if (name[0] != '\0' &&
	 (strcmp(type, "shash") == 0 || strcmp(type, "ahash") == 0)) {
		cb(name, ctx);
	}

	fclose(f);
	return 0;
}

static void add_to_set(const char* name, void* ctx)
{
	SimpleSet* set = (SimpleSet*) ctx;
	set_add_str(set, name);
}

int hash_list_algos(SimpleSet* set)
{
	return proc_crypto_for_each_hash(add_to_set, set);
}

int hash_is_available(const char* algo)
{
	SimpleSet set;
	if (set_init(&set) != SET_TRUE) {
		return -1;
	}

	if (hash_list_algos(&set) != 0) {
		set_destroy(&set);
		return -1;
	}

	int present = set_contains_str(&set, algo) == SET_TRUE;
	set_destroy(&set);
	return present ? 1 : 0;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
int hash_file(const char* algo, const char* path, unsigned char* out,
 size_t* out_len)
{
	int path_type = fs_identify_path(path);
	if (path_type != FS_FILE) {
		return HASH_SKIP;
	}

	file_t f = f_init(path);
	if (f == NULL) {
		fprintf(stderr, "mini-hash: cannot open '%s': %s\n", path,
		 strerror(errno));
		return -1;
	}

	const char* buf = f_read_file(f);
	if (buf == NULL) {
		fprintf(stderr, "mini-hash: failed to read '%s': %s\n", path,
		 strerror(errno));
		f_free(f);
		return -1;
	}

	size_t size = f_filesize(f);
	int res =
	 hash_compute(algo, (const unsigned char*) buf, size, out, out_len);

	f_free(f);
	return res;
}
