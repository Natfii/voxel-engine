#include "config.h"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>

Config& Config::instance() {
    static Config instance;
    return instance;
}

bool Config::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        Logger::error() << "Failed to open config file: " << filepath;
        return false;
    }

    std::string currentSection;
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Check for section header [Section]
        if (line[0] == '[' && line[line.length() - 1] == ']') {
            currentSection = line.substr(1, line.length() - 2);
            currentSection = trim(currentSection);
            continue;
        }

        // Parse key = value
        size_t equalPos = line.find('=');
        if (equalPos != std::string::npos) {
            std::string key = trim(line.substr(0, equalPos));
            std::string value = trim(line.substr(equalPos + 1));

            // Remove inline comments
            size_t commentPos = value.find('#');
            if (commentPos != std::string::npos) {
                value = trim(value.substr(0, commentPos));
            }
            commentPos = value.find(';');
            if (commentPos != std::string::npos) {
                value = trim(value.substr(0, commentPos));
            }

            if (!currentSection.empty() && !key.empty()) {
                m_data[currentSection][key] = value;
            }
        }
    }

    file.close();
    return true;
}

int Config::getInt(const std::string& section, const std::string& key, int defaultValue) const {
    auto sectionIt = m_data.find(section);
    if (sectionIt != m_data.end()) {
        auto keyIt = sectionIt->second.find(key);
        if (keyIt != sectionIt->second.end()) {
            try {
                return std::stoi(keyIt->second);
            } catch (...) {
                Logger::warning() << "Failed to parse int for [" << section << "]:" << key;
            }
        }
    }
    return defaultValue;
}

float Config::getFloat(const std::string& section, const std::string& key, float defaultValue) const {
    auto sectionIt = m_data.find(section);
    if (sectionIt != m_data.end()) {
        auto keyIt = sectionIt->second.find(key);
        if (keyIt != sectionIt->second.end()) {
            try {
                return std::stof(keyIt->second);
            } catch (...) {
                Logger::warning() << "Failed to parse float for [" << section << "]:" << key;
            }
        }
    }
    return defaultValue;
}

std::string Config::getString(const std::string& section, const std::string& key, const std::string& defaultValue) const {
    auto sectionIt = m_data.find(section);
    if (sectionIt != m_data.end()) {
        auto keyIt = sectionIt->second.find(key);
        if (keyIt != sectionIt->second.end()) {
            return keyIt->second;
        }
    }
    return defaultValue;
}

std::string Config::trim(const std::string& str) const {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

bool Config::saveToFile(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        Logger::error() << "Failed to open config file for writing: " << filepath;
        return false;
    }

    for (const auto& section : m_data) {
        file << "[" << section.first << "]\n";
        for (const auto& keyValue : section.second) {
            file << keyValue.first << " = " << keyValue.second << "\n";
        }
        file << "\n";
    }

    file.close();
    return true;
}

void Config::setInt(const std::string& section, const std::string& key, int value) {
    m_data[section][key] = std::to_string(value);
}

void Config::setFloat(const std::string& section, const std::string& key, float value) {
    m_data[section][key] = std::to_string(value);
}

void Config::setString(const std::string& section, const std::string& key, const std::string& value) {
    m_data[section][key] = value;
}
