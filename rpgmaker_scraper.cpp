#include "rpgmaker_scraper.hpp"

#include "logger.hpp"
#include "utils.hpp"

#include <exception>
#include <fstream>
#include <iomanip>

// debug
static constexpr bool is_debugging = false;
static constexpr uint32_t debug_map_id = 5;
static constexpr uint32_t debug_event_id = 130;

using colors = logger::console_colors;

void RPGMakerScraper::load() {

    log_info(R"(verifying we're in the proper path...)");

    if (!setup_directory()) {
        throw std::logic_error("invalid root directory");
    }

    log_info(R"(populating all the map names...)");

    // grab all the map names for later use
    if (!populate_map_names()) {
        throw std::logic_error("unable to populate map names");
    }

    // handle verifying ids
    if (mode == ScrapeMode::VARIABLES) {
        log_info(R"(populating all the variable names...)");

        // grab all the variable names for later use
        if (!populate_variable_names()) {
            throw std::logic_error("unable to populate variable names");
        }

        log_info(R"(verifying variable id...)");

        if (!get_variable_name(variable_id)) {
            log_err(R"(variable #%03d doesn't exist as a predefined variable in this game!)", variable_id);
            throw std::invalid_argument("invalid variable id");
        }

        // setup variable name
        variable_name = *get_variable_name(variable_id);
    } else {
        log_err(R"(only mode supported at the moment is variables)");
        throw std::invalid_argument("unsupported scrape mode");
    }

    // scrape all maps
    log_info(R"(scraping maps...)");

    scrape_maps();
}

void RPGMakerScraper::scrape_maps() {

    for (const auto &[map_id, name] : map_info_names) {
        // allow easy debugging
        if (is_debugging && map_id != debug_map_id) {
            continue;
        }

        // give hacky visual progress
        progress_status = 
            utils::format_string(R"(scraping Map%03d...)", map_id);
        log_colored_nnl(colors::WHITE, colors::BLACK, "%s", progress_status.data());

        log_colored_nnl(colors::WHITE, colors::BLACK, "%s",
                        std::string(progress_status.length(), '\b').data());

        // check if the current map we're scraping exists
        std::filesystem::path map_file_path = root_data_path / format_map_name(map_id);
        if (!std::filesystem::exists(map_file_path)) {
            log_nopre("\n");
            log_warn(R"(map id: %03d indicates there's supposed to be a file called: '%s' but it couldn't be found!)", map_id, map_file_path.string().data());
            continue;
        }

        // open the file
        std::ifstream map_file(map_file_path);
        if (!map_file.is_open() || !map_file.good()) {
            log_nopre("\n");
            log_err(R"(unable to read '%s')", map_file_path.string().data());
            continue;
        }

        // extract the json content
        json map_json;
        map_file >> map_json;
        map_file.close();

        // verify that it contains 'events'
        if (!map_json.contains("events")) {
            log_nopre("\n");
            log_warn(R"('%s' doesn't contain events!)", map_file_path.string().data());
            continue;
        }

        // scrape the events
        for (const auto &event : map_json["events"]) {
            if (event.empty() || event.is_null()) {
                continue;
            }

            all_events[map_id].emplace_back(RPGMaker::Event{event});
        }
    }
}

void RPGMakerScraper::scrape_variables() {

    // go over every map
    for (const auto &[map_id, events] : all_events) {
        // go over every event
        for (const auto &event : events) {
            // allow easy debugging
            if (is_debugging && event.id != debug_event_id) {
                continue;
            }
            // go over event page in each event
            for (size_t page_num = 0, page_count = event.pages.size(); page_num < page_count; ++page_num) {
                const auto &page = event.pages[page_num];

                ResultInformation result_info{};
                result_info.event_page = static_cast<uint32_t>(page_num) + 1;
                result_info.event_info = event;

                if (scrape_event_page_condition(result_info, page)) {
                    results[map_id].push_back(result_info);
                }

                // now we'll take a look at each command and scrape accordingly
                for (size_t line_num = 0, line_count = page.list.size(); line_num < line_count; ++line_num) {
                    const auto &line = page.list[line_num];

                    if (line.is_if_statement()) {
                        if (scrape_command_if_statement(result_info, line)) {
                            result_info.line_number = static_cast<uint32_t>(line_num) + 1;
                            results[map_id].push_back(result_info);
                            continue;
                        }
                    }

                    if (line.is_control_variable()) {
                        if (scrape_command_control_variable(result_info, line)) {
                            result_info.line_number = static_cast<uint32_t>(line_num) + 1;
                            results[map_id].push_back(result_info);
                            continue;
                        }
                    }

                    if (line.is_script()) {
                        if (scrape_command_script(result_info, line)) {
                            result_info.line_number = static_cast<uint32_t>(line_num) + 1;
                            results[map_id].push_back(result_info);
                            continue;
                        }
                    }
                }
            }
        }
    }

    print_results();
}

