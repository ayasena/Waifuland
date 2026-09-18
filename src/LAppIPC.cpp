#include "LAppIPC.hpp"
#include "LAppDelegate.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppModel.hpp"
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "JsonMini.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <sstream>
#include <algorithm>

using namespace Csm;

namespace {
    LAppIPC* s_instance = NULL;
}

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
                    if (!response.empty())
                    {
                        // Empty means "no reply" (lipsync-rate commands
                        // without "ack":true) — don't waste a write.
                        response += "\n";
                        // Best-effort write
                        write(fd, response.c_str(), response.size());
                    }
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

// ─── Minimal JSON output helper ─────────────────────────────────────────────
// Input parsing lives in JsonMini.hpp (shared with LAppConfig). Only output
// escaping stays here.

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

// Resolve which character a per-character command targets. An explicit
// "character" field wins; omitting it follows the first roster entry (NOT
// literal id 0 — ids are never reused, so 0 goes stale after the first
// removal). Returns -1 when the roster is empty.
static int ResolveCharacter(const JsonMini& req, LAppLive2DManager* mgr)
{
    if (req.Has("character"))
    {
        return req.GetInt("character", -1);
    }
    return mgr->GetCharacterIdAt(0);
}

// ─── Command handlers ─────────────────────────────────────────────────────────

