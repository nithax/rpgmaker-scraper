#pragma once

#include <filesystem>
#include <list>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "json.hpp"
using json = nlohmann::json;

#include "rpgmaker_types.hpp"
using namespace RPGMaker;

enum class AccessType : uint32_t {
	READ,
	WRITE,
};

enum class ScrapeMode : uint32_t {
	VARIABLES,
	SWITCHES,
};

class EventInformation {
public:

	EventInformation() = default;
	EventInformation(const json &event_json);

	std::string_view get_name() const {
		return name;
	};

	uint32_t get_id() const {
		return id;
	}

	uint32_t get_x() const {
		return x;
	}

	uint32_t get_y() const {
		return y;
	}

private:
	// Name of the event
	std::string name;
	// ID of the event
	uint32_t id;
	// X Position on the map where the event rests
	uint32_t x;
	// Y position on the map where the event rests
	uint32_t y;
};

struct ResultInformation {

	bool operator==(const ResultInformation other) const {
		return (other.access_type == access_type &&
			other.active == active &&
			other.event_page == event_page &&
			other.formatted_action == formatted_action &&
			other.line_number == line_number);
	}

	bool operator!=(const ResultInformation other) const {
		return !operator==(other);
	}

	// Is this an accessor or mutator?
	AccessType access_type;
	// The event information it belongs to
	Event event_info;
	// Is this actually active in-game code
	bool active;
	// What event page this is present on
	uint32_t event_page;
	// If this is a conditional in script, what line it appears on
	std::optional<uint32_t> line_number;
	// information parsed from json describing where the variable is used
	std::string formatted_action;
};

using MapIdToName = std::map<uint32_t, std::string>;
using VariableIdToName = std::map<uint32_t, std::string>;
using ResultMap = std::map<uint32_t, std::vector<ResultInformation>>;
using EventMap = std::map<uint32_t, std::vector<Event>>;

class RPGMakerScraper {
public:
	RPGMakerScraper() = default;
	RPGMakerScraper(ScrapeMode _mode, uint32_t _id) : variable_id(_id), mode(_mode) { load(); }
	~RPGMakerScraper() = default;

	MapIdToName get_map_info_names() const {
		return map_info_names;
	}

	VariableIdToName get_variable_names() const {
		return variable_names;
	}

	std::optional<std::string> get_map_name(uint32_t id) const {
		if (map_info_names.empty() || map_info_names.find(id) == map_info_names.end()) {
			return std::nullopt;
		}
		return map_info_names.at(id);
	}

	std::optional<std::string> get_variable_name(uint32_t id) const {
		if (id == 0 || variable_names.empty() || variable_names.find(id) == variable_names.end()) {
			return std::nullopt;
		}

		const std::string &name = variable_names.at(id);

		if (name.empty()) {
			return std::string("#") + std::to_string(id);
		}

		return name;
	}

	// loads all the necessary functions to setup and verify input
	// throws several types of exceptions
	void load();

	// scrape all information exclusive to RPGMaker variables into results
	void scrape_variables();

	// overload operator for ostream to print information to a file
	friend std::ostream& operator<<(std::ostream& os, const RPGMakerScraper &scraper);

private:
	// Path to the root folder we're searching
	std::filesystem::path root_data_path;

	// All the map names mapped via map id
	MapIdToName map_info_names{};

	// All the variable names mapped via variable id
	VariableIdToName variable_names{};

	// The variable ID we're interested in
	uint32_t variable_id = 0;

	// The name of the variable we're interested in
	std::string variable_name{};

	// All of our results via map id
	ResultMap results;

	// What mode the scraper is currently in
	ScrapeMode mode;

	// All the events already parsed via map id
	EventMap all_events;

	// Progress status
	std::string progress_status{};

	// populate all the map names into map_info_names
	// returns true if successful, otherwise false
	bool populate_map_names();

	// populate all the variable names into variable_names
	// returns true if successful, otherwise false
	bool populate_variable_names();

	// check if the root directory exists and setup root_data_path
	// returns true if valid, otherwise false
	bool setup_directory();

	// scrape all the existing maps and their events into all_events
	void scrape_maps();

	// translate a map id into the name of the .json file associated
	std::string format_map_name(uint32_t id) const;

	// output the string showing the reference to a wanted id inside a
	// RPGMaker event page condition
	std::string format_event_page_condition(const Condition& condition);

	// scrape RPGMaker event page conditions and modify result_info accordingly
	// returns true if valid, otherwise false
	bool scrape_event_page_condition(ResultInformation &result_info, const EventPage& event_page);
	
	// output the string showing the reference to a wanted id inside a
	// 'If Statement' command on an event page
	std::string format_command_if_statement(const std::vector<variable_element>& parameters);

	// scrape RPGMaker command 'If Statement' and modify result_info accordingly
	// returns true if valid, otherwise false
	bool scrape_command_if_statement(ResultInformation& result_info, const Command& command);

	// output the string showing the reference to a wanted id inside a
	// 'Control Variable' command on an event page
	std::string format_command_control_variable(const std::vector<variable_element>& parameters);

	// scrape RPGMaker command 'Control Variable' and modify result_info accordingly
	// returns true if valid, otherwise false
	bool scrape_command_control_variable(ResultInformation& result_info, const Command& command);

	// scrape a line of 'script' and modify result_info accordingly
	// returns true if successful, otherwise false
	bool scrape_command_script(ResultInformation& result_info, const Command& command);

	// determines based on the found text inside the script line if it's read or write access
	// returns true if successful, otherwise false
	bool determine_access_from_script(ResultInformation& result_info, std::string_view script_line);

	// calculates total of all results found
	uint32_t calculate_instances() const;

	// print all the found results in a pretty, colored and neat fashion
	// if file_output exists, the file is output with the same information without colors
	void print_results();
};