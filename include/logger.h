/**
 * @file logger.h
 * @brief Simple logging system with multiple severity levels
 *
 * Created by Claude (Anthropic AI Assistant) for code cleanup
 */

#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>

/**
 * @brief Log severity levels
 */
enum class LogLevel {
    DEBUG,    ///< Verbose debugging information
    INFO,     ///< General informational messages
    WARNING,  ///< Warning messages (non-critical issues)
    ERROR     ///< Error messages (critical issues)
};

/**
 * @brief Thread-safe logger with severity levels and formatting
 *
 * The Logger class provides a centralized logging system to replace
 * scattered std::cout/cerr calls throughout the codebase.
 *
 * Features:
 * - Multiple severity levels (DEBUG, INFO, WARNING, ERROR)
 * - Thread-safe output (uses mutex)
 * - Color-coded console output (optional)
 * - Configurable minimum log level
 * - Stream-style API for easy use
 *
 * Usage:
 * @code
 * Logger::info() << "Player position: " << position.x << ", " << position.y;
 * Logger::warning() << "Chunk mesh generation took too long: " << duration << "ms";
 * Logger::error() << "Failed to load texture: " << filename;
 * @endcode
 */
class Logger {
public:
    /**
     * @brief Log stream that outputs when destroyed
     *
     * This class allows for stream-style logging with automatic flushing.
     */
    class LogStream {
    public:
        /**
         * @brief Constructs a log stream with specified level
         * @param level Severity level for this log message
         */
        LogStream(LogLevel level) : m_level(level) {}

        /**
         * @brief Destructor flushes the log message
         *
         * Outputs the accumulated message to stdout/stderr based on level.
         */
        ~LogStream() {
            if (m_level >= s_minLevel) {
                std::lock_guard<std::mutex> lock(s_mutex);

                // Output to appropriate stream
                std::ostream& out = (m_level >= LogLevel::ERROR) ? std::cerr : std::cout;

                // Color-coded prefix (ANSI escape codes)
                if (s_useColors) {
                    switch (m_level) {
                        case LogLevel::DEBUG:
                            out << "\033[36m[DEBUG]\033[0m ";   // Cyan
                            break;
                        case LogLevel::INFO:
                            out << "\033[32m[INFO]\033[0m ";    // Green
                            break;
                        case LogLevel::WARNING:
                            out << "\033[33m[WARNING]\033[0m "; // Yellow
                            break;
                        case LogLevel::ERROR:
                            out << "\033[31m[ERROR]\033[0m ";   // Red
                            break;
                    }
                } else {
                    switch (m_level) {
                        case LogLevel::DEBUG:   out << "[DEBUG] "; break;
                        case LogLevel::INFO:    out << "[INFO] "; break;
                        case LogLevel::WARNING: out << "[WARNING] "; break;
                        case LogLevel::ERROR:   out << "[ERROR] "; break;
                    }
                }

                out << m_stream.str() << std::endl;
            }
        }

        /**
         * @brief Stream operator for chaining output
         * @tparam T Type of value to log
         * @param value Value to append to log message
         * @return Reference to this stream for chaining
         */
        template<typename T>
        LogStream& operator<<(const T& value) {
            if (m_level >= s_minLevel) {
                m_stream << value;
            }
            return *this;
        }

    private:
        LogLevel m_level;              ///< Severity level of this message
        std::ostringstream m_stream;   ///< Accumulated message
    };

    // ========== Static Logging Methods ==========

    /**
     * @brief Creates a debug-level log stream
     * @return LogStream for debug messages
     */
    static LogStream debug() { return LogStream(LogLevel::DEBUG); }

    /**
     * @brief Creates an info-level log stream
     * @return LogStream for informational messages
     */
    static LogStream info() { return LogStream(LogLevel::INFO); }

    /**
     * @brief Creates a warning-level log stream
     * @return LogStream for warnings
     */
    static LogStream warning() { return LogStream(LogLevel::WARNING); }

    /**
     * @brief Creates an error-level log stream
     * @return LogStream for errors
     */
    static LogStream error() { return LogStream(LogLevel::ERROR); }

    // ========== Configuration ==========

    /**
     * @brief Sets the minimum log level
     *
     * Messages below this level will be suppressed.
     *
     * @param level Minimum severity level to display
     */
    static void setMinLevel(LogLevel level) {
        s_minLevel = level;
    }

    /**
     * @brief Enables or disables color-coded output
     * @param enable True to use ANSI color codes
     */
    static void setUseColors(bool enable) {
        s_useColors = enable;
    }

private:
    static LogLevel s_minLevel;       ///< Minimum level to display
    static bool s_useColors;          ///< Whether to use ANSI colors
    static std::mutex s_mutex;        ///< Mutex for thread-safe output
};
