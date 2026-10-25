#include "cmdly_c.h"

#include <string.h>
#include <deque>
#include <memory>
#include <string>

#include <cmdly/completion.h>
#include <cmdly/history.h>
#include <cmdly/io.h>
#include <cmdly/terminal.h>

namespace {

// A non-interactive IO that replays a fixed script and captures what the editor
// writes. This is what makes the interop demo deterministic in CI.
class ScriptedIO : public cmdly::IO {
public:
    explicit ScriptedIO(std::string script)
        : _script(std::move(script)) {}

    cmdly::Key getKey() override {
        if (_pos < _script.size()) return cmdly::Key(_script[_pos++]);
        return cmdly::Key::Enter;
    }
    char getChar() override {
        // Answer the terminal "device status report" (ESC [ 6 n) that Cursor
        // issues; otherwise scripted input is served.
        if (!_pending.empty()) {
            const char c = _pending.front();
            _pending.pop_front();
            return c;
        }
        return _pos < _script.size() ? _script[_pos++] : '\0';
    }
    void write(const std::string &data) const override {
        _output += data;
        if (data.find("\033[6n") != std::string::npos) {
            const char response[] = "\033[1;1R";
            for (const char *c = response; *c != '\0'; ++c)
                _pending.push_back(*c);
        }
    }
    void getWindowSize(size_t *cols, size_t *rows) const override {
        *cols = 80;
        *rows = 24;
    }

    const std::string &output() const { return _output; }

private:
    std::string _script;
    size_t _pos = 0;
    mutable std::string _output;
    mutable std::deque<char> _pending;
};

struct TerminalHandle {
    std::shared_ptr<ScriptedIO> io;
    std::unique_ptr<cmdly::Terminal> terminal;
    std::string last_output;
};

TerminalHandle *handle(cmdly_terminal *terminal) { return reinterpret_cast<TerminalHandle *>(terminal); }
const TerminalHandle *handle(const cmdly_terminal *terminal) {
    return reinterpret_cast<const TerminalHandle *>(terminal);
}

// Bridges a C callback into cmdly's listener model.
class CallbackListener : public cmdly::KeyPressed {
public:
    CallbackListener(cmdly_key_handler handler, void *context)
        : _handler(handler)
        , _context(context) {}

    Status invoke(const cmdly::Key &key, cmdly::Line &, cmdly::Cursor &, cmdly::Terminal &) override {
        if (_handler != nullptr) _handler(_context, static_cast<uint32_t>(static_cast<unsigned char>(key.code())));
        return Status::OK;
    }

private:
    cmdly_key_handler _handler;
    void *_context;
};

}   // namespace

extern "C" cmdly_terminal *cmdly_terminal_create(const char *script) {
    auto *h = new TerminalHandle();
    h->io = std::make_shared<ScriptedIO>(script != nullptr ? script : "");
    auto history = std::make_shared<cmdly::MemoryHistory>();
    auto completion = std::make_shared<cmdly::Completion>();
    h->terminal = std::make_unique<cmdly::Terminal>(h->io, history, completion);
    return reinterpret_cast<cmdly_terminal *>(h);
}

extern "C" int32_t cmdly_terminal_destroy(cmdly_terminal *terminal) {
    delete handle(terminal);
    return 0;
}

extern "C" int32_t cmdly_completion_insert(cmdly_terminal *terminal, const char *word) {
    if (terminal != nullptr && word != nullptr) handle(terminal)->terminal->completion()->insert(word);
    return 0;
}

extern "C" int32_t cmdly_on_key(cmdly_terminal *terminal, uint32_t key_code, cmdly_key_handler handler, void *context) {
    if (terminal == nullptr) return -1;
    auto listener = std::make_shared<CallbackListener>(handler, context);
    if (key_code == 0)
        handle(terminal)->terminal->addKeyPressed(cmdly::Key::Any, listener);
    else
        handle(terminal)->terminal->addKeyPressed(cmdly::Key(static_cast<char>(key_code)), listener);
    return 0;
}

extern "C" int32_t cmdly_history_insert(cmdly_terminal *terminal, const char *line) {
    if (terminal != nullptr && line != nullptr) handle(terminal)->terminal->history()->insert(line);
    return 0;
}

extern "C" uint64_t cmdly_history_length(const cmdly_terminal *terminal) {
    return terminal != nullptr ? handle(terminal)->terminal->history()->length() : 0;
}

extern "C" int32_t cmdly_read_line(cmdly_terminal *terminal, const char *prompt, char *out, uint64_t capacity) {
    if (terminal == nullptr || out == nullptr) return -1;
    TerminalHandle *h = handle(terminal);
    const std::string line = h->terminal->readLine(prompt != nullptr ? prompt : "");
    h->last_output = h->io->output();
    if (line.size() + 1 > capacity) return -1;
    memcpy(out, line.data(), line.size());
    out[line.size()] = '\0';
    return static_cast<int32_t>(line.size());
}

extern "C" const char *cmdly_last_output(const cmdly_terminal *terminal) {
    return terminal != nullptr ? handle(terminal)->last_output.c_str() : "";
}
