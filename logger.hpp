#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <iostream>
#include <mutex>
#include <unordered_map>

#include "utils.hpp"

enum class log_level : std::uint32_t
{
    LOG_FATAL,
    LOG_ERROR,
    LOG_WARN,
    LOG_OK,
    LOG_INFO,
    LOG_DEBUG,
    LOG_NOPREFIX, // keep this last
};

class logger
{
public:

    logger( const std::wstring_view& title_name = {} )
    {
        AllocConsole();
        AttachConsole( GetCurrentProcessId() );

        if ( !title_name.empty() )
        {
            SetConsoleTitle( title_name.data() );
        }

        FILE* in = nullptr;
        FILE* out = nullptr;

        freopen_s( &in, "conin$", "r", stdin );
        freopen_s( &out, "conout$", "w", stdout );
        freopen_s( &out, "conout$", "w", stderr );

        console_handle = GetStdHandle( STD_OUTPUT_HANDLE );
    }

    ~logger()
    {
        FreeConsole();
    }

    enum class console_colors : std::uint8_t
    {
        BLACK,
        DARK_BLUE,
        DARK_GREEN,
        DARK_CYAN,
        DARK_RED,
        DARK_MAGENTA,
        DARK_YELLOW,
        GRAY,
        DARK_GRAY,
        BLUE,
        GREEN,
        CYAN,
        RED,
        MAGENTA,
        YELLOW,
        WHITE,
    };

    struct log_type_info_t
    {
        std::string prefix{};
        console_colors fg = console_colors::WHITE;
        console_colors bg = console_colors::BLACK;

        log_type_info_t() = default;
    };

    template<typename ... arg>
    void print_colored( console_colors fg, console_colors bg, bool newline, std::string_view fmt, arg ... args )
    {
        std::unique_lock< decltype( m )> lock( m );

        const auto txt = utils::format_string( fmt, args... );

        set_console_color( fg, bg );

        std::cout << txt.c_str();

        set_console_color( console_colors::WHITE, console_colors::BLACK );

        if (newline) {
            std::cout << std::endl;
        }
    }

    template< typename ... arg >
    void print( log_level level, std::string_view fmt, arg ... args ) {
        std::unique_lock< decltype( m )> lock( m );

        const auto &info = console_type_info[level];
        const auto txt = utils::format_string( fmt, args... );

        set_console_color( info.fg, info.bg );

        if ( level < log_level::LOG_NOPREFIX ) {
            std::cout << info.prefix;
        }

        std::cout << txt.c_str();

        set_console_color( console_colors::WHITE, console_colors::BLACK );

        std::cout << std::endl;
    }

    template< typename ... arg >
    void print_with_func( log_level level, std::string_view func_name, std::string_view fmt, arg ... args )
    {
        std::unique_lock< decltype( m )> lock( m );

        const auto &info = console_type_info[ level ];
        const auto txt = utils::format_string( fmt, args... );

        set_console_color( info.fg, info.bg );

        if ( level < log_level::LOG_NOPREFIX )
        {
            std::cout << info.prefix;
        }

        std::cout << "[ " << func_name.data() << " ] " << txt.c_str();

        set_console_color( console_colors::WHITE, console_colors::BLACK );

        std::cout << std::endl;
    }

private:
    inline bool set_console_color( const console_colors fg, const console_colors bg )
    {
        if ( console_handle != INVALID_HANDLE_VALUE )
        {
            WORD color = (uint8_t)fg | ( (uint8_t)bg << 4 );
            return SetConsoleTextAttribute( console_handle, color );
        }

        return false;
    }

    std::unordered_map< log_level, log_type_info_t > console_type_info =
    {
        { log_level::LOG_FATAL, { "[ ! ] ", console_colors::RED, console_colors::WHITE } },
        { log_level::LOG_ERROR, { "[ - ] ", console_colors::RED, console_colors::BLACK } },
        { log_level::LOG_WARN,  { "[ # ] ", console_colors::BLACK, console_colors::YELLOW } },
        { log_level::LOG_OK,    { "[ + ] ", console_colors::GREEN, console_colors::BLACK } },
        { log_level::LOG_INFO,  { "[ ~ ] ", console_colors::WHITE, console_colors::BLACK } },
        { log_level::LOG_DEBUG, { "      ", console_colors::DARK_GRAY, console_colors::BLACK } },
    };

    std::mutex m;
    HANDLE console_handle = INVALID_HANDLE_VALUE;
};

inline auto g_logger = std::make_unique< logger >( L"~ rpgmaker scraper by nit ~" );
#define log_colored_nnl(fg, bg, ...) g_logger->print_colored(fg, bg, false, __VA_ARGS__)
#define log_colored( fg, bg, ... ) g_logger->print_colored( fg, bg, true, __VA_ARGS__ )
#define _log(log_type, ...) g_logger->print( log_type, __VA_ARGS__ )
#define _log_with_func(log_type, ...) g_logger->print_with_func( log_type, __FUNCTION__, __VA_ARGS__ )

#define log_fatal( ... ) _log( log_level::LOG_FATAL, __VA_ARGS__ )
#define log_err( ... ) _log( log_level::LOG_ERROR, __VA_ARGS__ )
#define log_warn( ... ) _log( log_level::LOG_WARN, __VA_ARGS__ )
#define log_ok( ... ) _log( log_level::LOG_OK, __VA_ARGS__ )
#define log_info( ... ) _log( log_level::LOG_INFO, __VA_ARGS__ )
#define log_dbg( ... ) _log( log_level::LOG_DEBUG, __VA_ARGS__ )
#define log_nopre( ... ) _log( log_level::LOG_NOPREFIX, __VA_ARGS__ )
