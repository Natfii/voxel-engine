/**
 * @file logger.cpp
 * @brief Implementation of the logging system
 *
 */

#include "logger.h"

// Initialize static members
LogLevel Logger::s_minLevel = LogLevel::INFO;
bool Logger::s_useColors = true;
std::mutex Logger::s_mutex;
