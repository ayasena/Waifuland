#include "LAppConfig.hpp"
#include "JsonMini.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <cstdio>

static LAppConfig s_config;

LAppConfig& LAppConfig::GetInstance()
{
    return s_config;
}

std::string LAppConfig::GetDefaultConfigPath()
{
    const char* configHome = getenv("XDG_CONFIG_HOME");
    if (configHome && strlen(configHome) > 0)
    {
        return std::string(configHome) + "/waifuland/config.json";
    }
    const char* homeDir = getenv("HOME");
    if (!homeDir || strlen(homeDir) == 0)
    {
        homeDir = getpwuid(getuid())->pw_dir;
    }
    return std::string(homeDir) + "/.config/waifuland/config.json";
}

std::string LAppConfig::ExpandTilde(const std::string& path)
{
    if (path.empty() || path[0] != '~') return path;

    const char* homeDir = getenv("HOME");
    if (!homeDir || strlen(homeDir) == 0)
    {
        homeDir = getpwuid(getuid())->pw_dir;
    }
    return std::string(homeDir) + path.substr(1);
}

std::vector<CharacterConfig> LAppConfig::ParseCharacters(const std::string& s)
{
    std::vector<CharacterConfig> result;

    JsonMini root;
    if (!root.Parse(s)) return result;
    if (!root.Has("characters") || root.WasString("characters")) return result;

    std::vector<std::string> objects;
    if (!JsonMini::SplitTopLevel(root.GetString("characters"), objects)) return result;

    for (size_t k = 0; k < objects.size(); k++)
    {
        JsonMini obj;
        if (!obj.Parse(objects[k])) continue;

        CharacterConfig cc;
        cc.model = obj.GetString("model");
        bool hasX = obj.Has("x");
        bool hasY = obj.Has("y");
        cc.x = obj.GetFloat("x", 0.0f);
        cc.y = obj.GetFloat("y", 0.0f);
        cc.hasPosition = hasX || hasY;
        cc.scale = obj.GetFloat("scale", 1.0f);

        if (!cc.model.empty())
        {
            result.push_back(cc);
        }
    }

    return result;
}

std::string LAppConfig::StripComments(const std::string& s)
{
    std::string result;
    result.reserve(s.size());
    bool inString = false;

    for (size_t i = 0; i < s.size(); i++)
    {
        if (inString)
        {
            result += s[i];
            if (s[i] == '\\' && i + 1 < s.size())
            {
                result += s[++i];
            }
            else if (s[i] == '"')
            {
                inString = false;
            }
        }
        else if (s[i] == '"')
        {
            inString = true;
            result += s[i];
        }
        else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/')
        {
            // Skip until end of line
            while (i < s.size() && s[i] != '\n') i++;
            if (i < s.size()) result += '\n';
        }
        else
        {
            result += s[i];
        }
    }

    return result;
}

bool LAppConfig::LoadFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = StripComments(ss.str());

    // Best-effort like before: a partially malformed file still yields
    // whatever parsed cleanly, instead of failing the whole load.
    JsonMini root;
    root.Parse(content);

    std::vector<std::string> dirs;
    if (root.GetStringArray("additional_model_dirs", dirs))
    {
        additionalModelDirs = dirs;
    }
    else
    {
        additionalModelDirs.clear();
    }
    for (size_t i = 0; i < additionalModelDirs.size(); i++)
    {
        additionalModelDirs[i] = ExpandTilde(additionalModelDirs[i]);
    }
    defaultModel = root.GetString("default_model");
    emotionTimeout = root.GetFloat("emotion_timeout", 5.0f);
    modelScale = root.GetFloat("model_scale", 1.0f);
    modelX = root.GetFloat("model_x", 0.0f);
    modelY = root.GetFloat("model_y", 0.0f);
    windowWidth = root.GetInt("window_width", 1900);
    windowHeight = root.GetInt("window_height", 1000);

    characters = ParseCharacters(content);
    if (characters.empty() && !defaultModel.empty())
    {
        // Legacy single-character config (no "characters" array): synthesize
        // one character from default_model/model_scale/model_x/model_y so
        // old config files keep behaving the same.
        CharacterConfig cc;
        cc.model = defaultModel;
        cc.hasPosition = true;
        cc.x = modelX;
        cc.y = modelY;
        cc.scale = modelScale;
        characters.push_back(cc);
    }

    return true;
}
