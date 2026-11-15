#include "retroachievements.hpp"

#include "ps2/memory.hpp"

extern "C" {
#include "rc_client.h"
#include "rc_consoles.h"
}

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <signal.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace nfshp2::achievements {
namespace {
struct Request {
    std::string url, post, content_type;
    bool has_post = false;
    rc_client_server_callback_t callback = nullptr;
    void* callback_data = nullptr;
};
struct Reply {
    Request request;
    std::string body;
    int status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
};

rc_client_t* client = nullptr;
std::mutex mutex;
std::condition_variable wake;
std::deque<Request> pending;
std::deque<Reply> completed;
std::deque<std::string> notices;
std::thread network_thread;
bool stopping = false;
std::atomic<bool> reset_on_frame{false};
bool login_done = false, login_ok = false;
bool game_done = false, game_ok = false;
std::chrono::steady_clock::time_point last_idle;

void notice(std::string message) {
    std::fprintf(stderr, "[ra] %s\n", message.c_str());
    std::lock_guard lock(mutex);
    notices.push_back(std::move(message));
}

// RetroAchievements PS2 addresses: 32 MiB EE RAM followed by 16 KiB scratchpad.
uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t size, rc_client_t*) {
    constexpr uint32_t ram = ps2::kRamSize, spr = ps2::kScratchpadSize;
    if (address < ram && size <= ram - address) {
        std::memcpy(buffer, ps2::g_ram + address, size);
        return size;
    }
    if (address >= ram && address < ram + spr && size <= ram + spr - address) {
        std::memcpy(buffer, ps2::g_spr + address - ram, size);
        return size;
    }
    return 0;
}

// Spawn curl without a shell. POST data, including the login token, goes to
// stdin, so it never appears in the process arguments or shell history.
Reply perform(Request request) {
    Reply reply;
    reply.request = std::move(request);
    std::string url = reply.request.url;
    if (url.starts_with("http://")) url.replace(0, 7, "https://");
    if (!url.starts_with("https://")) return reply;
    int input[2], output[2];
    if (pipe(input)) return reply;
    if (pipe(output)) { close(input[0]); close(input[1]); return reply; }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, input[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, input[1]);
    posix_spawn_file_actions_addclose(&actions, output[0]);
    std::vector<char*> args;
    auto add = [&](const char* text) { args.push_back(const_cast<char*>(text)); };
    add("curl"); add("--silent"); add("--show-error"); add("--location");
    add("--max-time"); add("15"); add("--connect-timeout"); add("5");
    add("--proto"); add("=https"); add("--proto-redir"); add("=https");
    add("--user-agent"); add("nfshp2-recomp/0.1 (Linux) rcheevos/12.4.0");
    add("--write-out"); add("\n%{http_code}");
    if (reply.request.has_post) {
        add("--data-binary"); add("@-");
        if (!reply.request.content_type.empty()) {
            add("--header");
            args.push_back(reply.request.content_type.data());
        }
    }
    args.push_back(url.data());
    args.push_back(nullptr);
    pid_t pid = -1;
    const int started = posix_spawnp(&pid, "curl", &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(input[0]); close(output[1]);
    if (started != 0) { close(input[1]); close(output[0]); return reply; }
    if (reply.request.has_post) {
        const char* data = reply.request.post.data();
        size_t left = reply.request.post.size();
        while (left) {
            const ssize_t count = write(input[1], data, left);
            if (count <= 0) break;
            data += count; left -= static_cast<size_t>(count);
        }
    }
    close(input[1]);
    char block[4096];
    while (reply.body.size() < 8 * 1024 * 1024) {
        const ssize_t count = read(output[0], block, sizeof block);
        if (count <= 0) break;
        reply.body.append(block, static_cast<size_t>(count));
    }
    close(output[0]);
    int process_status = 0;
    while (waitpid(pid, &process_status, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(process_status) && WEXITSTATUS(process_status) == 0 && reply.body.size() >= 4) {
        const size_t mark = reply.body.rfind('\n');
        if (mark != std::string::npos && reply.body.size() == mark + 4) {
            const std::string code = reply.body.substr(mark + 1);
            if (std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                reply.status = std::atoi(code.c_str());
                reply.body.resize(mark);
            }
        }
    }
    return reply;
}

void worker() {
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
    for (;;) {
        Request request;
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, [] { return stopping || !pending.empty(); });
            if (pending.empty() && stopping) return;
            request = std::move(pending.front());
            pending.pop_front();
        }
        Reply reply = perform(std::move(request));
        {
            std::lock_guard lock(mutex);
            completed.push_back(std::move(reply));
        }
        wake.notify_all();
    }
}

void server_call(const rc_api_request_t* request, rc_client_server_callback_t callback, void* data, rc_client_t*) {
    Request job;
    job.url = request->url ? request->url : "";
    job.has_post = request->post_data != nullptr;
    if (job.has_post) job.post = request->post_data;
    if (request->content_type) job.content_type = std::string("Content-Type: ") + request->content_type;
    job.callback = callback;
    job.callback_data = data;
    {
        std::lock_guard lock(mutex);
        pending.push_back(std::move(job));
    }
    wake.notify_all();
}

void deliver() {
    std::deque<Reply> replies;
    {
        std::lock_guard lock(mutex);
        replies.swap(completed);
    }
    for (auto& reply : replies) {
        const bool received = reply.status >= 0;
        const rc_api_server_response_t response{received ? reply.body.c_str() : nullptr,
                                                 received ? reply.body.size() : 0, reply.status};
        reply.request.callback(&response, reply.request.callback_data);
    }
}

void logged_in(int result, const char* error, rc_client_t* instance, void*) {
    login_ok = result == RC_OK;
    login_done = true;
    if (login_ok) {
        const auto* user = rc_client_get_user_info(instance);
        notice(std::string("Signed in as ") + (user ? user->display_name : "player") + " (casual mode)");
    } else notice(std::string("Login failed: ") + (error ? error : rc_error_str(result)));
}

void game_loaded(int result, const char* error, rc_client_t* instance, void*) {
    game_ok = result == RC_OK;
    game_done = true;
    if (game_ok) {
        const auto* game = rc_client_get_game_info(instance);
        notice(std::string("Achievements active: ") + (game ? game->title : "game"));
    } else notice(std::string("Game identification failed: ") + (error ? error : rc_error_str(result)));
}

void event(const rc_client_event_t* value, rc_client_t*) {
    if (value->type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED && value->achievement)
        notice(std::string("Achievement unlocked: ") + value->achievement->title);
    else if (value->type == RC_CLIENT_EVENT_SERVER_ERROR && value->server_error)
        notice(std::string("RetroAchievements server error: ") +
               (value->server_error->error_message ? value->server_error->error_message : "unknown error"));
}

bool await(bool& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        deliver();
        if (!done) {
            std::unique_lock lock(mutex);
            wake.wait_for(lock, std::chrono::milliseconds(20));
        }
    }
    return done;
}
} // namespace

