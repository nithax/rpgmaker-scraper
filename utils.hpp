#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace utils {

	template<typename ... arg>
    static std::string format_string( std::string_view fmt, arg ... args )
    {
        const int size = std::snprintf(nullptr, NULL, fmt.data(), args ...) + 1;
        const auto buf = std::make_unique<char[]>(size);
        std::snprintf(buf.get(), size, fmt.data(), args ...);

        return std::string(buf.get(), (buf.get() + size) - 1);
    }
};
