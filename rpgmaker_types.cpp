#include "rpgmaker_types.hpp"

#include "logger.hpp"

using namespace RPGMaker;

bool Command::is_valid(const json &command_json) const {
    if (!command_json.contains("code") || !command_json["code"].is_number_integer()) {
        log_err(R"(Command doesn't have a code or it's not an integer!")");
        return false;
    }
    if (!command_json.contains("parameters")) {
        log_err(R"(Command doesn't have parameters!)");
        return false;
    }

    return true;
}

Command::Command(const json &command_json) {
    if (!is_valid(command_json)) {
        return;
    }

    code = static_cast<CommandCode>(command_json["code"].get<uint32_t>());

    for (const auto &parameter : command_json["parameters"]) {
        if (parameter.is_number_integer()) {
            parameters.emplace_back(parameter.get<uint32_t>());
            continue;
        }
        if (parameter.is_number_float()) {
            parameters.emplace_back(parameter.get<double>());
            continue;
        }
        if (parameter.is_boolean()) {
            parameters.emplace_back(parameter.get<bool>());
            continue;
        }
        if (parameter.is_string() && parameter.get<std::string_view>().length() == 1) {
            parameters.emplace_back(parameter.get<std::string>()[0]);
            continue;
        }
        if (parameter.is_string()) {
            parameters.emplace_back(parameter.get<std::string>());
            continue;
        }
    }
}

bool Condition::is_valid(const json &condition_json) const {
    if (!condition_json.contains("switch1Id") || !condition_json["switch1Id"].is_number_integer()) {
        log_err(R"(This condition doesn't have a switch 1 id or it's not an integer!)");
        return false;
    }
    if (!condition_json.contains("switch1Valid") || !condition_json["switch1Valid"].is_boolean()) {
        log_err(R"(This condition doesn't have a bool to define if switch1Id is active or it's not a boolean!)");
        return false;
    }
    if (!condition_json.contains("switch2Id") || !condition_json["switch2Id"].is_number_integer()) {
        log_err(R"(This condition doesn't have a switch 2 id or it's not an integer!)");
        return false;
    }
    if (!condition_json.contains("switch2Valid") || !condition_json["switch2Valid"].is_boolean()) {
        log_err(R"(This condition doesn't have a bool to define if switch2Id is active or it's not a boolean!)");
        return false;
    }
    if (!condition_json.contains("variableId") || !condition_json["variableId"].is_number_integer()) {
        log_err(R"(This condition doesn't have a variable id or it's not an integer!)");
        return false;
    }
    if (!condition_json.contains("variableValid") || !condition_json["variableValid"].is_boolean()) {
        log_err(R"(This condition doesn't have a bool to define if variableId is active or it's not a boolean!)");
        return false;
    }
    if (!condition_json.contains("variableValue") || !condition_json["variableValue"].is_number_integer()) {
        log_err(R"(This condition doesn't have a value to compare against or it's not an integer!)");
        return false;
    }

    return true;
}

Condition::Condition(const json &condition_json) {
    if (!is_valid(condition_json)) {
        return;
    }

    switch1_id = condition_json["switch1Id"].get<uint32_t>();
    switch1_valid = condition_json["switch1Valid"].get<bool>();
    switch2_id = condition_json["switch2Id"].get<uint32_t>();
    switch2_valid = condition_json["switch2Valid"].get<bool>();
    variable_id = condition_json["variableId"].get<uint32_t>();
    variable_valid = condition_json["variableValid"].get<bool>();
    variable_value = condition_json["variableValue"].get<uint32_t>();
}

bool EventPage::is_valid(const json &event_page_json) const {
    if (!event_page_json.contains("conditions")) {
        log_err(R"(This event page doesn't have conditions!)");
        return false;
    }
    if (!event_page_json.contains("list")) {
        log_err(R"(This event page doesn't have commands!)");
        return false;
    }

    return true;
}

EventPage::EventPage(const json &event_page_json) {
    if (!is_valid(event_page_json)) {
        return;
    }

    conditions = Condition(event_page_json["conditions"]);

    const auto &command_list = event_page_json["list"];
    list.reserve(command_list.size());

    for (size_t line = 0, last_line = command_list.size(); line < last_line; ++line) {
        list.emplace_back(Command(command_list[line]));
    }
}

bool Event::is_valid(const json &event_json) const {
    if (!event_json.contains("x") || !event_json["x"].is_number_integer()) {
        log_err(R"(Event doesn't have a x position or it's not an integer!)");
        return false;
    }
    if (!event_json.contains("y") || !event_json["y"].is_number_integer()) {
        log_err(R"(Event doesn't have a y position or it's not an integer!)");
        return false;
    }
    if (!event_json.contains("name") || !event_json["name"].is_string()) {
        log_err(R"(Event doesn't have a name or it's not a string!)");
        return false;
    }
    if (!event_json.contains("id") || !event_json["id"].is_number_integer()) {
        log_err(R"(Event doesn't have an id or it's not an integer!)");
        return false;
    }
    if (!event_json.contains("pages")) {
        log_err(R"(Event doesn't have pages!)");
        return false;
    }

    return true;
}

Event::Event(const json &event_json) {
    if (!is_valid(event_json)) {
        return;
    }

    x = event_json["x"].get<uint32_t>();
    y = event_json["y"].get<uint32_t>();
    id = event_json["id"].get<uint32_t>();
    name = event_json["name"].get<std::string_view>();
    note = event_json["note"].get<std::string_view>();

    const auto page_count = event_json["pages"].size();
    pages.reserve(page_count);

    for (size_t page_num = 0; page_num < page_count; ++page_num) {
        pages.emplace_back(EventPage(event_json["pages"][page_num]));
    }
}