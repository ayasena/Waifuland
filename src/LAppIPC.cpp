#include "LAppIPC.hpp"
#include "LAppDelegate.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppModel.hpp"
#include "LAppPal.hpp"
#include "LAppDefine.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <algorithm>

using namespace Csm;

namespace {
    LAppIPC* s_instance = NULL;
}

float LAppIPC::_externalMouthY = 0.0f;
bool LAppIPC::_hasExternalMouthY = false;
float LAppIPC::_externalLookX = 0.0f;
float LAppIPC::_externalLookY = 0.0f;
bool LAppIPC::_hasExternalLook = false;

LAppIPC* LAppIPC::GetInstance()
{
    if (s_instance == NULL)
    {
        s_instance = new LAppIPC();
    }
    return s_instance;
}

void LAppIPC::ReleaseInstance()
{
    if (s_instance != NULL)
    {
        delete s_instance;
        s_instance = NULL;
    }
}

LAppIPC::LAppIPC()
    : _serverFd(-1)
{
}

LAppIPC::~LAppIPC()
{
    Release();
}

bool LAppIPC::Initialize()
{
    // Determine socket path
    const char* runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (runtimeDir && strlen(runtimeDir) > 0)
    {
        _socketPath = std::string(runtimeDir) + "/waifuland.sock";
    }
    else
    {
        _socketPath = "/tmp/waifuland.sock";
    }

    // Remove stale socket
    unlink(_socketPath.c_str());

    _serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (_serverFd < 0)
    {
        LAppPal::PrintLogLn("[IPC] Failed to create socket: %s", strerror(errno));
        return false;
    }

    // Set non-blocking
    int flags = fcntl(_serverFd, F_GETFL, 0);
    fcntl(_serverFd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, _socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(_serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        LAppPal::PrintLogLn("[IPC] Failed to bind socket: %s", strerror(errno));
        close(_serverFd);
        _serverFd = -1;
        return false;
    }

    if (listen(_serverFd, 4) < 0)
    {
        LAppPal::PrintLogLn("[IPC] Failed to listen: %s", strerror(errno));
        close(_serverFd);
        _serverFd = -1;
        unlink(_socketPath.c_str());
        return false;
    }

    LAppPal::PrintLogLn("[IPC] Listening on %s", _socketPath.c_str());
    return true;
}

void LAppIPC::Release()
{
    for (size_t i = 0; i < _clients.size(); i++)
    {
        close(_clients[i]);
    }
    _clients.clear();
    _clientBuffers.clear();

    if (_serverFd >= 0)
    {
        close(_serverFd);
        _serverFd = -1;
    }

    if (!_socketPath.empty())
    {
        unlink(_socketPath.c_str());
        _socketPath.clear();
    }
}

std::string& LAppIPC::GetClientBuffer(int fd)
{
    for (size_t i = 0; i < _clientBuffers.size(); i++)
    {
        if (_clientBuffers[i].fd == fd) return _clientBuffers[i].buf;
    }
    ClientBuffer cb;
    cb.fd = fd;
    _clientBuffers.push_back(cb);
    return _clientBuffers.back().buf;
}

void LAppIPC::RemoveClientBuffer(int fd)
{
    for (size_t i = 0; i < _clientBuffers.size(); i++)
    {
        if (_clientBuffers[i].fd == fd)
        {
            _clientBuffers.erase(_clientBuffers.begin() + i);
            return;
        }
    }
}

void LAppIPC::Poll()
{
    if (_serverFd < 0) return;

    // Accept new connections
    while (true)
    {
        int clientFd = accept(_serverFd, NULL, NULL);
        if (clientFd < 0) break;

        int flags = fcntl(clientFd, F_GETFL, 0);
        fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);
        _clients.push_back(clientFd);
    }

    // Process existing clients
    std::vector<int> toRemove;
    for (size_t i = 0; i < _clients.size(); i++)
    {
        int fd = _clients[i];
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);

        if (n > 0)
        {
            buf[n] = '\0';
            std::string& clientBuf = GetClientBuffer(fd);
            clientBuf += buf;

            // Process complete lines
            size_t pos;
            while ((pos = clientBuf.find('\n')) != std::string::npos)
            {
                std::string line = clientBuf.substr(0, pos);
                clientBuf.erase(0, pos + 1);

                if (!line.empty())
                {
                    std::string response = ProcessCommand(line);
                    response += "\n";
                    // Best-effort write
                    write(fd, response.c_str(), response.size());
                }
            }
        }
        else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
        {
            close(fd);
            RemoveClientBuffer(fd);
            toRemove.push_back(fd);
        }
    }

    // Remove disconnected clients
    for (size_t i = 0; i < toRemove.size(); i++)
    {
        _clients.erase(std::remove(_clients.begin(), _clients.end(), toRemove[i]), _clients.end());
    }
}