std::string LAppIPC::ProcessCommand(const std::string& json)
{
    JsonMini req;
    if (!req.Parse(json))
    {
        return "{\"ok\":false,\"error\":\"invalid JSON\"}";
    }

    std::string command = req.GetString("command");

    if (command.empty())
    {
        return "{\"ok\":false,\"error\":\"missing 'command' field\"}";
    }

    LAppDelegate* app = LAppDelegate::GetInstance();
    LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();

    // ── get_available_models ──
    if (command == "get_available_models")
    {
        const csmVector<csmString>& dirs = mgr->GetModelDir();
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
    // field; omitting it follows the first roster entry (see
    // ResolveCharacter), so scripts written against the old
    // single-character API keep working unchanged.

    // ── get_current_model ──
    if (command == "get_current_model")
    {
        int charId = ResolveCharacter(req, mgr);
        csmInt32 idx = mgr->GetCharacterModelDirIndex(charId);
        if (idx < 0)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        const csmVector<csmString>& dirs = mgr->GetModelDir();
        std::string name = (idx < (csmInt32)dirs.GetSize()) ? dirs[idx].GetRawString() : "";

        std::ostringstream oss;
        oss << "{\"ok\":true,\"character\":" << charId
            << ",\"model\":\"" << JsonEscape(name) << "\",\"index\":" << idx << "}";
        return oss.str();
    }

    // ── set_model ──
    if (command == "set_model")
    {
        int charId = ResolveCharacter(req, mgr);
        std::string name = req.GetString("name");
        int index = req.GetInt("index", -1);

        if (!name.empty())
        {
            index = mgr->FindModelIndex(name.c_str());
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
        int charId = ResolveCharacter(req, mgr);
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
        int charId = ResolveCharacter(req, mgr);
        LAppModel* model = mgr->GetCharacterModel(charId);
        if (!model)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        std::string id = req.GetString("id");
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
        int charId = ResolveCharacter(req, mgr);
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
        int charId = ResolveCharacter(req, mgr);
        LAppModel* model = mgr->GetCharacterModel(charId);
        if (!model)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        std::string group = req.GetString("group");
        int index = req.GetInt("index", 0);
        int priority = req.GetInt("priority", LAppDefine::PriorityNormal);

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
    // Per-character lipsync. No reply unless "ack":true — at lipsync rate
    // the reply would be pure waste (the sender never reads it).
    if (command == "set_mouth_y")
    {
        int charId = ResolveCharacter(req, mgr);
        if (mgr->GetCharacterModel(charId) == NULL)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        float value = req.GetFloat("value", 0.0f);
        mgr->SetCharacterMouth(charId, value);
        if (req.GetBool("ack", false)) return "{\"ok\":true}";
        return "";
    }

    // ── set_mouth_batch ──
    // One syscall per audio chunk instead of one per character:
    // {"command":"set_mouth_batch","mouths":{"<id>":<0..1>, ...}}.
    // Unknown ids are skipped; like set_mouth_y, silent without "ack".
    if (command == "set_mouth_batch")
    {
        if (req.Has("mouths") && !req.WasString("mouths"))
        {
            JsonMini mouths;
            if (mouths.Parse(req.GetString("mouths")))
            {
                for (std::map<std::string, std::string>::const_iterator it = mouths.values.begin();
                     it != mouths.values.end(); ++it)
                {
                    char* end = NULL;
                    long id = strtol(it->first.c_str(), &end, 10);
                    if (end == it->first.c_str() || *end != '\0') continue;
                    if (mgr->GetCharacterModel((int)id) == NULL) continue;
                    char* vend = NULL;
                    float v = strtof(it->second.c_str(), &vend);
                    if (vend == it->second.c_str()) continue;
                    mgr->SetCharacterMouth((int)id, v);
                }
            }
        }
        if (req.GetBool("ack", false)) return "{\"ok\":true}";
        return "";
    }

    // ── set_model_zoom ──
    if (command == "set_model_zoom")
    {
        int charId = ResolveCharacter(req, mgr);
        if (mgr->GetCharacterModel(charId) == NULL)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        float zoom = req.GetFloat("value", -1.0f);
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
        int charId = ResolveCharacter(req, mgr);
        if (mgr->GetCharacterModel(charId) == NULL)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        float x = req.GetFloat("x", mgr->GetCharacterX(charId));
        float y = req.GetFloat("y", mgr->GetCharacterY(charId));
        mgr->SetCharacterPosition(charId, x, y);
        return "{\"ok\":true}";
    }

    // ── get_model_position ──
    if (command == "get_model_position")
    {
        int charId = ResolveCharacter(req, mgr);
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
        const csmVector<csmString>& dirs = mgr->GetModelDir();
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
        std::string name = req.GetString("name");
        int index = req.GetInt("index", -1);

        if (!name.empty())
        {
            index = mgr->FindModelIndex(name.c_str());
        }

        if (index < 0 || index >= mgr->GetModelDirSize())
        {
            return "{\"ok\":false,\"error\":\"model not found\"}";
        }

        // Positioned when an explicit x or y was supplied; a single
        // supplied axis pins the other to 0 (same rule as config parsing).
        bool hasPosition = req.Has("x") || req.Has("y");
        float x = req.GetFloat("x", 0.0f);
        float y = req.GetFloat("y", 0.0f);
        float scale = req.GetFloat("scale", 1.0f);

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
        int charId = req.GetInt("character", -1);
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
    // Per-character look override. A reset clears only the targeted
    // character (default: first roster entry), not every head on screen.
    if (command == "set_look")
    {
        int charId = ResolveCharacter(req, mgr);
        if (mgr->GetCharacterModel(charId) == NULL)
        {
            return "{\"ok\":false,\"error\":\"no such character\"}";
        }

        if (req.GetBool("reset", false))
        {
            mgr->ClearCharacterLook(charId);
            return "{\"ok\":true}";
        }
        float x = req.GetFloat("x", 0.0f);
        float y = req.GetFloat("y", 0.0f);
        mgr->SetCharacterLook(charId, x, y);
        return "{\"ok\":true}";
    }

    // ── get_status ──
    if (command == "get_status")
    {
        const csmVector<csmString>& dirs = mgr->GetModelDir();

        // Top-level model/model_index/zoom/x/y mirror the first roster
        // entry, for scripts written against the old single-character API.
        int firstId = mgr->GetCharacterIdAt(0);
        csmInt32 idx = mgr->GetCharacterModelDirIndex(firstId);
        std::string modelName = (idx >= 0 && idx < (csmInt32)dirs.GetSize()) ? dirs[idx].GetRawString() : "";

        std::ostringstream oss;
        oss << "{\"ok\":true"
            << ",\"model\":\"" << JsonEscape(modelName) << "\""
            << ",\"model_index\":" << idx
            << ",\"model_count\":" << mgr->GetModelDirSize()
            << ",\"hidden\":" << (app->_isHidden ? "true" : "false")
            << ",\"zoom\":" << mgr->GetCharacterZoom(firstId)
            << ",\"x\":" << mgr->GetCharacterX(firstId)
            << ",\"y\":" << mgr->GetCharacterY(firstId)
            << ",\"character_count\":" << mgr->GetCharacterCount()
            << "}";
        return oss.str();
    }

    // ── next_model ──
    if (command == "next_model")
    {
        int charId = ResolveCharacter(req, mgr);
        mgr->NextCharacterModel(charId);
        return "{\"ok\":true}";
    }

    // ── prev_model ──
    if (command == "prev_model")
    {
        int charId = ResolveCharacter(req, mgr);
        mgr->PrevCharacterModel(charId);
        return "{\"ok\":true}";
    }

    // ── switch_skin ──
    if (command == "switch_skin")
    {
        int charId = ResolveCharacter(req, mgr);
        mgr->SwitchSkin(charId);
        return "{\"ok\":true}";
    }

    return "{\"ok\":false,\"error\":\"unknown command\"}";
}