bool start(const std::string& disc_path) {
    const char* path = std::getenv("NFSHP2_RA_CREDENTIALS");
    if (!path || !*path) return false;
    struct stat info{};
    if (lstat(path, &info) || (info.st_mode & 077) != 0 || !S_ISREG(info.st_mode)) {
        std::fprintf(stderr, "[ra] credentials missing or not private (mode 0600): %s\n", path);
        return false;
    }
    std::ifstream file(path);
    std::string username, token;
    std::getline(file, username); std::getline(file, token);
    if (username.empty() || token.empty() || disc_path.empty()) return false;
    stopping = login_done = login_ok = game_done = game_ok = false;
    client = rc_client_create(read_memory, server_call);
    if (!client) return false;
    rc_client_set_hardcore_enabled(client, 0);
    rc_client_set_event_handler(client, event);
    network_thread = std::thread(worker);
    rc_client_begin_login_with_token(client, username.c_str(), token.c_str(), logged_in, nullptr);
    if (!await(login_done) || !login_ok) { shutdown(); return false; }
    // Identify the unmodified disc. The patched ELF has a different hash.
    rc_client_begin_identify_and_load_game(client, RC_CONSOLE_PLAYSTATION_2,
                                           disc_path.c_str(), nullptr, 0, game_loaded, nullptr);
    if (!await(game_done) || !game_ok) { shutdown(); return false; }
    last_idle = std::chrono::steady_clock::now();
    return true;
}

void frame() {
    if (!client || !game_ok) return;
    deliver();
    if (reset_on_frame.exchange(false)) rc_client_reset(client);
    rc_client_do_frame(client);
    last_idle = std::chrono::steady_clock::now();
}

void state_loaded() { reset_on_frame.store(true); }

void idle() {
    if (!client || !game_ok) return;
    deliver();
    const auto now = std::chrono::steady_clock::now();
    if (now - last_idle >= std::chrono::seconds(1)) {
        rc_client_idle(client);
        last_idle = now;
    }
}

std::string take_notice() {
    std::lock_guard lock(mutex);
    if (notices.empty()) return {};
    std::string message = std::move(notices.front());
    notices.pop_front();
    return message;
}

void shutdown() {
    if (!client) return;
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    wake.notify_all();
    if (network_thread.joinable()) network_thread.join();
    rc_client_destroy(client);
    client = nullptr;
    std::lock_guard lock(mutex);
    pending.clear(); completed.clear(); notices.clear();
}
} // namespace nfshp2::achievements
