/**
 * @file logger.cpp
 * @brief Implementation of the logging system
 *
 * Created by Claude (Anthropic AI Assistant) for code cleanup
 */

#include "logger.h"

// Initialize static members
LogLevel Logger::s_minLevel = LogLevel::INFO;
bool Logger::s_useColors = true;
std::mutex Logger::s_mutex;