// ─── Minimal JSON helpers (no external dependency) ───────────────────────────
// We only need to produce JSON output and parse simple {"command":"...", ...} input.

static std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++)
    {
        char c = s[i];
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += c; break;
        }
    }
    return out;
}

// Very simple key extraction from a flat JSON object. Returns empty string if not found.
static std::string JsonGetString(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";

    // Skip whitespace
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos >= json.size()) return "";

    if (json[pos] == '"')
    {
        // String value
        pos++;
        std::string val;
        while (pos < json.size() && json[pos] != '"')
        {
            if (json[pos] == '\\' && pos + 1 < json.size())
            {
                pos++;
            }
            val += json[pos];
            pos++;
        }
        return val;
    }
    else
    {
        // Number or other literal
        std::string val;
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ' ')
        {
            val += json[pos];
            pos++;
        }
        return val;
    }
}

static float JsonGetFloat(const std::string& json, const std::string& key, float defaultVal)
{
    std::string val = JsonGetString(json, key);
    if (val.empty()) return defaultVal;
    return (float)atof(val.c_str());
}

static int JsonGetInt(const std::string& json, const std::string& key, int defaultVal)
{
    std::string val = JsonGetString(json, key);
    if (val.empty()) return defaultVal;
    return atoi(val.c_str());
}

// ─── Command handlers ─────────────────────────────────────────────────────────

