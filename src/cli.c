#include "include/cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/minicli.h"

void minihash_config_init(MiniHashConfig* cfg)
{
	cfg->algo = "sha256";
	cfg->inputs = NULL;
	cfg->input_count = 0;
	cfg->mode = INPUT_MODE_FILES;
	cfg->list_algos = false;
}

void minihash_config_destroy(MiniHashConfig* cfg)
{
	free((void*) cfg->inputs);
	cfg->inputs = NULL;
	cfg->input_count = 0;
}

static int handle_algo(int argc, char** argv, void* user_data)
{
	MiniHashConfig* cfg = (MiniHashConfig*) user_data;
	if (argc > 0) {
		cfg->algo = argv[0];
		return 1;
	}
	fprintf(stderr, "mini-hash: --algo requires an argument\n");
	return 0;
}

static int handle_list_algos(int argc, char** argv, void* user_data)
{
	(void) argc;
	(void) argv;
	MiniHashConfig* cfg = (MiniHashConfig*) user_data;
	cfg->list_algos = true;
	return 0;
}

static int handle_string(int argc, char** argv, void* user_data)
{
	(void) argc;
	(void) argv;
	MiniHashConfig* cfg = (MiniHashConfig*) user_data;
	cfg->mode = INPUT_MODE_STRING;
	return 0;
}

static int handle_stdin(int argc, char** argv, void* user_data)
{
	(void) argc;
	(void) argv;
	MiniHashConfig* cfg = (MiniHashConfig*) user_data;
	cfg->mode = INPUT_MODE_STDIN;
	return 0;
}

int parse_cli(MiniHashConfig* cfg, int argc, char** argv)
{
	CliParser parser;
	cli_init(&parser,
	 (CliInitParams) {"mini-hash", "Compute hashes via the Linux kernel crypto API"});

	cli_add_argument(&parser,
	 (CliArgument) {"--algo", "-a", "Hash algorithm to use (default: sha256)",
	     handle_algo, cfg});
	cli_add_argument(&parser,
	 (CliArgument) {"--list-algos", "-l",
	     "List hash algorithms available in the running kernel",
	     handle_list_algos, cfg});
	cli_add_argument(&parser,
	 (CliArgument) {"--string", "-s",
	     "Treat arguments as literal strings instead of file paths",
	     handle_string, cfg});
	cli_add_argument(&parser,
	 (CliArgument) {"--stdin", NULL, "Read input from stdin", handle_stdin, cfg});

	int first_non_option = cli_parse(&parser, argc, argv);

	/* Collect all remaining non-option arguments into a single-pass.
	 * Allocate the max possible (argc - first_non_option) up front so
	 * the analyser can see idx is always < capacity, then realloc to
	 * the actual count. */
	if (first_non_option < argc) {
		int remaining = argc - first_non_option;
		const char** inputs =
		 (const char**) malloc(sizeof(char*) * (size_t) remaining);
		if (inputs) {
			size_t count = 0;
			for (int i = first_non_option; i < argc; i++) {
				if (argv[i][0] != '-' || strcmp(argv[i], "-") == 0) {
					inputs[count++] = argv[i];
				}
			}
			if (count > 0) {
				const char** trimmed = (const char**) realloc((void*) inputs,
				 sizeof(char*) * count);
				cfg->inputs = trimmed ? trimmed : inputs;
				cfg->input_count = count;
			} else {
				free((void*) inputs);
			}
		}
	}

	cli_destroy(&parser);
	return 0;
}
