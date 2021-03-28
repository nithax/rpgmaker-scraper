#include "logger.hpp"
#include "rpgmaker_scraper.hpp"

#include <fstream>

int main(int argc, const char *argv[]) {

	constexpr uint32_t expected_minimum_argc = 3;

	constexpr const char* search_type_variables = "-v";
	constexpr const char* search_type_switches = "-s";

	const auto print_usage = []() {
		log_err(R"(Usage: RPGMakerScraper -v 143 test_output.txt)");
	};

	// check the argument count
	if (argc < expected_minimum_argc) {
		print_usage();
		return 1;
	}
	
	const std::string search_type{ argv[1] };
	const std::string id_str{ argv[2] };

	const bool output_to_file = argc == 4;

	// check search types
	if (search_type != search_type_variables) {
		log_err(R"(invalid search type. Only '-v' for variables is supported at the moment.")");
		print_usage();
		return 1;
	}

	// search variable ids
	if (search_type == search_type_variables) {
		// make sure this id is actually a number
		if (!std::all_of(id_str.begin(), id_str.end(), isdigit)) {
			log_err(R"(invalid variable id. Please provide a number.")");
			print_usage();
			return 1;
		}

		const uint32_t variable_id = static_cast<uint32_t>(std::stoul(id_str));

		try {
			RPGMakerScraper scraper{ ScrapeMode::VARIABLES, variable_id };
			scraper.scrape_variables();

			if (output_to_file) {
				log_info(R"(writing results to %s...)", argv[3]);
				const std::string file_name{ argv[3] };
				std::ofstream file(file_name, std::ios_base::out);

				if (!file.is_open() || !file.good()) {
					throw std::invalid_argument(R"(unable to create output file)");
				}

				file << scraper;
				file.close();

				log_ok(R"(results wrote successfully.)");
			}

		} catch (const std::exception &e) {
			log_err(R"(exception caught: %s)", e.what());
		}
	}

	log_nopre("\n");
	log_ok(R"(press any key to close the program...)");

	std::cin.get();
	return 0;
}