std::string LAppIPC::ProcessCommand(const std::string& json)
{
    std::string command = JsonGetString(json, "command");

    if (command.empty())
    {
        return "{\"ok\":false,\"error\":\"missing 'command' field\"}";
    }

    LAppDelegate* app = LAppDelegate::GetInstance();
    LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();

    // ── get_available_models ──
    if (command == "get_available_models")
    {
        csmVector<csmString> dirs = mgr->GetModelDir();
        std::ostringstream oss;
        oss << "{\"ok\":true,\"models\":[";
        for (csmInt32 i = 0; i < (csmInt32)dirs.GetSize(); i++)
        {
            if (i > 0) oss << ",";
            oss << "\"" << JsonEscape(dirs[i].GetRawString()) << "\"";
        }
        oss << "]}";
        return oss.str();
    }

    // Every per-character command below takes an optional "character": <id>
    // field; omitting it defaults to character 0, so scripts written against
    // the old single-character API keep working unchanged.

    // ── get_current_model ──
    if (command == "get_current_model")
    {
        int charId = JsonGetInt(json, "character", 0);
        csmInt32 idx = mgr->GetCharacterModelDirIndex(charId);
        if (idx < 0)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        csmVector<csmString> dirs = mgr->GetModelDir();
        std::string name = (idx < (csmInt32)dirs.GetSize()) ? dirs[idx].GetRawString() : "";

        std::ostringstream oss;
        oss << "{\"ok\":true,\"character\":" << charId
            << ",\"model\":\"" << JsonEscape(name) << "\",\"index\":" << idx << "}";
        return oss.str();
    }

    // ── set_model ──
    if (command == "set_model")
    {
        int charId = JsonGetInt(json, "character", 0);
        std::string name = JsonGetString(json, "name");
        int index = JsonGetInt(json, "index", -1);

        if (!name.empty())
        {
            csmVector<csmString> dirs = mgr->GetModelDir();
            for (csmInt32 i = 0; i < (csmInt32)dirs.GetSize(); i++)
            {
                if (strcmp(dirs[i].GetRawString(), name.c_str()) == 0)
                {
                    index = i;
                    break;
                }
            }
            if (index < 0)
            {
                return "{\"ok\":false,\"error\":\"model not found\"}";
            }
        }

        if (index < 0 || index >= mgr->GetModelDirSize())
        {
            return "{\"ok\":false,\"error\":\"invalid index\"}";
        }

        if (mgr->GetCharacterModel(charId) == NULL)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        mgr->SetCharacterModel(charId, index);
        return "{\"ok\":true}";
    }

    // ── get_expressions ──
    if (command == "get_expressions")
    {
        int charId = JsonGetInt(json, "character", 0);
        LAppModel* model = mgr->GetCharacterModel(charId);
        if (!model)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        std::vector<std::string> exprs = model->GetExpressionIds();
        std::ostringstream oss;
        oss << "{\"ok\":true,\"expressions\":[";
        for (size_t i = 0; i < exprs.size(); i++)
        {
            if (i > 0) oss << ",";
            oss << "\"" << JsonEscape(exprs[i]) << "\"";
        }
        oss << "]}";
        return oss.str();
    }

    // ── set_expression ──
    if (command == "set_expression")
    {
        int charId = JsonGetInt(json, "character", 0);
        LAppModel* model = mgr->GetCharacterModel(charId);
        if (!model)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        std::string id = JsonGetString(json, "id");
        if (id.empty())
        {
            return "{\"ok\":false,\"error\":\"missing 'id'\"}";
        }

        model->SetExpression(id.c_str());
        return "{\"ok\":true}";
    }

    // ── get_motions ──
    if (command == "get_motions")
    {
        int charId = JsonGetInt(json, "character", 0);
        LAppModel* model = mgr->GetCharacterModel(charId);
        if (!model)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        std::vector<LAppModel::MotionInfo> motions = model->GetMotionList();
        std::ostringstream oss;
        oss << "{\"ok\":true,\"motions\":[";
        for (size_t i = 0; i < motions.size(); i++)
        {
            if (i > 0) oss << ",";
            oss << "{\"group\":\"" << JsonEscape(motions[i].group) << "\""
                << ",\"index\":" << motions[i].index
                << ",\"file\":\"" << JsonEscape(motions[i].file) << "\"}";
        }
        oss << "]}";
        return oss.str();
    }

    // ── do_motion ──
    if (command == "do_motion")
    {
        int charId = JsonGetInt(json, "character", 0);
        LAppModel* model = mgr->GetCharacterModel(charId);
        if (!model)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        std::string group = JsonGetString(json, "group");
        int index = JsonGetInt(json, "index", 0);
        int priority = JsonGetInt(json, "priority", LAppDefine::PriorityNormal);

        if (group.empty())
        {
            // Try random auto-motion
            model->StartRandomMotion(LAppDefine::MotionGroupTapBody, priority);
            return "{\"ok\":true}";
        }

        model->StartMotion(group.c_str(), index, priority);
        return "{\"ok\":true}";
    }

    // ── set_mouth_y ──
    if (command == "set_mouth_y")
    {
        float value = JsonGetFloat(json, "value", 0.0f);
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        _externalMouthY = value;
        _hasExternalMouthY = true;
        return "{\"ok\":true}";
    }

    // ── set_model_zoom ──
    if (command == "set_model_zoom")
    {
        int charId = JsonGetInt(json, "character", 0);
        if (mgr->GetCharacterModel(charId) == NULL)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        float zoom = JsonGetFloat(json, "value", -1.0f);
        if (zoom < 0.1f || zoom > 10.0f)
        {
            return "{\"ok\":false,\"error\":\"value must be 0.1..10.0\"}";
        }
        mgr->SetCharacterZoom(charId, zoom);
        return "{\"ok\":true}";
    }

    // ── set_model_position ──
    if (command == "set_model_position")
    {
        int charId = JsonGetInt(json, "character", 0);
        if (mgr->GetCharacterModel(charId) == NULL)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        float x = JsonGetFloat(json, "x", mgr->GetCharacterX(charId));
        float y = JsonGetFloat(json, "y", mgr->GetCharacterY(charId));
        mgr->SetCharacterPosition(charId, x, y);
        return "{\"ok\":true}";
    }

    // ── get_model_position ──
    if (command == "get_model_position")
    {
        int charId = JsonGetInt(json, "character", 0);
        if (mgr->GetCharacterModel(charId) == NULL)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        std::ostringstream oss;
        oss << "{\"ok\":true,\"x\":" << mgr->GetCharacterX(charId)
            << ",\"y\":" << mgr->GetCharacterY(charId)
            << ",\"zoom\":" << mgr->GetCharacterZoom(charId) << "}";
        return oss.str();
    }

    // ── list_characters ──
    if (command == "list_characters")
    {
        csmVector<csmString> dirs = mgr->GetModelDir();
        std::ostringstream oss;
        oss << "{\"ok\":true,\"characters\":[";
        csmInt32 count = mgr->GetCharacterCount();
        for (csmInt32 i = 0; i < count; i++)
        {
            int id = mgr->GetCharacterIdAt(i);
            csmInt32 idx = mgr->GetCharacterModelDirIndex(id);
            std::string name = (idx >= 0 && idx < (csmInt32)dirs.GetSize()) ? dirs[idx].GetRawString() : "";

            if (i > 0) oss << ",";
            oss << "{\"id\":" << id
                << ",\"model\":\"" << JsonEscape(name) << "\""
                << ",\"index\":" << idx
                << ",\"x\":" << mgr->GetCharacterX(id)
                << ",\"y\":" << mgr->GetCharacterY(id)
                << ",\"zoom\":" << mgr->GetCharacterZoom(id) << "}";
        }
        oss << "]}";
        return oss.str();
    }

    // ── add_character ──
    if (command == "add_character")
    {
        std::string name = JsonGetString(json, "name");
        int index = JsonGetInt(json, "index", -1);

        if (!name.empty())
        {
            csmVector<csmString> dirs = mgr->GetModelDir();
            for (csmInt32 i = 0; i < (csmInt32)dirs.GetSize(); i++)
            {
                if (strcmp(dirs[i].GetRawString(), name.c_str()) == 0)
                {
                    index = i;
                    break;
                }
            }
        }

        if (index < 0 || index >= mgr->GetModelDirSize())
        {
            return "{\"ok\":false,\"error\":\"model not found\"}";
        }

        bool hasPosition = json.find("\"x\"") != std::string::npos || json.find("\"y\"") != std::string::npos;
        float x = JsonGetFloat(json, "x", 0.0f);
        float y = JsonGetFloat(json, "y", 0.0f);
        float scale = JsonGetFloat(json, "scale", 1.0f);

        int id = mgr->AddCharacter(index, hasPosition, x, y, scale);
        if (id < 0)
        {
            return "{\"ok\":false,\"error\":\"failed to add character\"}";
        }

        std::ostringstream oss;
        oss << "{\"ok\":true,\"character\":" << id << "}";
        return oss.str();
    }

    // ── remove_character ──
    if (command == "remove_character")
    {
        int charId = JsonGetInt(json, "character", -1);
        if (!mgr->RemoveCharacter(charId))
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }
        return "{\"ok\":true}";
    }

    // ── toggle_hidden ──
    if (command == "toggle_hidden")
    {
        app->ToggleHidden();
        std::ostringstream oss;
        oss << "{\"ok\":true,\"hidden\":" << (app->_isHidden ? "true" : "false") << "}";
        return oss.str();
    }

    // ── set_look ──
    if (command == "set_look")
    {
        std::string reset = JsonGetString(json, "reset");
        if (reset == "true" || reset == "1")
        {
            _hasExternalLook = false;
            return "{\"ok\":true}";
        }
        float x = JsonGetFloat(json, "x", 0.0f);
        float y = JsonGetFloat(json, "y", 0.0f);
        _externalLookX = x;
        _externalLookY = y;
        _hasExternalLook = true;
        return "{\"ok\":true}";
    }

    // ── get_status ──
    if (command == "get_status")
    {
        csmVector<csmString> dirs = mgr->GetModelDir();

        // Top-level model/model_index/zoom/x/y mirror character 0, for
        // scripts written against the old single-character API.
        csmInt32 idx = mgr->GetCharacterModelDirIndex(0);
        std::string modelName = (idx >= 0 && idx < (csmInt32)dirs.GetSize()) ? dirs[idx].GetRawString() : "";

        std::ostringstream oss;
        oss << "{\"ok\":true"
            << ",\"model\":\"" << JsonEscape(modelName) << "\""
            << ",\"model_index\":" << idx
            << ",\"model_count\":" << mgr->GetModelDirSize()
            << ",\"hidden\":" << (app->_isHidden ? "true" : "false")
            << ",\"zoom\":" << mgr->GetCharacterZoom(0)
            << ",\"x\":" << mgr->GetCharacterX(0)
            << ",\"y\":" << mgr->GetCharacterY(0)
            << ",\"character_count\":" << mgr->GetCharacterCount()
            << "}";
        return oss.str();
    }

    // ── next_model ──
    if (command == "next_model")
    {
        int charId = JsonGetInt(json, "character", 0);
        mgr->NextCharacterModel(charId);
        return "{\"ok\":true}";
    }

    // ── prev_model ──
    if (command == "prev_model")
    {
        int charId = JsonGetInt(json, "character", 0);
        mgr->PrevCharacterModel(charId);
        return "{\"ok\":true}";
    }

    // ── switch_skin ──
    if (command == "switch_skin")
    {
        int charId = JsonGetInt(json, "character", 0);
        mgr->SwitchSkin(charId);
        return "{\"ok\":true}";
    }

    return "{\"ok\":false,\"error\":\"unknown command\"}";
}
