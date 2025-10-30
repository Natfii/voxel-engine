#pragma once
#include <string>
#include <map>

class Config {
public:
    static Config& instance();

    bool loadFromFile(const std::string& filepath);

    int getInt(const std::string& section, const std::string& key, int defaultValue = 0) const;
    float getFloat(const std::string& section, const std::string& key, float defaultValue = 0.0f) const;
    std::string getString(const std::string& section, const std::string& key, const std::string& defaultValue = "") const;

private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::map<std::string, std::map<std::string, std::string>> m_data;

    std::string trim(const std::string& str) const;
};