bool RPGMakerScraper::setup_directory() {

    // setup our working directory in the 'data' folder of the application
    root_data_path = std::filesystem::current_path() / "data";

    // check if the 'data/' folder exists.
    if (!std::filesystem::exists(root_data_path)) {
        log_err(R"('data/' folder doesn't exist. Please drop this executable in the root directory of your RPG Maker project.)");
        return false;
    }

    return true;
}

bool RPGMakerScraper::populate_map_names() {

    constexpr const char *map_infos_file_str = "MapInfos.json";
    const std::filesystem::path map_infos_path = root_data_path / map_infos_file_str;

    if (!std::filesystem::exists(map_infos_path)) {
        log_err(R"(MapInfos.json doesn't exist inside data/. Please make sure you're in the proper folder.)");
        return false;
    }

    std::ifstream mapInfos_file(map_infos_path);
    if (mapInfos_file.bad()) {
        log_err(R"(Unable to open the mapinfos file.)");
        return false;
    }

    json map_info_json;
    mapInfos_file >> map_info_json;

    for (const auto &group : map_info_json) {
        if (group.empty() ||
            !group.contains("id") || !group["id"].is_number_integer() ||
            !group.contains("name") || !group["name"].is_string()) {
            continue;
        }

        const uint32_t map_id = group["id"].get<uint32_t>();
        const std::string_view map_name = group["name"].get<std::string_view>();

        map_info_names[map_id] = map_name;
    }

    mapInfos_file.close();

    return true;
}

bool RPGMakerScraper::populate_variable_names() {

    constexpr const char *system_file_str = "System.json";
    const std::filesystem::path system_file_path = root_data_path / system_file_str;

    if (!std::filesystem::exists(system_file_path)) {
        log_err(R"(System.json doesn't exist inside data/. Please make sure you're in the proper folder.)");
        return false;
    }

    std::ifstream system_file(system_file_path);
    if (system_file.bad()) {
        log_err(R"(Unable to open the System file.)");
        return false;
    }

    json system_json;
    system_file >> system_json;

    if (!system_json.contains("variables")) {
        log_err(R"(System.json doesn't contain variables!")");
        system_file.close();
        return false;
    }

    for (size_t var_id = 0, size = system_json["variables"].size(); var_id < size; ++var_id) {
        if (system_json["variables"][var_id].is_null()) {
            continue;
        }
        variable_names[static_cast<uint32_t>(var_id)] = system_json["variables"][var_id].get<std::string_view>();
    }

    system_file.close();

    return true;
}

std::string RPGMakerScraper::format_map_name(uint32_t id) const {

    return utils::format_string("Map%03d.json", id);
}

std::string RPGMakerScraper::format_event_page_condition(const Condition &condition) {

    return utils::format_string("IF {%s} >= %d:", variable_name.data(), condition.variable_value);
}

bool RPGMakerScraper::scrape_event_page_condition(ResultInformation &result_info, const EventPage &event_page) {

    if (event_page.conditions.variable_id != variable_id) {
        return false;
    }

    // hacky check since RPGMaker's default id is '1'.
    // so to prevent possible wrong results, we're going to ignore the ones that are 'off'
    if (variable_id == 1 && !event_page.conditions.variable_valid) {
        return false;
    }

    result_info.access_type = AccessType::READ;
    result_info.active = event_page.conditions.variable_valid;
    result_info.formatted_action = format_event_page_condition(event_page.conditions);

    return true;
}

