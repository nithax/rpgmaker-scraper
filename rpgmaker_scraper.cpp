#include "rpgmaker_scraper.hpp"

#include "logger.hpp"
#include "utils.hpp"

#include <exception>
#include <fstream>
#include <iomanip>

// lazy debug, set an id to UINT_MAX if you want to ignore it
static constexpr bool is_debugging = false;
static constexpr uint32_t debug_map_id = UINT_MAX;
static constexpr uint32_t debug_event_id = UINT_MAX;

using colors = logger::console_colors;
using access_color = std::pair<std::string, colors>;

static access_color get_access_info(const AccessType &access_type) {
    static std::unordered_map<AccessType, access_color> info = {
        {AccessType::NONE, {"NONE", colors::GRAY}},
        {AccessType::READ, {"READ", colors::BLUE}},
        {AccessType::READWRITE, {"READWRITE", colors::MAGENTA}},
        {AccessType::WRITE, {"WRITE", colors::RED}},
    };

    if (info.find(access_type) != info.end()) {
        return info.at(access_type);
    }

    return info.at(AccessType::NONE);
}

std::optional<std::string> RPGMakerScraper::get_map_name(uint32_t id) const {

    if (map_info_names.empty() || map_info_names.find(id) == map_info_names.end()) {
        return std::nullopt;
    }
    return map_info_names.at(id);
}

std::optional<std::string> RPGMakerScraper::get_variable_name(uint32_t id) const {

    if (id == 0 || variable_names.empty() || variable_names.find(id) == variable_names.end()) {
        return std::nullopt;
    }

    const std::string &name = variable_names.at(id);

    if (name.empty()) {
        return std::string("#") + std::to_string(id);
    }

    return name;
}

std::optional<std::string> RPGMakerScraper::get_switch_name(uint32_t id) const {

    if (switch_names.empty()) {
        return std::nullopt;
    }

    if (switch_names.find(id) == switch_names.end()) {
        return std::string("#") + std::to_string(id) + " ?";
    }

    const std::string &name = switch_names.at(id);

    if (name.empty()) {
        return std::string("#") + std::to_string(id);
    }

    return name;
}

std::optional<std::string> RPGMakerScraper::get_common_event_name(uint32_t id) const {

    if (id == 0 || common_event_names.empty() || common_event_names.find(id) == common_event_names.end()) {
        return std::nullopt;
    }

    return common_event_names.at(id);
}

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

    log_info(R"(populating all the names...)");

    // grab all the variable or switch names for later use
    if (!populate_names()) {
        throw std::logic_error("unable to populate names");
    }

    if (mode == ScrapeMode::VARIABLES) {
        log_info(R"(verifying variable id...)");

        if (!get_variable_name(query_id)) {
            log_err(R"(variable #%03d doesn't exist as a predefined variable in this game!)", query_id);
            throw std::invalid_argument("invalid variable id");
        }

        // setup variable name
        variable_name = *get_variable_name(query_id);
    } else if (mode == ScrapeMode::SWITCHES) {
        log_info(R"(verifying switch id...)");

        if (!get_switch_name(query_id)) {
            log_err(R"(switch #%03d doesn't exist as a predefined switch in this game!)", query_id);
            throw std::invalid_argument("invalid switch id");
        }

        // setup variable name
        switch_name = *get_switch_name(query_id);
    }

    // scrape all maps
    log_info(R"(scraping maps...)");

    scrape_maps();

    log_info(R"(scraping common events...)");

    scrape_common_events();
}

