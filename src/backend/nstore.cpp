/*-------------------------------------------------------------------------
 *
 * main.cc
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /n-store/src/main.cc
 *
 *-------------------------------------------------------------------------
 */

#include "nstore.h"

namespace nstore {

static void usage_exit() {
	std::cerr << "CLI usage : nstore <args> \n"
			<< "   -h --help       				:  Print help message \n"
			<< "   -s --storage_manager_tests   	:  Test storage manager \n"
			<< "   -f --filesystem-path    		:  Path for Filesystem \n";

	exit(EXIT_FAILURE);
}

static struct option opts[] = {
		{ "storage_manager_tests", no_argument, NULL, 's' },
		{ "filesystem-path", optional_argument, NULL, 'f' },
		{ NULL, 0, NULL, 0 }
};

static void parse_arguments(int argc, char* argv[], configuration& config) {
	// Parse arguments

	while (1) {
		int idx = 0;
		int c = getopt_long(argc, argv, "fsh", opts, &idx);

		if (c == -1)
			break;

		switch (c) {

		case 'f':
			config.filesystem_path = std::string(optarg);
			std::cout << "filesystem_path        :: " << config.filesystem_path << std::endl;
			break;

		case 's':
			config.storage_manager_tests = true;
			std::cout << "storage_manager_tests  :: ENABLED" << std::endl;
			break;

		case 'h':
			usage_exit();
			break;

		default:
			std::cerr <<"unknown option: --" << (char) c << "-- \n";
			usage_exit();
		}
	}
}

}

int main(int argc, char **argv) {
	const char* path = "/mnt/pmfs/n-store/zfile";

	nstore::configuration state;

	nstore::parse_arguments(argc, argv, state);

	//storage::coordinator cc(state);

	return 0;
}
