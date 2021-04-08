#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "json.hpp"
using json = nlohmann::json;

using variable_element = 
    std::variant<uint32_t, double, bool, char, std::string>;

namespace RPGMaker {

    enum class CommonEventTrigger : uint32_t {
        NONE,
        AUTORUN,
        PARALLEL,
    };

    enum class CommandCode : uint32_t {
        IF_STATEMENT = 111,
        CONTROL_SWITCH = 121,
        CONTROL_VARIABLE = 122,
        SCRIPT_SINGLE_LINE = 355,
        SCRIPT_MULTI_LINE = 655,
    };

    namespace IfStatement {

        // parameter 0
        enum class IDType : uint32_t {
            SWITCH,
            VARIABLE,
            SCRIPT = 12,
        };
        // parameter 2
        enum class CompareType : uint32_t {
            CONSTANT,
            VARIABLE,
        };
    }; // IfStatement

    namespace ControlVariable {

        // parameter 3
        enum class Operand : uint32_t {
            CONSTANT,
            VARIABLE,
            RANDOM,
            GAME_DATA,
            SCRIPT
        };
    }; // ControlVariable

    struct Command {

        Command() = default;
        Command(const json &command);

        bool is_valid(const json &command) const;

        bool is_script() const {
            return code == CommandCode::SCRIPT_SINGLE_LINE ||
                code == CommandCode::SCRIPT_MULTI_LINE;
        }
        bool is_if_statement() const {
            return code == CommandCode::IF_STATEMENT;
        }

        bool is_control_switch() const {
            return code == CommandCode::CONTROL_SWITCH;
        }

        bool is_control_variable() const {
            return code == CommandCode::CONTROL_VARIABLE;
        }

        CommandCode code{};
        std::vector<variable_element> parameters{};
    };

    struct Condition {

        Condition() = default;
        Condition(const json &condition_json);

        bool is_valid(const json &condition_json) const;

        uint32_t switch1_id{};
        bool switch1_valid{};
        uint32_t switch2_id{};
        bool switch2_valid{};
        uint32_t variable_id{};
        bool variable_valid{};
        uint32_t variable_value{};
    };

    struct EventPage {

        EventPage() = default;
        EventPage(const json &event_page_json);

        bool is_valid(const json &event_page_json) const;

        Condition conditions{};
        std::vector<Command> list{};
    };

    struct Event {

        Event() = default;
        Event(const json &event_json);

        bool is_valid(const json &event_json) const;

        uint32_t id{};
        std::string name{};
        std::string note{};
        std::vector<EventPage> pages{};
        uint32_t x{};
        uint32_t y{};
    };

    struct CommonEvent {

        CommonEvent() = default;
        CommonEvent(const json &common_event_json);

        bool is_valid(const json &common_event_json) const;

        bool has_trigger() const;

        uint32_t id{};
        std::vector<Command> list{};
        std::string name{};
        uint32_t switch_id{};
        CommonEventTrigger trigger{};
    };

}; //RPGMaker
