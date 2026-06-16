#ifndef MINIHASH_CLI_H
#define MINIHASH_CLI_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
	INPUT_MODE_FILES,  /* default: treat positional args as file paths */
	INPUT_MODE_STRING, /* --string/-s: treat positional args as literal strings
	                    */
	INPUT_MODE_STDIN   /* --stdin: read input from stdin */
} InputMode;

typedef struct {
	const char* algo;
	const char** inputs;
	size_t input_count;
	InputMode mode;
	bool list_algos;
} MiniHashConfig;

void minihash_config_init(MiniHashConfig* cfg);
int parse_cli(MiniHashConfig* cfg, int argc, char** argv);
void minihash_config_destroy(MiniHashConfig* cfg);

#endif
