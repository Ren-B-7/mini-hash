#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/cli.h"
#include "include/hashing.h"
#include "include/set.h"

#define MAX_THREADS 8

static void print_hex(const unsigned char* buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		printf("%02x", buf[i]);
	}
}

static void print_available_algos(void)
{
	SimpleSet set;
	if (set_init(&set) != SET_TRUE) {
		fprintf(stderr, "mini-hash: failed to initialize algo set\n");
		return;
	}

	if (hash_list_algos(&set) != 0) {
		set_destroy(&set);
		return;
	}

	uint64_t size;
	char** names = set_to_array(&set, &size);
	for (uint64_t i = 0; i < size; i++) {
		printf("%s\n", names[i]);
		free(names[i]);
	}
	free((void*) names);

	set_destroy(&set);
}

/* Checks AF_ALG + /proc/crypto availability for the chosen algo. Returns 0 if
 * OK to proceed, 1 if a fatal problem was reported. */
static int check_algo_available(const MiniHashConfig* cfg, const char* argv0)
{
	int afalg_ok = hash_afalg_available();
	if (afalg_ok == 0) {
		fprintf(stderr,
		 "mini-hash: AF_ALG socket interface is not available on this "
		 "kernel/container\n");
		fprintf(stderr,
		 "(needs CONFIG_CRYPTO_USER_API_HASH and AF_ALG socket access; "
		 "common in restricted containers)\n");
		return 1;
	} else if (afalg_ok < 0) {
		fprintf(stderr,
		 "mini-hash: warning: could not determine AF_ALG availability, "
		 "attempting anyway\n");
	}

	int available = hash_is_available(cfg->algo);
	if (available == 0) {
		fprintf(stderr,
		 "mini-hash: algorithm '%s' is not available in this kernel\n",
		 cfg->algo);
		fprintf(stderr, "Run '%s --list-algos' to see what's supported.\n",
		 argv0);
		return 1;
	} else if (available < 0) {
		fprintf(stderr,
		 "mini-hash: warning: could not check /proc/crypto, "
		 "attempting anyway\n");
	}

	return 0;
}

/* --- String / stdin hashing (single input, no threading needed) --- */

static int hash_one_string(const char* algo, const char* str)
{
	unsigned char digest[HASH_MAX_DIGEST_SIZE];
	size_t digest_len;

	if (hash_compute(algo, (const unsigned char*) str, strlen(str), digest,
	     &digest_len) != 0) {
		return 1;
	}

	printf("%s  ", algo);
	print_hex(digest, digest_len);
	printf("  \"%s\"\n", str);
	return 0;
}

static int hash_stdin(const char* algo)
{
	unsigned char* buf = NULL;
	size_t cap = 0;
	size_t len = 0;
	size_t chunk = 65536;

	for (;;) {
		if (len + chunk > cap) {
			cap = (cap == 0) ? chunk : cap * 2;
			unsigned char* tmp = (unsigned char*) realloc(buf, cap);
			if (!tmp) {
				fprintf(stderr, "mini-hash: out of memory reading stdin\n");
				free(buf);
				return 1;
			}
			buf = tmp;
		}
		size_t n = fread(buf + len, 1, cap - len, stdin);
		len += n;
		if (n == 0) {
			break;
		}
	}

	unsigned char digest[HASH_MAX_DIGEST_SIZE];
	size_t digest_len;
	int rc = hash_compute(algo, buf, len, digest, &digest_len);
	free(buf);

	if (rc != 0) {
		return 1;
	}

	printf("%s  ", algo);
	print_hex(digest, digest_len);
	printf("  -\n");
	return 0;
}

/* --- File hashing, with a thread pool for multiple files --- */

typedef struct {
	const char* algo;
	const char* path;
	unsigned char digest[HASH_MAX_DIGEST_SIZE];
	size_t digest_len;
	int result; /* 0 = hashed, HASH_SKIP = skip, -1 = error */
} FileHashJob;

static void* file_hash_worker(void* arg)
{
	FileHashJob* job = (FileHashJob*) arg;
	job->result =
	 hash_file(job->algo, job->path, job->digest, &job->digest_len);
	return NULL;
}

static int hash_files(const char* algo, const char** paths, size_t count)
{
	FileHashJob* jobs = (FileHashJob*) calloc(count, sizeof(FileHashJob));
	if (!jobs) {
		fprintf(stderr, "mini-hash: out of memory\n");
		return 1;
	}

	for (size_t i = 0; i < count; i++) {
		jobs[i].algo = algo;
		jobs[i].path = paths[i];
	}

	if (count == 1) {
		/* No point spinning up threads for a single file. */
		file_hash_worker(&jobs[0]);
	} else {
		pthread_t threads[MAX_THREADS];
		size_t next = 0;

		while (next < count) {
			size_t batch = count - next;
			if (batch > MAX_THREADS) {
				batch = MAX_THREADS;
			}

			for (size_t i = 0; i < batch; i++) {
				pthread_create(&threads[i], NULL, file_hash_worker,
				 &jobs[next + i]);
			}
			for (size_t i = 0; i < batch; i++) {
				pthread_join(threads[i], NULL);
			}

			next += batch;
		}
	}

	int exit_code = 0;
	for (size_t i = 0; i < count; i++) {
		if (jobs[i].result == HASH_SKIP) {
			continue;
		}
		if (jobs[i].result != 0) {
			exit_code = 1;
			continue;
		}
		printf("%s  ", jobs[i].algo);
		print_hex(jobs[i].digest, jobs[i].digest_len);
		printf("  %s\n", jobs[i].path);
	}

	free(jobs);
	return exit_code;
}

int main(int argc, char** argv)
{
	MiniHashConfig cfg;
	minihash_config_init(&cfg);
	parse_cli(&cfg, argc, argv);

	int exit_code = 0;

	if (cfg.list_algos) {
		print_available_algos();
		goto done;
	}

	if (cfg.mode != INPUT_MODE_STDIN && cfg.input_count == 0) {
		fprintf(stderr, "usage: %s [--algo|-a <algorithm>] <file...>\n",
		 argv[0]);
		fprintf(stderr, "       %s --string [--algo|-a <algorithm>] <string...>\n",
		 argv[0]);
		fprintf(stderr, "       %s --stdin [--algo|-a <algorithm>]\n", argv[0]);
		fprintf(stderr, "       %s --list-algos\n", argv[0]);
		exit_code = 1;
		goto done;
	}

	if (check_algo_available(&cfg, argv[0]) != 0) {
		exit_code = 1;
		goto done;
	}

	switch (cfg.mode) {
		case INPUT_MODE_STRING:
			for (size_t i = 0; i < cfg.input_count; i++) {
				if (hash_one_string(cfg.algo, cfg.inputs[i]) != 0) {
					exit_code = 1;
				}
			}
			break;

		case INPUT_MODE_STDIN:
			exit_code = hash_stdin(cfg.algo);
			break;

		case INPUT_MODE_FILES:
		default:
			exit_code = hash_files(cfg.algo, cfg.inputs, cfg.input_count);
			break;
	}

done:
	minihash_config_destroy(&cfg);
	return exit_code;
}
