#include "logger.hpp"
#include "rpgmaker_scraper.hpp"

#include <fstream>

using colors = logger::console_colors;

void print_usage() {
    log_colored(colors::RED, colors::BLACK,
                "incorrect usage - please use the program like so:\n"
                "RPGMakerScraper -v 143 test_output.txt\n"
                "RPGMakerScraper -s 21");
}

int main(int argc, const char *argv[]) {

    constexpr uint32_t expected_minimum_argc = 3;

    constexpr const char *search_type_variables = "-v";
    constexpr const char *search_type_switches = "-s";

    // check the argument count
    if (argc < expected_minimum_argc) {
        print_usage();
        return 1;
    }

    const std::string search_type{argv[1]};
    const std::string id_str{argv[2]};

    const bool output_to_file = argc == 4;

    // make sure this id is actually a number
    if (!std::all_of(id_str.begin(), id_str.end(), isdigit)) {

        const auto get_type_name = [&](std::string search_type) -> std::string {
            static std::unordered_map<std::string, std::string> types = {
                {search_type_variables, "variable"},
                {search_type_switches, "switch"},
            };

            if (types.find(search_type) != types.end()) {
                return types.at(search_type);
            }

            return "unsupported";
        };

        log_err(R"(invalid %s id. Please provide a number.")", get_type_name(search_type).data());
        print_usage();
        return 1;
    }

    const uint32_t id = static_cast<uint32_t>(std::stoul(id_str));

    try {
        // search variable ids
        std::unique_ptr<RPGMakerScraper> scraper = nullptr;
        if (search_type == search_type_variables) {
            scraper = std::make_unique<RPGMakerScraper>(ScrapeMode::VARIABLES, id);
        } else if (search_type == search_type_switches) {
            scraper = std::make_unique<RPGMakerScraper>(ScrapeMode::SWITCHES, id);
        }

        if (scraper != nullptr) {
            scraper->scrape();

            if (output_to_file) {
                log_info(R"(writing results to %s...)", argv[3]);
                const std::string file_name{argv[3]};
                std::ofstream file(file_name, std::ios_base::out);

                if (!file.is_open() || !file.good()) {
                    throw std::invalid_argument(R"(unable to create output file)");
                }

                file << scraper;
                file.close();

                log_ok(R"(results wrote successfully.)");
            }
        }

    } catch (const std::exception &e) {
        log_err(R"(exception caught: %s)", e.what());
    }

    log_nopre("\n");
    log_ok(R"(press any key to close the program...)");

    std::cin.get();
    return 0;
}