std::string RPGMakerScraper::format_command_if_statement(const std::vector<variable_element> &parameters) {

    static const std::unordered_map<uint32_t, std::string> operator_strs = {
        {0, "="},
        {1, ">="},
        {2, "<="},
        {3, ">"},
        {4, "<"},
        {5, "!="},
    };

    const auto oper = std::get<uint32_t>(parameters[4]);

    if (oper >= operator_strs.size()) {
        log_warn("Operator was out of range!");
        return "malformed operator";
    }

    const bool being_compared_against =
        (std::get<uint32_t>(parameters[2]) == static_cast<uint32_t>(IfStatement::IDType::VARIABLE) &&
         std::get<uint32_t>(parameters[3]) == variable_id);

    std::string if_statement = "IF ";

    if (!being_compared_against) {
        if_statement +=
            utils::format_string("{%s} %s %d:", get_variable_name(variable_id)->data(),
                                 operator_strs.at(oper).data(), std::get<uint32_t>(parameters[3]));
    } else {
        if_statement +=
            utils::format_string("{#%d} %s {%s}:", std::get<uint32_t>(parameters[1]),
                                 operator_strs.at(oper).data(), get_variable_name(variable_id)->data());
    }
    return if_statement;
}

bool RPGMakerScraper::scrape_command_if_statement(ResultInformation &result_info, const Command &command) {

    constexpr uint32_t expected_param_count = 5;

    if (command.parameters.size() != expected_param_count) {
        return false;
    }

    const auto id_type = static_cast<IfStatement::IDType>(std::get<uint32_t>(command.parameters[0]));
    const auto id = std::get<uint32_t>(command.parameters[1]);
    const auto compare_type = static_cast<IfStatement::CompareType>(std::get<uint32_t>(command.parameters[2]));
    const auto compared_id = std::get<uint32_t>(command.parameters[3]);

    // check if we're in the right mode
    if (mode == ScrapeMode::VARIABLES && id_type != IfStatement::IDType::VARIABLE) {
        return false;
    }
    if (mode == ScrapeMode::SWITCHES && id_type != IfStatement::IDType::SWITCH) {
        return false;
    }

    // check if our id is used
    if (compare_type == IfStatement::CompareType::CONSTANT && id != variable_id) {
        return false;
    }
    if (compare_type == IfStatement::CompareType::VARIABLE && (id != variable_id || compared_id != variable_id)) {
        return false;
    }

    result_info.access_type = AccessType::READ;
    result_info.active = true;
    result_info.formatted_action = format_command_if_statement(command.parameters);

    return true;
}

bool RPGMakerScraper::scrape_command_control_variable(ResultInformation &result_info, const Command &command) {

    constexpr uint32_t expected_param_count_for_constant = 5;
    constexpr uint32_t expected_param_count_for_variable = 5;
    constexpr uint32_t expected_param_count_for_random = 6;

    const auto operand = static_cast<ControlVariable::Operand>(std::get<uint32_t>(command.parameters[3]));

    // verify operands match their parameter counts
    if (operand == ControlVariable::Operand::CONSTANT && command.parameters.size() != expected_param_count_for_constant) {
        return false;
    }
    if (operand == ControlVariable::Operand::VARIABLE && command.parameters.size() != expected_param_count_for_variable) {
        return false;
    }
    if (operand == ControlVariable::Operand::RANDOM && command.parameters.size() != expected_param_count_for_random) {
        return false;
    }

    const auto variable_id_start = std::get<uint32_t>(command.parameters[0]);
    const auto variable_id_end = std::get<uint32_t>(command.parameters[1]);

    const bool is_range = variable_id_start != variable_id_end;

    // check if this command is mutating our variable
    if (operand == ControlVariable::Operand::CONSTANT && variable_id_start != variable_id) {
        return false;
    }
    if (operand == ControlVariable::Operand::VARIABLE &&
        (variable_id_start != variable_id && variable_id_end != variable_id)) {
        return false;
    }
    if (operand == ControlVariable::Operand::RANDOM &&
        (variable_id_start != variable_id) || (is_range && variable_id_end != variable_id)) {
        return false;
    }
    if (operand == ControlVariable::Operand::SCRIPT) {
        const auto &script_line = std::get<std::string>(command.parameters[4]);

        return determine_access_from_script(result_info, script_line);
    }
    if (operand == ControlVariable::Operand::GAME_DATA) {
        // data doesn't pertain to variables or switches, so we don't care
        return false;
    }

    // access depends on where the variable is being used

    // if we're setting a value to our variable
    if (operand == ControlVariable::Operand::CONSTANT || operand == ControlVariable::Operand::RANDOM) {
        result_info.access_type = AccessType::WRITE;
    } else if (operand == ControlVariable::Operand::VARIABLE) {
        // if the thing we're setting another variable to is ours
        if (std::get<uint32_t>(command.parameters[4]) == variable_id) {
            result_info.access_type = AccessType::READ;
        }
        // if we're setting our variable to another variable in a range or not
        else if (variable_id_start == variable_id || (is_range && variable_id_end == variable_id)) {
            result_info.access_type = AccessType::WRITE;
        }
    }

    result_info.active = true;
    result_info.formatted_action = format_command_control_variable(command.parameters);

    return true;
}