void RPGMakerScraper::scrape_maps() {

    for (const auto &[map_id, name] : map_info_names) {
        // allow easy debugging
        if (is_debugging && (debug_map_id != UINT_MAX && map_id != debug_map_id)) {
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

bool RPGMakerScraper::scrape_common_events() {

    constexpr const char *common_events_file_str = "CommonEvents.json";
    const std::filesystem::path common_events_path = root_data_path / common_events_file_str;

    if (!std::filesystem::exists(common_events_path)) {
        log_err(R"(CommonEvents.json doesn't exist inside data/. Please make sure you're in the proper folder.)");
        return false;
    }

    std::ifstream common_events_file(common_events_path);
    if (!common_events_file.is_open() || !common_events_file.good()) {
        log_err(R"(Unable to open the CommonEvents file.)");
        return false;
    }

    json common_events_json;
    common_events_file >> common_events_json;
    common_events_file.close();

    for (const auto &common_event : common_events_json) {
        if (common_event.empty()) {
            continue;
        }

        all_common_events.emplace_back(CommonEvent(common_event));

        // grab the common event names in one shot
        auto &back = all_common_events.back();
        common_event_names[back.id] = back.name;
    }

    return true;
}

void RPGMakerScraper::scrape() {

    // go over every map
    for (const auto &[map_id, events] : all_events) {
        // go over every event
        for (const auto &event : events) {
            // allow easy debugging
            if (is_debugging && (debug_event_id != UINT_MAX && event.id != debug_event_id)) {
                continue;
            }
            // go over event page in each event
            for (size_t page_num = 0, page_count = event.pages.size(); page_num < page_count; ++page_num) {
                const auto &page = event.pages[page_num];

                std::shared_ptr<MapEventResult> result_info = std::make_shared<MapEventResult>();
                result_info->event_page = static_cast<uint32_t>(page_num) + 1;
                result_info->event_info = event;

                auto downcasted_result_info = std::dynamic_pointer_cast<ResultInformationBase>(result_info);

                if (scrape_event_page_condition(downcasted_result_info, page)) {
                    results[map_id].push_back(result_info);
                }

                // now we'll take a look at each command and scrape accordingly
                for (size_t line_num = 0, line_count = page.list.size(); line_num < line_count; ++line_num) {
                    const auto &line = page.list[line_num];

                    if (line.is_if_statement()) {
                        if (scrape_command_if_statement(downcasted_result_info, line)) {
                            result_info->line_number = static_cast<uint32_t>(line_num) + 1;
                            results[map_id].push_back(result_info);
                            continue;
                        }
                    }

                    if (line.is_control_variable()) {
                        if (scrape_command_control_variable(downcasted_result_info, line)) {
                            result_info->line_number = static_cast<uint32_t>(line_num) + 1;
                            results[map_id].push_back(result_info);
                            continue;
                        }
                    }

                    if (line.is_control_switch()) {
                        if (scrape_command_control_switch(downcasted_result_info, line)) {
                            result_info->line_number = static_cast<uint32_t>(line_num) + 1;
                            results[map_id].push_back(result_info);
                            continue;
                        }
                    }

                    if (line.is_script()) {
                        if (scrape_command_script(downcasted_result_info, line)) {
                            result_info->line_number = static_cast<uint32_t>(line_num) + 1;
                            results[map_id].push_back(result_info);
                            continue;
                        }
                    }
                }
            }
        }
    }

    const bool check_for_switches = mode == ScrapeMode::SWITCHES;
    // go over every common event
    for (const auto &common_event : all_common_events) {

        // check for switches
        if (check_for_switches && common_event.has_trigger()) {
            auto result_info = std::make_shared<ResultInformationBase>();

            if (scrape_common_event_trigger(result_info, common_event)) {
                result_info->name = common_event.name;
                common_event_results[common_event.id].push_back(result_info);
            }
        }

        auto &command_list = common_event.list;
        for (size_t line_num = 0, line_count = command_list.size(); line_num < line_count; ++line_num) {
            const auto &line = command_list[line_num];

            auto result_info = std::make_shared<ResultInformationBase>();

            if (line.is_if_statement()) {
                if (scrape_command_if_statement(result_info, line)) {
                    result_info->line_number = static_cast<uint32_t>(line_num) + 1;
                    result_info->name = common_event.name;
                    common_event_results[common_event.id].push_back(result_info);
                    continue;
                }
            }

            if (line.is_control_variable()) {
                if (scrape_command_control_variable(result_info, line)) {
                    result_info->line_number = static_cast<uint32_t>(line_num) + 1;
                    result_info->name = common_event.name;
                    common_event_results[common_event.id].push_back(result_info);
                    continue;
                }
            }

            if (line.is_control_switch()) {
                if (scrape_command_control_switch(result_info, line)) {
                    result_info->line_number = static_cast<uint32_t>(line_num) + 1;
                    result_info->name = common_event.name;
                    common_event_results[common_event.id].push_back(result_info);
                    continue;
                }
            }

            if (line.is_script()) {
                if (scrape_command_script(result_info, line)) {
                    result_info->line_number = static_cast<uint32_t>(line_num) + 1;
                    result_info->name = common_event.name;
                    common_event_results[common_event.id].push_back(result_info);
                    continue;
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
    if (!mapInfos_file.is_open() || !mapInfos_file.good()) {
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

bool RPGMakerScraper::populate_names() {

    constexpr const char *system_file_str = "System.json";
    const std::filesystem::path system_file_path = root_data_path / system_file_str;

    if (!std::filesystem::exists(system_file_path)) {
        log_err(R"(System.json doesn't exist inside data/. Please make sure you're in the proper folder.)");
        return false;
    }

    std::ifstream system_file(system_file_path);
    if (!system_file.is_open() || !system_file.good()) {
        log_err(R"(Unable to open the System file.)");
        return false;
    }

    json system_json;
    system_file >> system_json;

    if (mode == ScrapeMode::VARIABLES) {
        if (!system_json.contains("variables")) {
            log_err(R"(System.json doesn't contain variables!")");
            system_file.close();
            return false;
        }

        for (size_t var_id = 0, size = system_json["variables"].size(); var_id < size; ++var_id) {
            const auto &variable = system_json["variables"][var_id];
            if (variable.is_null()) {
                continue;
            }
            variable_names[static_cast<uint32_t>(var_id)] = variable.get<std::string_view>();
        }
    } else if (mode == ScrapeMode::SWITCHES) {
        if (!system_json.contains("switches")) {
            log_err(R"(System.json doesn't contain switches!")");
            system_file.close();
            return false;
        }

        for (size_t switch_id = 0, size = system_json["switches"].size(); switch_id < size; ++switch_id) {
            const auto &_switch = system_json["switches"][switch_id];
            if (_switch.is_null()) {
                continue;
            }
            switch_names[static_cast<uint32_t>(switch_id)] = _switch.get<std::string_view>();
        }
    }

    system_file.close();

    return true;
}

bool RPGMakerScraper::has_results() const {
    return !results.empty() || !common_event_results.empty();
}

std::string RPGMakerScraper::format_map_name(uint32_t id) const {

    return utils::format_string("Map%03d.json", id);
}

std::string RPGMakerScraper::format_event_page_condition(const Condition &condition) {

    if (mode == ScrapeMode::VARIABLES) {
        return utils::format_string("IF {%s} >= %d:", variable_name.data(), condition.variable_value);
    } else if (mode == ScrapeMode::SWITCHES) {
        std::string ret = "IF ";
        // this is messy af but this is the cleanest way to represent this
        // rpgmaker allows statements when switch2 is only valid for some god awful reason..
        if (condition.switch1_valid && condition.switch2_valid) {
            ret += utils::format_string("{%s} && {%s}",
                                        (condition.switch1_valid ? get_switch_name(condition.switch1_id)->data() :
                                         get_switch_name(condition.switch2_id)->data()),
                                        (condition.switch2_valid ? get_switch_name(condition.switch2_id)->data() :
                                         get_switch_name(condition.switch1_id)->data()));
        } else if (condition.switch1_valid) {
            ret += utils::format_string("{%s}", get_switch_name(condition.switch1_id)->data());
        } else if (condition.switch2_valid) {
            ret += utils::format_string("{%s}", get_switch_name(condition.switch2_id)->data());
        }
        ret += ":";
        return ret;
    }

    return unsupported;
}

bool RPGMakerScraper::scrape_event_page_condition(std::shared_ptr<ResultInformationBase> result_info, const EventPage &event_page) {

    if (mode == ScrapeMode::VARIABLES) {
        if (event_page.conditions.variable_id != query_id) {
            return false;
        }

        // hacky check since RPGMaker's default id is '1'.
        // so to prevent possible wrong results, we're going to ignore the ones that are 'off'
        if (query_id == 1 && !event_page.conditions.variable_valid) {
            return false;
        }

        result_info->active = event_page.conditions.variable_valid;

    } else if (mode == ScrapeMode::SWITCHES) {
        if (event_page.conditions.switch1_id != query_id &&
            event_page.conditions.switch2_id != query_id) {
            return false;
        }

        if (query_id == 1 &&
            (!event_page.conditions.switch1_valid &&
             !event_page.conditions.switch2_valid)) {
            return false;
        }

        result_info->active = (event_page.conditions.switch1_valid || event_page.conditions.switch2_valid);
    }

    result_info->access_type = AccessType::READ;
    result_info->formatted_action = format_event_page_condition(event_page.conditions);

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

    std::string if_statement = "If: ";

    if (mode == ScrapeMode::VARIABLES) {
        const bool being_compared_against =
            (std::get<uint32_t>(parameters[2]) == static_cast<uint32_t>(IfStatement::IDType::VARIABLE) &&
             std::get<uint32_t>(parameters[3]) == query_id);

        const auto oper = std::get<uint32_t>(parameters[4]);

        if (oper >= operator_strs.size()) {
            log_warn("Operator was out of range!");
            return "malformed operator";
        }

        if (!being_compared_against) {
            if_statement +=
                utils::format_string("{%s} %s %d:", get_variable_name(query_id)->data(),
                                     operator_strs.at(oper).data(), std::get<uint32_t>(parameters[3]));
        } else {
            if_statement +=
                utils::format_string("{#%d} %s {%s}:", std::get<uint32_t>(parameters[1]),
                                     operator_strs.at(oper).data(), get_variable_name(query_id)->data());
        }
    } else if (mode == ScrapeMode::SWITCHES) {
        const auto switch_compared = std::get<uint32_t>(parameters[1]);
        if_statement +=
            utils::format_string("{%s} is %s", get_switch_name(switch_compared)->data(),
                                 (std::get<uint32_t>(parameters[2]) ? "OFF" : "ON"));

    }

    return if_statement;
}

bool RPGMakerScraper::scrape_command_if_statement(std::shared_ptr<ResultInformationBase> result_info, const Command &command) {

    constexpr uint32_t expected_variable_param_count = 5;
    constexpr uint32_t expected_switch_param_count = 3;
    constexpr uint32_t expected_script_param_count = 2;

    const auto param_count = command.parameters.size();

    const auto id_type = static_cast<IfStatement::IDType>(std::get<uint32_t>(command.parameters[0]));

    if (id_type == IfStatement::IDType::SCRIPT) {
        if (param_count != expected_script_param_count) {
            return false;
        }

        return determine_access_from_script(result_info, std::get<std::string>(command.parameters[1]));
    }

    // check if we're in the right mode
    if (mode == ScrapeMode::VARIABLES) {
        if (id_type != IfStatement::IDType::VARIABLE || param_count != expected_variable_param_count) {
            return false;
        }

        const auto compare_type = static_cast<IfStatement::CompareType>(std::get<uint32_t>(command.parameters[2]));
        const auto compared_id = std::get<uint32_t>(command.parameters[3]);
        const auto id = std::get<uint32_t>(command.parameters[1]);

        // check if our id is used
        if (compare_type == IfStatement::CompareType::CONSTANT && id != query_id) {
            return false;
        }
        if (compare_type == IfStatement::CompareType::VARIABLE && (id != query_id || compared_id != query_id)) {
            return false;
        }
    } else if (mode == ScrapeMode::SWITCHES) {
        if (id_type != IfStatement::IDType::SWITCH || param_count != expected_switch_param_count) {
            return false;
        }

        const auto id = std::get<uint32_t>(command.parameters[1]);

        if (id != query_id) {
            return false;
        }
    }

    result_info->access_type = AccessType::READ;
    result_info->active = true;
    result_info->formatted_action = format_command_if_statement(command.parameters);

    return true;
}

bool RPGMakerScraper::scrape_command_control_variable(std::shared_ptr<ResultInformationBase> result_info, const Command &command) {
    if (mode != ScrapeMode::VARIABLES) {
        return false;
    }

    constexpr uint32_t expected_param_count_for_constant = 5;
    constexpr uint32_t expected_param_count_for_variable = 5;
    constexpr uint32_t expected_param_count_for_random = 6;

    const auto operand = static_cast<ControlVariable::Operand>(std::get<uint32_t>(command.parameters[3]));
    const auto param_size = command.parameters.size();

    // verify operands match their parameter counts
    if (operand == ControlVariable::Operand::CONSTANT && param_size != expected_param_count_for_constant) {
        return false;
    }
    if (operand == ControlVariable::Operand::VARIABLE && param_size != expected_param_count_for_variable) {
        return false;
    }
    if (operand == ControlVariable::Operand::RANDOM && param_size != expected_param_count_for_random) {
        return false;
    }

    const auto variable_id_start = std::get<uint32_t>(command.parameters[0]);
    const auto variable_id_end = std::get<uint32_t>(command.parameters[1]);

    const bool is_range = variable_id_start != variable_id_end;
    // RPGMaker actually does query_id <= end to include the range
    const bool is_within_range = query_id >= variable_id_start && query_id <= variable_id_end;

    // script access is determined if the value is present in the line of script
    if (operand == ControlVariable::Operand::SCRIPT) {
        const auto &script_line = std::get<std::string>(command.parameters[4]);

        return determine_access_from_script(result_info, script_line);
    }
    // data doesn't pertain to variables or switches, so we don't care
    if (operand == ControlVariable::Operand::GAME_DATA) {
        return false;
    }

    // access depends on where the variable is being used

    // if we're setting a value to our variable
    if (operand == ControlVariable::Operand::CONSTANT || operand == ControlVariable::Operand::RANDOM) {
        // make sure if we're dealing with the query_id
        if ((!is_range && variable_id_start != query_id) || (is_range && !is_within_range)) {
            return false;
        }
        result_info->access_type = AccessType::WRITE;
    } else if (operand == ControlVariable::Operand::VARIABLE) {
        // if the thing we're setting another variable to is ours
        if (std::get<uint32_t>(command.parameters[4]) == query_id) {
            // support weird commands that are reading and writing the same variable(s)..
            result_info->access_type = is_within_range ? AccessType::READWRITE : AccessType::READ;
        } else if (is_within_range) {
            // if we're setting our variable to another variable in a range or not
            result_info->access_type = AccessType::WRITE;
        } else {
            // this doesn't deal with the query id at all
            return false;
        }
    }

    result_info->active = true;
    result_info->formatted_action = format_command_control_variable(command.parameters);

    return true;
}

bool RPGMakerScraper::scrape_command_control_switch(std::shared_ptr<ResultInformationBase> result_info, const Command &command) {
    if (mode != ScrapeMode::SWITCHES) {
        return false;
    }

    constexpr uint32_t expected_param_count = 3;

    if (command.parameters.size() != expected_param_count) {
        return false;
    }

    const auto switch_id_start = std::get<uint32_t>(command.parameters[0]);
    const auto switch_id_end = std::get<uint32_t>(command.parameters[1]);

    const bool is_range = switch_id_start != switch_id_end;
    // RPGMaker actually does i <= end to include the range
    const bool is_within_range = query_id >= switch_id_start && query_id <= switch_id_end;

    if ((!is_range && switch_id_start != query_id) || (is_range && !is_within_range)) {
        return false;
    }

    result_info->access_type = AccessType::WRITE;
    result_info->active = true;
    result_info->formatted_action = format_command_control_switch(command.parameters);

    return true;
}

std::string RPGMakerScraper::format_command_control_switch(const std::vector<variable_element> &parameters) {

    const auto switch_id_start = std::get<uint32_t>(parameters[0]);
    const auto switch_id_end = std::get<uint32_t>(parameters[1]);

    const auto is_range = switch_id_start != switch_id_end;

    const auto setting_to_off = (std::get<uint32_t>(parameters[2]) ? true : false);

    const auto var_prefix = is_range ?
        utils::format_string("{%s} .. {%s}",
                             get_switch_name(switch_id_start)->data(),
                             get_switch_name(switch_id_end)->data()) :
        utils::format_string("{%s}", get_switch_name(switch_id_start)->data());

    return utils::format_string("%s = %s", var_prefix.data(), setting_to_off ? "OFF" : "ON");
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
        utils::format_string("{%s} .. {%s}",
                             get_variable_name(variable_id_start)->data(),
                             get_variable_name(variable_id_end)->data()) :
        utils::format_string("{%s}", get_variable_name(variable_id_start)->data());

    const auto operand = static_cast<ControlVariable::Operand>(std::get<uint32_t>(parameters[3]));

    if (operand == ControlVariable::Operand::VARIABLE) {
        const auto variable = std::get<uint32_t>(parameters[4]);

        return utils::format_string("%s %s {%s}", var_prefix.data(),
                                    operation_strs.at(operation).data(),
                                    get_variable_name(variable)->data());
    } else if (operand == ControlVariable::Operand::CONSTANT) {
        const auto constant = std::get<uint32_t>(parameters[4]);

        return utils::format_string("%s %s %d", var_prefix.data(),
                                    operation_strs.at(operation).data(), constant);
    } else if (operand == ControlVariable::Operand::RANDOM) {
        const auto min = std::get<uint32_t>(parameters[4]);
        const auto max = std::get<uint32_t>(parameters[5]);

        return utils::format_string("%s = Random %d .. %d", var_prefix.data(), min, max);
    }

    return unsupported;
}

bool RPGMakerScraper::scrape_command_script(std::shared_ptr<ResultInformationBase> result_info, const Command &command) {

    constexpr uint32_t expected_param_count = 1;

    if (command.parameters.size() != expected_param_count) {
        return false;
    }

    if (!std::holds_alternative<std::string>(command.parameters[0])) {
        return false;
    }

    return determine_access_from_script(result_info, std::get<std::string>(command.parameters[0]));
}

std::string RPGMakerScraper::format_common_event_trigger(const CommonEvent &common_event) {
    return utils::format_string("HAS TRIGGER: (%s)", (common_event.trigger == CommonEventTrigger::AUTORUN ? "AUTORUN" : "PARALLEL"));
}

bool RPGMakerScraper::scrape_common_event_trigger(std::shared_ptr<ResultInformationBase> result_info, const CommonEvent &common_event) {

    if (common_event.switch_id != query_id) {
        return false;
    }

    result_info->access_type = AccessType::READ;
    result_info->active = true;
    result_info->formatted_action = format_common_event_trigger(common_event);

    return true;
}

bool RPGMakerScraper::determine_access_from_script(std::shared_ptr<ResultInformationBase> result_info, std::string_view script_line) {

    if (mode == ScrapeMode::VARIABLES) {
        const std::string script_read_variable = utils::format_string("$gameVariables.value(%d)", query_id);
        if (script_line.find(script_read_variable) != std::string::npos) {
            result_info->access_type = AccessType::READ;
            result_info->active = true;
            result_info->formatted_action = script_line;
            return true;
        }
        const std::string script_write_variable = utils::format_string("$gameVariables.setValue(%d", query_id);
        if (script_line.find(script_write_variable) != std::string::npos) {
            result_info->access_type = AccessType::WRITE;
            result_info->active = true;
            result_info->formatted_action = script_line;
            return true;
        }
    } else if (mode == ScrapeMode::SWITCHES) {
        const std::string script_read_switch = utils::format_string("$gameSwitches.value(%d)", query_id);
        if (script_line.find(script_read_switch) != std::string::npos) {
            result_info->access_type = AccessType::READ;
            result_info->active = true;
            result_info->formatted_action = script_line;
            return true;
        }
        const std::string script_write_switch = utils::format_string("$gameSwitches.setValue(%d", query_id);
        if (script_line.find(script_write_switch) != std::string::npos) {
            result_info->access_type = AccessType::WRITE;
            result_info->active = true;
            result_info->formatted_action = script_line;
            return true;
        }
    }

    return false;
}

uint32_t RPGMakerScraper::calculate_instances() const {

    uint32_t count = 0;
    for (const auto &pair : results) {
        count += static_cast<uint32_t>(pair.second.size());
    }
    for (const auto &pair : common_event_results) {
        count += static_cast<uint32_t>(pair.second.size());
    }
    return count;
}

void RPGMakerScraper::print_results() {

    if (!has_results()) {
        log_colored(logger::console_colors::RED, logger::console_colors::BLACK, "Couldn't locate maps using RPGMaker Variable #%03d", query_id);
        return;
    }

    log_nopre("=========================================");

    log_colored_nnl(colors::WHITE, colors::BLACK, "Found ");
    log_colored_nnl(colors::GREEN, colors::BLACK, "%d %s", results.size(), (results.size() > 1 ? "maps" : "map"));
    if (!common_event_results.empty()) {
        log_colored_nnl(colors::WHITE, colors::BLACK, " and ");
        log_colored_nnl(colors::GREEN, colors::BLACK, "%d %s", common_event_results.size(), 
                        (common_event_results.size() > 1 ? "common events" : "common event"));
    }
    log_colored_nnl(colors::WHITE, colors::BLACK, " yielding ");
    log_colored(colors::GREEN, colors::BLACK, "%d total %s ", calculate_instances(), (calculate_instances() > 1 ? "instances" : "instance"));

    if (mode == ScrapeMode::VARIABLES) {
        log_colored(colors::WHITE, colors::BLACK, "using variable #%03d (\'%s\')", query_id, variable_name.data());
    } else if (mode == ScrapeMode::SWITCHES) {
        log_colored(colors::WHITE, colors::BLACK, "using switch #%03d (\'%s\')", query_id, switch_name.data());
    }

    log_nopre("=========================================");

    for (const auto &[map_id, hits] : results) {
        log_colored(colors::CYAN, colors::BLACK, "\n%s ('%s')", format_map_name(map_id).data(), get_map_name(map_id)->data());
        log_colored(colors::WHITE, colors::BLACK, "--------------------------------------------------\n");

        for (const auto &hit : hits) {
            const auto &event_info = hit->event_info;

            // group similar events cleanly
            static auto latest_event_id = event_info.id;
            static auto latest_event_page = hit->event_page;

            const bool is_different = (latest_event_id != event_info.id);
            if (is_different) {
                latest_event_id = event_info.id;
                latest_event_page = hit->event_page;
                if (hit != *hits.begin()) {
                    log_nopre("\n");
                }
            }

            log_colored_nnl((hit->active ? colors::DARK_GREEN : colors::DARK_GRAY), colors::BLACK, "%s",
                            (hit->active ? "ON" : "OFF"));
            const auto access_info = get_access_info(hit->access_type);
            log_colored_nnl(access_info.second, colors::BLACK, " [%s]", access_info.first.data());

            log_nopre("\t@ [%d, %d] on Event #%03d ('%s') on Event Page #%02d:", event_info.x, event_info.y,
                      event_info.id, event_info.name.data(), hit->event_page);

            // log line number | reference
            if (hit->line_number) {
                log_colored_nnl(colors::DARK_GRAY, colors::BLACK, "\t\t\tLine %03d", *hit->line_number);
            } else {
                log_colored_nnl(colors::DARK_GRAY, colors::BLACK, "\t\t\tLine N/A");
            }

            log_colored(colors::WHITE, colors::BLACK, " | %s", hit->formatted_action.data());
        }
    }

    if (!common_event_results.empty()) {
        log_nopre("\n\n");
    }

    for (const auto &[event_id, common_events] : common_event_results) {
        log_colored(colors::CYAN, colors::BLACK, "\n%s", get_common_event_name(event_id)->data());
        log_colored(colors::WHITE, colors::BLACK, "--------------------------------------------------\n");

        for (const auto &common_event : common_events) {

            // group similar common events evenly
            static auto &latest_event_name = common_event->name;
            const bool is_different = (latest_event_name != common_event->name);
            if (is_different) {
                latest_event_name = common_event->name;
                if (common_event != *common_events.begin()) {
                    log_nopre("\n");
                }
            }

            log_colored_nnl((common_event->active ? colors::DARK_GREEN : colors::DARK_GRAY), colors::BLACK, "%s",
                            (common_event->active ? "ON" : "OFF"));
            const auto access_info = get_access_info(common_event->access_type);
            log_colored_nnl(access_info.second, colors::BLACK, " [%s]", access_info.first.data());

            // log line number | reference
            if (common_event->line_number) {
                log_colored_nnl(colors::DARK_GRAY, colors::BLACK, "\t\tLine %03d", *common_event->line_number);
            } else {
                log_colored_nnl(colors::DARK_GRAY, colors::BLACK, "\t\tLine N/A");
            }

            log_colored(colors::WHITE, colors::BLACK, " | %s", common_event->formatted_action.data());
        }
    }

    log_nopre("\n=========================================");
}

std::ostream &operator<<(std::ostream &os, const RPGMakerScraper &scraper) {

    if (!scraper.has_results()) {
        return os;
    }

    os << "=========================================" << "\n";

    os << utils::format_string("Found %d %s ", scraper.results.size(), (scraper.results.size() > 1 ? "maps" : "map")).data();
    
    if (!scraper.common_event_results.empty()) {
        os << utils::format_string(" and %d %s ", 
                                   scraper.common_event_results.size(), 
                                   (scraper.common_event_results.size() > 1 ? "common events" : "common event")).data();
    }

    os << utils::format_string("yielding %d total %s ", scraper.calculate_instances(), (scraper.calculate_instances() > 1 ? "instances" : "instance")).data();

    if (scraper.mode == ScrapeMode::VARIABLES) {
        os << utils::format_string("using variable #%03d (\'%s\')", scraper.query_id, scraper.variable_name.data()).data();
    } else if (scraper.mode == ScrapeMode::SWITCHES) {
        os << utils::format_string("using switch #%03d (\'%s\')", scraper.query_id, scraper.switch_name.data()).data();
    }

    os << std::endl << "=========================================" << std::endl;

    for (const auto &[map_id, hits] : scraper.results) {
        os << "\n";
        os << utils::format_string("%s (\'%s\')", scraper.format_map_name(map_id).data(), scraper.get_map_name(map_id)->data()) << "\n";
        os << "--------------------------------------------------" << "\n";
        for (const auto &hit : hits) {
            const auto &event_info = hit->event_info;

            // group similar events cleanly
            static auto latest_event_id = event_info.id;
            if (latest_event_id != event_info.id) {
                latest_event_id = event_info.id;
                if (hit != *hits.begin()) {
                    os << std::endl;
                }
            }

            const auto access_info = get_access_info(hit->access_type);
            os << utils::format_string("%s", (hit->active ? "ON" : "OFF")).data() <<
                utils::format_string(" [%s]", access_info.first.data()) << "\n";

            os << utils::format_string("\t@ [%d, %d] on Event #%03d (\'%s\') on Event Page #%02d:", event_info.x, event_info.y,
                                       event_info.id, event_info.name.data(), hit->event_page) << "\n";

            if (hit->line_number) {
                os << utils::format_string("\t\tLine %03d | %s", *hit->line_number, hit->formatted_action.data()) << "\n";
            } else {
                os << "\t\t" << hit->formatted_action.data() << "\n";
            }
        }
    }

    if (!scraper.common_event_results.empty()) {
        os << "\n\n";
    }

    for (const auto &[event_id, common_events] : scraper.common_event_results) {
        os << scraper.get_common_event_name(event_id)->data() << "\n";
        os << "--------------------------------------------------\n";

        for (const auto &common_event : common_events) {

            // group similar common events evenly
            static auto &latest_event_name = common_event->name;
            const bool is_different = (latest_event_name != common_event->name);
            if (is_different) {
                latest_event_name = common_event->name;
                if (common_event != *common_events.begin()) {
                    log_nopre("\n");
                }
            }

            const auto access_info = get_access_info(common_event->access_type);
            os << utils::format_string("%s", (common_event->active ? "ON" : "OFF")).data() <<
                utils::format_string(" [%s]", access_info.first.data());

            // log line number | reference
            if (common_event->line_number) {
                os << utils::format_string("\t\tLine %03d | %s", *common_event->line_number, common_event->formatted_action.data()) << "\n";
            } else {
                os << "\t\t" << common_event->formatted_action.data() << "\n";
            }
        }
    }

    os << "=========================================" << "\n";

    return os;
}

std::optional<std::string> RPGMakerScraper::output_json() {
    if (!has_results()) {
        return std::nullopt;
    }

    json _json;

    // output map events under 'map_events'
    for (const auto &[map_id, events] : results) {
        for (const auto &event : events) {
            json event_json = {
                {"access_type", static_cast<uint32_t>(event->access_type)},
                {"active", event->active},
                {"event_page", event->event_page},
                {"formatted_action", event->formatted_action},
                {"id", event->event_info.id},
                {"name", event->event_info.name},
                {"note", event->event_info.note},
                {"x", event->event_info.x},
                {"y", event->event_info.y},
            };

            if (event->line_number) {
                event_json["line_number"] = *event->line_number;
            }

            _json["maps"][std::to_string(map_id)].push_back(event_json);
        }
    }
    // output common event results under 'common_events'
    for (const auto &[common_event_id, common_events] : common_event_results) {
        for (const auto &common_event : common_events) {
            json common_event_json = {
                {"access_type", static_cast<uint32_t>(common_event->access_type)},
                {"active", common_event->active},
                {"formatted_action", common_event->formatted_action},
                {"name", common_event->name}
            };

            if (common_event->line_number) {
                common_event_json["line_number"] = *common_event->line_number;
            }

            _json["common_events"][std::to_string(common_event_id)].push_back(common_event_json);
        }
    }

    return _json.dump();
}
