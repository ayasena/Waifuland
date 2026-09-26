/**
 * @brief IPC server for controlling Waifuland from external programs.
 *
 * Listens on a Unix domain socket and accepts newline-delimited JSON commands.
 * Socket path: $XDG_RUNTIME_DIR/waifuland.sock (fallback: /tmp/waifuland.sock)
 */

#pragma once

#include <string>
#include <vector>

class LAppIPC
{
public:
    static LAppIPC* GetInstance();
    static void ReleaseInstance();

    /**
     * @brief Initialize the IPC socket server.
     * @return true on success
     */
    bool Initialize();

    /**
     * @brief Poll for incoming connections and process commands.
     *        Call this once per frame from the main loop.
     */
    void Poll();

    /**
     * @brief Clean up the socket.
     */
    void Release();

    /**
     * @brief Get the socket path being used.
     */
    const std::string& GetSocketPath() const { return _socketPath; }

    /**
     * @brief Where the characters should look instead of the real cursor
     *        (global logical pixels), set by `set_gaze_target`. False once
     *        the sender stops updating it (stale after half a second), so
     *        the gaze falls back to the real cursor by itself.
     */
    static bool GazeTarget(int& x, int& y);

private:
    LAppIPC();
    ~LAppIPC();

    std::string ProcessCommand(const std::string& json);
    void HandleClient(int clientFd);

    static int _gazeX;
    static int _gazeY;
    static double _gazeAt;  ///< steady-clock seconds of the last update; < 0 = none

    int _serverFd;
    std::string _socketPath;
    std::vector<int> _clients;

    // Partial read buffers per client fd
    struct ClientBuffer {
        int fd;
        std::string buf;
    };
    std::vector<ClientBuffer> _clientBuffers;

    std::string& GetClientBuffer(int fd);
    void RemoveClientBuffer(int fd);
};