std::string RPGMakerScraper::format_command_control_variable(const std::vector<variable_element> &parameters) {

    static const std::unordered_map<uint32_t, std::string> operation_strs = {
        {0, "="},
        {1, "+="},
        {2, "-="},
        {3, "*="},
        {4, "/="},
        {5, "%="},
    };

    const auto variable_id_start = std::get<uint32_t>(parameters[0]);
    const auto variable_id_end = std::get<uint32_t>(parameters[1]);

    const bool is_range = variable_id_start != variable_id_end;

    const auto operation = std::get<uint32_t>(parameters[3]);
    if (operation >= operation_strs.size()) {
        log_warn("Operation was out of range!");
        return "malformed operation";
    }

    const auto var_prefix = is_range ?
        utils::format_string("{%s} .. {%s}", get_variable_name(variable_id_start)->data(), get_variable_name(variable_id_end)->data()) :
        utils::format_string("{%s}", get_variable_name(variable_id_start)->data());

    const auto operand = static_cast<ControlVariable::Operand>(std::get<uint32_t>(parameters[3]));

    if (operand == ControlVariable::Operand::VARIABLE) {
        const auto variable = std::get<uint32_t>(parameters[4]);

        return utils::format_string("%s %s {%s}", var_prefix.data(), operation_strs.at(operation).data(), get_variable_name(variable)->data());
    } else if (operand == ControlVariable::Operand::CONSTANT) {
        const auto constant = std::get<uint32_t>(parameters[4]);

        return utils::format_string("%s %s %d", var_prefix.data(), operation_strs.at(operation).data(), constant);
    } else if (operand == ControlVariable::Operand::RANDOM) {
        const auto min = std::get<uint32_t>(parameters[4]);
        const auto max = std::get<uint32_t>(parameters[5]);

        return utils::format_string("%s = Random %d .. %d", var_prefix.data(), min, max);
    }

    return "unsupported";
}

bool RPGMakerScraper::scrape_command_script(ResultInformation &result_info, const Command &command) {

    constexpr uint32_t expected_param_count = 1;

    if (command.parameters.size() != expected_param_count) {
        return false;
    }

    if (!std::holds_alternative<std::string>(command.parameters[0])) {
        return false;
    }

    return determine_access_from_script(result_info, std::get<std::string>(command.parameters[0]));
}

bool RPGMakerScraper::determine_access_from_script(ResultInformation &result_info, std::string_view script_line) {

    const std::string script_read_value = utils::format_string("$gameVariables.value(%d)", variable_id);
    if (script_line.find(script_read_value) != std::string::npos) {
        result_info.access_type = AccessType::READ;
        result_info.active = true;
        result_info.formatted_action = script_line;
        return true;
    }
    const std::string script_write_value = utils::format_string("$gameVariables.setValue(%d", variable_id);
    if (script_line.find(script_write_value) != std::string::npos) {
        result_info.access_type = AccessType::WRITE;
        result_info.active = true;
        result_info.formatted_action = script_line;
        return true;
    }

    return false;
}

uint32_t RPGMakerScraper::calculate_instances() const {

    uint32_t count = 0;
    for (const auto &pair : results) {
        count += static_cast<uint32_t>(pair.second.size());
    }
    return count;
}

