#pragma once

#include <string>
#include <vector>

/// One entry of the "characters" config array. x/y/scale are only meaningful
/// when hasPosition is true — otherwise the character is auto-arranged in a
/// row alongside the others (see LAppLive2DManager::ReflowAutoLayout).
struct CharacterConfig
{
    std::string model;
    bool hasPosition = false;
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
};

struct LAppConfig
{
    std::vector<std::string> additionalModelDirs;
    std::string defaultModel;
    float emotionTimeout = 5.0f;
    float modelScale = 1.0f;
    float modelX = 0.0f;
    float modelY = 0.0f;
    int windowWidth = 1900;
    int windowHeight = 1000;

    /// Initial character roster. Populated from the "characters" JSON array;
    /// if that's absent/empty but "default_model" is set, synthesized as a
    /// single entry from default_model/model_scale/model_x/model_y so old
    /// config files keep behaving the same.
    std::vector<CharacterConfig> characters;

    static LAppConfig& GetInstance();

    bool LoadFromFile(const std::string& path);

    static std::string GetDefaultConfigPath();

    static std::string ExpandTilde(const std::string& path);

private:
    static std::vector<CharacterConfig> ParseCharacters(const std::string& s);
    static std::string StripComments(const std::string& s);
};