void RPGMakerScraper::print_results() {

    if (results.empty()) {
        log_colored(logger::console_colors::RED, logger::console_colors::BLACK, "Couldn't locate maps using RPGMaker Variable #%03d", variable_id);
        return;
    }

    log_nopre("=========================================");

    log_colored_nnl(colors::WHITE, colors::BLACK, "Found ");
    log_colored_nnl(colors::GREEN, colors::BLACK, "%d %s", results.size(), (results.size() > 1 ? "maps" : "map"));
    log_colored_nnl(colors::WHITE, colors::BLACK, " yielding ");
    log_colored_nnl(colors::GREEN, colors::BLACK, "%d separate %s ", calculate_instances(), (calculate_instances() > 1 ? "instances" : "instance"));

    if (mode == ScrapeMode::VARIABLES) {
        log_colored(colors::WHITE, colors::BLACK, "using variable #%03d (\'%s\')", variable_id, variable_name.data());
    }

    log_nopre("=========================================");

    for (const auto &[map_id, hits] : results) {
        log_colored(colors::CYAN, colors::BLACK, "\n%s ('%s')", format_map_name(map_id).data(), get_map_name(map_id)->data());
        log_colored(colors::WHITE, colors::BLACK, "--------------------------------------------------\n");
        for (const auto &hit : hits) {
            const auto &event_info = hit.event_info;

            // group similar events cleanly
            static auto latest_event_id = event_info.id;
            if (latest_event_id != event_info.id) {
                latest_event_id = event_info.id;
                if (hit != *hits.begin()) {
                    log_nopre("\n");
                }
            }

            log_colored_nnl((hit.active ? colors::DARK_GREEN : colors::DARK_GRAY), colors::BLACK, "%s",
                            (hit.active ? "ON" : "OFF"));
            log_colored((hit.access_type == AccessType::READ ? colors::BLUE : colors::RED), colors::BLACK, " [%s]", (hit.access_type == AccessType::READ ? "READ" : "WRITE"));

            log_nopre("\t@ [%d, %d] on Event #%03d ('%s') on Event Page #%02d:", event_info.x, event_info.y,
                      event_info.id, event_info.name.data(), hit.event_page);

            if (hit.line_number) {
                log_colored_nnl(colors::DARK_GRAY, colors::BLACK, "\t\tLine %03d", *hit.line_number);
                log_colored(colors::WHITE, colors::BLACK, " | %s", hit.formatted_action.data());
            } else {
                log_colored(colors::WHITE, colors::BLACK, "\t\t%s", hit.formatted_action.data());
            }
        }
    }

    log_nopre("=========================================");
}

std::ostream &operator<<(std::ostream &os, const RPGMakerScraper &scraper) {

    if (scraper.results.empty()) {
        return os;
    }

    os << "=========================================" << std::endl;

    os << utils::format_string("Found %d %s ", scraper.results.size(), (scraper.results.size() > 1 ? "maps" : "map")).data() <<
        utils::format_string("yielding %d %s ", scraper.calculate_instances(), (scraper.calculate_instances() > 1 ? "instances" : "instance")).data();

    if (scraper.mode == ScrapeMode::VARIABLES) {
        os << utils::format_string("using variable #%03d (\'%s\')", scraper.variable_id, scraper.variable_name.data()).data();
    }

    os << std::endl << "=========================================" << std::endl;

    for (const auto &[map_id, hits] : scraper.results) {
        os << std::endl;
        os << utils::format_string("%s (\'%s\')", scraper.format_map_name(map_id).data(), scraper.get_map_name(map_id)->data()) << std::endl;
        os << "--------------------------------------------------" << std::endl;
        for (const auto &hit : hits) {
            const auto &event_info = hit.event_info;

            // group similar events cleanly
            static auto latest_event_id = event_info.id;
            if (latest_event_id != event_info.id) {
                latest_event_id = event_info.id;
                if (hit != *hits.begin()) {
                    os << std::endl;
                }
            }

            os << utils::format_string("%s", (hit.active ? "ON" : "OFF")).data() <<
                utils::format_string(" [%s]", (hit.access_type == AccessType::READ ? "READ" : "WRITE")) << std::endl;

            os << utils::format_string("\t@ [%d, %d] on Event #%03d (\'%s\') on Event Page #%02d:", event_info.x, event_info.y,
                                       event_info.id, event_info.name.data(), hit.event_page) << std::endl;

            if (hit.line_number) {
                os << utils::format_string("\t\tLine %03d | %s", *hit.line_number, hit.formatted_action.data()) << std::endl;
            } else {
                os << "\t\t" << hit.formatted_action.data() << std::endl;
            }
        }
    }

    os << "=========================================" << std::endl;

    return os;
}
