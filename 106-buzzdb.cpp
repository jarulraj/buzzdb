#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// BuzzDB v106: timers, workloads, and closed-loop client workers.
//
// DSLabs clients are closed-loop: one outstanding command at a time. This
// version adds that testing model, plus logical timers for retries and
// rate-limited workloads.

struct Address {
    std::string root;
    std::vector<std::string> path;

    explicit Address(std::string root_id = "") : root(std::move(root_id)) {}

    Address(std::string root_id, std::vector<std::string> sub_path)
        : root(std::move(root_id)), path(std::move(sub_path)) {}

    Address rootAddress() const {
        return Address(root);
    }

    std::string str() const {
        std::ostringstream out;
        out << root;
        for (const auto& part : path) {
            out << "/" << part;
        }
        return out.str();
    }

    bool operator<(const Address& other) const {
        return str() < other.str();
    }

    bool operator==(const Address& other) const {
        return root == other.root && path == other.path;
    }
};

std::ostream& operator<<(std::ostream& out, const Address& address) {
    out << address.str();
    return out;
}

struct PingCommand {
    std::string value;
};

struct PutCommand {
    std::string key;
    std::string value;
};

struct GetCommand {
    std::string key;
};

struct AppendCommand {
    std::string key;
    std::string value;
};

using Command = std::variant<PingCommand, PutCommand, GetCommand, AppendCommand>;

struct PingResult {
    std::string value;
};

struct PutOkResult {};

struct GetResult {
    std::string value;
};

struct KeyNotFoundResult {};

struct AppendResult {
    std::string value;
};

using Result = std::variant<
    PingResult,
    PutOkResult,
    GetResult,
    KeyNotFoundResult,
    AppendResult>;

bool operator==(const PingResult& lhs, const PingResult& rhs) {
    return lhs.value == rhs.value;
}

bool operator==(const PutOkResult&, const PutOkResult&) {
    return true;
}

bool operator==(const GetResult& lhs, const GetResult& rhs) {
    return lhs.value == rhs.value;
}

bool operator==(const KeyNotFoundResult&, const KeyNotFoundResult&) {
    return true;
}

bool operator==(const AppendResult& lhs, const AppendResult& rhs) {
    return lhs.value == rhs.value;
}

bool operator==(const Result& lhs, const Result& rhs) {
    return lhs.index() == rhs.index() &&
           std::visit(
               [](const auto& l, const auto& r) {
                   using L = std::decay_t<decltype(l)>;
                   using R = std::decay_t<decltype(r)>;
                   if constexpr (std::is_same_v<L, R>) {
                       return l == r;
                   }
                   return false;
               },
               lhs,
               rhs);
}

std::string describeCommand(const Command& command) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, PingCommand>) {
                out << "Ping(" << value.value << ")";
            } else if constexpr (std::is_same_v<T, PutCommand>) {
                out << "Put(" << value.key << ", " << value.value << ")";
            } else if constexpr (std::is_same_v<T, GetCommand>) {
                out << "Get(" << value.key << ")";
            } else if constexpr (std::is_same_v<T, AppendCommand>) {
                out << "Append(" << value.key << ", " << value.value << ")";
            }
            return out.str();
        },
        command);
}

std::string describeResult(const Result& result) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, PingResult>) {
                out << "Pong(" << value.value << ")";
            } else if constexpr (std::is_same_v<T, PutOkResult>) {
                out << "PutOk";
            } else if constexpr (std::is_same_v<T, GetResult>) {
                out << "GetResult(" << value.value << ")";
            } else if constexpr (std::is_same_v<T, KeyNotFoundResult>) {
                out << "KeyNotFound";
            } else if constexpr (std::is_same_v<T, AppendResult>) {
                out << "AppendResult(" << value.value << ")";
            }
            return out.str();
        },
        result);
}

class Application {
public:
    virtual ~Application() = default;
    virtual Result execute(const Command& command) = 0;
    virtual std::unique_ptr<Application> clone() const = 0;
    virtual std::string describe() const = 0;
};

class PingApplication : public Application {
public:
    Result execute(const Command& command) override {
        if (const auto* ping = std::get_if<PingCommand>(&command)) {
            ++num_executed_;
            return PingResult{"pong(" + ping->value + ")"};
        }
        throw std::runtime_error("PingApplication received an unknown command.");
    }

    std::unique_ptr<Application> clone() const override {
        return std::make_unique<PingApplication>(*this);
    }

    std::string describe() const override {
        return "PingApplication{executed=" + std::to_string(num_executed_) + "}";
    }

private:
    int num_executed_ = 0;
};

class KVApplication : public Application {
public:
    Result execute(const Command& command) override {
        if (const auto* put = std::get_if<PutCommand>(&command)) {
            data_[put->key] = put->value;
            return PutOkResult{};
        }
        if (const auto* get = std::get_if<GetCommand>(&command)) {
            auto it = data_.find(get->key);
            if (it == data_.end()) {
                return KeyNotFoundResult{};
            }
            return GetResult{it->second};
        }
        if (const auto* append = std::get_if<AppendCommand>(&command)) {
            std::string& value = data_[append->key];
            value += append->value;
            return AppendResult{value};
        }
        throw std::runtime_error("KVApplication received an unknown command.");
    }

    std::unique_ptr<Application> clone() const override {
        return std::make_unique<KVApplication>(*this);
    }

    std::string describe() const override {
        return "KVApplication{keys=" + std::to_string(data_.size()) + "}";
    }

private:
    std::map<std::string, std::string> data_;
};

class KVOracle {
public:
    Result execute(const Command& command) {
        if (const auto* put = std::get_if<PutCommand>(&command)) {
            data_[put->key] = put->value;
            return PutOkResult{};
        }
        if (const auto* get = std::get_if<GetCommand>(&command)) {
            auto it = data_.find(get->key);
            if (it == data_.end()) {
                return KeyNotFoundResult{};
            }
            return GetResult{it->second};
        }
        if (const auto* append = std::get_if<AppendCommand>(&command)) {
            std::string& value = data_[append->key];
            value += append->value;
            return AppendResult{value};
        }
        throw std::runtime_error("KVOracle received an unknown command.");
    }

    std::vector<Result> executeAll(const std::vector<Command>& commands) {
        std::vector<Result> results;
        results.reserve(commands.size());
        for (const auto& command : commands) {
            results.push_back(execute(command));
        }
        return results;
    }

private:
    std::map<std::string, std::string> data_;
};

std::vector<Result> kvOracleResults(const std::vector<Command>& commands) {
    KVOracle oracle;
    return oracle.executeAll(commands);
}

struct ClientRequest {
    int client_id;
    int request_id;
    Command command;
};

struct ClientReply {
    int client_id;
    int request_id;
    Result result;
};

using Message = std::variant<ClientRequest, ClientReply>;

struct RetryTimer {
    int request_id;
};

struct InterRequestTimer {};

using Timer = std::variant<RetryTimer, InterRequestTimer>;

std::string describeMessage(const Message& message) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ClientRequest>) {
                out << "ClientRequest(client=" << value.client_id
                    << ", req=" << value.request_id
                    << ", " << describeCommand(value.command) << ")";
            } else if constexpr (std::is_same_v<T, ClientReply>) {
                out << "ClientReply(client=" << value.client_id
                    << ", req=" << value.request_id
                    << ", " << describeResult(value.result) << ")";
            }
            return out.str();
        },
        message);
}

std::string describeTimer(const Timer& timer) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, RetryTimer>) {
                out << "RetryTimer(req=" << value.request_id << ")";
            } else if constexpr (std::is_same_v<T, InterRequestTimer>) {
                out << "InterRequestTimer";
            }
            return out.str();
        },
        timer);
}

struct MessageEnvelope {
    Address from;
    Address to;
    Message message;
};

struct TimerEnvelope {
    Address to;
    Timer timer;
    uint64_t min_delay_ms;
    uint64_t max_delay_ms;
    uint64_t set_order;
};

using EventBody = std::variant<MessageEnvelope, TimerEnvelope>;

struct ScheduledEvent {
    uint64_t deliver_at_ms;
    uint64_t order;
    EventBody body;
};

class SimState;

class NodeContext {
public:
    NodeContext(SimState& state, Address self)
        : state_(state), self_(std::move(self)) {}

    const Address& self() const {
        return self_;
    }

    void send(const Address& to, Message message);
    void setTimer(Timer timer, uint64_t delay_ms);
    void setTimer(Timer timer, uint64_t min_delay_ms, uint64_t max_delay_ms);

private:
    SimState& state_;
    Address self_;
};

class Node {
public:
    explicit Node(Address address) : address_(std::move(address)) {}
    virtual ~Node() = default;

    const Address& address() const {
        return address_;
    }

    virtual void init(NodeContext& ctx) {
        (void) ctx;
    }

    virtual void onMessage(NodeContext& ctx,
                           const Address& from,
                           const Message& message) = 0;

    virtual void onTimer(NodeContext& ctx, const Timer& timer) {
        (void) ctx;
        (void) timer;
    }

    virtual std::unique_ptr<Node> clone() const = 0;
    virtual std::string describe() const = 0;

private:
    Address address_;
};

class Workload {
public:
    Workload(std::vector<Command> commands,
             std::vector<Result> expected_results,
             uint64_t millis_between_requests = 0)
        : commands_(std::move(commands)),
          expected_results_(std::move(expected_results)),
          millis_between_requests_(millis_between_requests) {
        if (!expected_results_.empty() &&
            expected_results_.size() != commands_.size()) {
            throw std::runtime_error("Workload commands/results size mismatch.");
        }
    }

    bool hasNext() const {
        return index_ < commands_.size();
    }

    bool hasExpectedResults() const {
        return !expected_results_.empty();
    }

    Command nextCommand() {
        if (!hasNext()) {
            throw std::runtime_error("Workload is finished.");
        }
        return commands_[index_++];
    }

    Result expectedResult(size_t index) const {
        return expected_results_.at(index);
    }

    size_t expectedCount() const {
        return expected_results_.size();
    }

    uint64_t millisBetweenRequests() const {
        return millis_between_requests_;
    }

    void reset() {
        index_ = 0;
    }

private:
    std::vector<Command> commands_;
    std::vector<Result> expected_results_;
    uint64_t millis_between_requests_;
    size_t index_ = 0;
};

class SimState {
public:
    void addNode(std::unique_ptr<Node> node) {
        Address address = node->address();
        nodes_.emplace(address.rootAddress(), std::move(node));
    }

    void initAll() {
        for (auto& entry : nodes_) {
            NodeContext ctx(*this, entry.first);
            entry.second->init(ctx);
        }
    }

    void send(const Address& from, const Address& to, Message message) {
        MessageEnvelope envelope{from, to, std::move(message)};
        trace_.push_back(timestamp() + " enqueue " + describe(envelope));
        schedule(1, std::move(envelope));
    }

    void setTimer(const Address& to,
                  Timer timer,
                  uint64_t min_delay_ms,
                  uint64_t max_delay_ms) {
        if (min_delay_ms > max_delay_ms) {
            throw std::runtime_error("Timer minimum delay exceeds maximum delay.");
        }
        TimerEnvelope envelope{
            to,
            std::move(timer),
            min_delay_ms,
            max_delay_ms,
            next_timer_order_++
        };
        trace_.push_back(timestamp() + " set " + describe(envelope));
        schedule(min_delay_ms, std::move(envelope));
    }

    bool step() {
        if (events_.empty()) {
            return false;
        }

        auto next = std::min_element(
            events_.begin(),
            events_.end(),
            [](const ScheduledEvent& lhs, const ScheduledEvent& rhs) {
                if (lhs.deliver_at_ms != rhs.deliver_at_ms) {
                    return lhs.deliver_at_ms < rhs.deliver_at_ms;
                }
                return lhs.order < rhs.order;
            });

        ScheduledEvent event = std::move(*next);
        events_.erase(next);
        now_ms_ = event.deliver_at_ms;

        if (const auto* message = std::get_if<MessageEnvelope>(&event.body)) {
            deliver(*message);
        } else {
            deliver(std::get<TimerEnvelope>(event.body));
        }
        return true;
    }

    size_t run(size_t max_steps = 100000) {
        size_t steps = 0;
        while (steps < max_steps && step()) {
            ++steps;
        }
        return steps;
    }

    size_t pendingEvents() const {
        return events_.size();
    }

    const Node* node(const Address& address) const {
        auto it = nodes_.find(address.rootAddress());
        if (it == nodes_.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    template <typename T>
    const T* nodeAs(const Address& address) const {
        return dynamic_cast<const T*>(node(address));
    }

    void printTrace() const {
        std::cout << "Trace:" << std::endl;
        for (const auto& line : trace_) {
            std::cout << "  " << line << std::endl;
        }
    }

    void printNodes() const {
        std::cout << "Nodes:" << std::endl;
        for (const auto& entry : nodes_) {
            std::cout << "  " << entry.first << " -> "
                      << entry.second->describe() << std::endl;
        }
    }

private:
    void schedule(uint64_t delay_ms, EventBody body) {
        events_.push_back(ScheduledEvent{
            now_ms_ + delay_ms,
            next_event_order_++,
            std::move(body)
        });
    }

    void deliver(const MessageEnvelope& envelope) {
        trace_.push_back(timestamp() + " deliver " + describe(envelope));
        auto it = nodes_.find(envelope.to.rootAddress());
        if (it == nodes_.end()) {
            trace_.push_back(timestamp() + " drop missing destination " +
                             envelope.to.str());
            return;
        }

        NodeContext ctx(*this, envelope.to.rootAddress());
        it->second->onMessage(ctx, envelope.from, envelope.message);
    }

    void deliver(const TimerEnvelope& envelope) {
        trace_.push_back(timestamp() + " fire " + describe(envelope));
        auto it = nodes_.find(envelope.to.rootAddress());
        if (it == nodes_.end()) {
            trace_.push_back(timestamp() + " ignore timer for missing destination " +
                             envelope.to.str());
            return;
        }

        NodeContext ctx(*this, envelope.to.rootAddress());
        it->second->onTimer(ctx, envelope.timer);
    }

    std::string timestamp() const {
        return "t=" + std::to_string(now_ms_);
    }

    static std::string describe(const MessageEnvelope& envelope) {
        std::ostringstream out;
        out << envelope.from << " -> " << envelope.to << " "
            << describeMessage(envelope.message);
        return out.str();
    }

    static std::string describe(const TimerEnvelope& envelope) {
        std::ostringstream out;
        out << "timer -> " << envelope.to << " "
            << describeTimer(envelope.timer)
            << " min=" << envelope.min_delay_ms
            << " max=" << envelope.max_delay_ms;
        return out.str();
    }

    std::map<Address, std::unique_ptr<Node>> nodes_;
    std::vector<ScheduledEvent> events_;
    std::vector<std::string> trace_;
    uint64_t now_ms_ = 0;
    uint64_t next_event_order_ = 1;
    uint64_t next_timer_order_ = 1;
};

void NodeContext::send(const Address& to, Message message) {
    state_.send(self_, to, std::move(message));
}

void NodeContext::setTimer(Timer timer, uint64_t delay_ms) {
    setTimer(std::move(timer), delay_ms, delay_ms);
}

void NodeContext::setTimer(Timer timer,
                           uint64_t min_delay_ms,
                           uint64_t max_delay_ms) {
    state_.setTimer(self_, std::move(timer), min_delay_ms, max_delay_ms);
}

class ApplicationServer : public Node {
public:
    ApplicationServer(Address address, std::unique_ptr<Application> app)
        : Node(std::move(address)), app_(std::move(app)) {}

    void onMessage(NodeContext& ctx,
                   const Address& from,
                   const Message& message) override {
        if (const auto* request = std::get_if<ClientRequest>(&message)) {
            Result result = app_->execute(request->command);
            ctx.send(from, ClientReply{
                request->client_id,
                request->request_id,
                result
            });
        }
    }

    std::unique_ptr<Node> clone() const override {
        return std::make_unique<ApplicationServer>(address(), app_->clone());
    }

    std::string describe() const override {
        return "ApplicationServer{" + app_->describe() + "}";
    }

private:
    std::unique_ptr<Application> app_;
};

class ClientWorker : public Node {
public:
    ClientWorker(Address address,
                 Address server,
                 int client_id,
                 Workload workload)
        : Node(std::move(address)),
          server_(std::move(server)),
          client_id_(client_id),
          workload_(std::move(workload)) {}

    void init(NodeContext& ctx) override {
        sendNextCommand(ctx);
    }

    void onMessage(NodeContext& ctx,
                   const Address& from,
                   const Message& message) override {
        (void) from;
        const auto* reply = std::get_if<ClientReply>(&message);
        if (reply == nullptr) {
            return;
        }
        if (!waiting_ ||
            reply->client_id != client_id_ ||
            reply->request_id != in_flight_request_id_) {
            ++stale_replies_;
            return;
        }

        results_.push_back(reply->result);
        waiting_ = false;
        in_flight_request_id_ = 0;

        if (workload_.millisBetweenRequests() > 0 && workload_.hasNext()) {
            waiting_to_send_ = true;
            ctx.setTimer(InterRequestTimer{}, workload_.millisBetweenRequests());
        } else {
            sendNextCommand(ctx);
        }
    }

    void onTimer(NodeContext& ctx, const Timer& timer) override {
        if (std::holds_alternative<InterRequestTimer>(timer)) {
            if (waiting_to_send_) {
                waiting_to_send_ = false;
                sendNextCommand(ctx);
            }
            return;
        }

        const auto* retry = std::get_if<RetryTimer>(&timer);
        if (retry != nullptr &&
            waiting_ &&
            retry->request_id == in_flight_request_id_) {
            ++retries_;
            sendInFlight(ctx);
        }
    }

    std::unique_ptr<Node> clone() const override {
        return std::make_unique<ClientWorker>(*this);
    }

    bool done() const {
        return !waiting_ && !waiting_to_send_ && !workload_.hasNext();
    }

    size_t resultCount() const {
        return results_.size();
    }

    size_t sentCount() const {
        return sent_commands_.size();
    }

    const std::vector<Command>& sentCommands() const {
        return sent_commands_;
    }

    const std::vector<Result>& results() const {
        return results_;
    }

    int retryCount() const {
        return retries_;
    }

    int staleReplyCount() const {
        return stale_replies_;
    }

    bool resultsOk() const {
        if (!workload_.hasExpectedResults()) {
            return true;
        }
        if (results_.size() > workload_.expectedCount()) {
            return false;
        }
        for (size_t i = 0; i < results_.size(); ++i) {
            if (!(results_[i] == workload_.expectedResult(i))) {
                return false;
            }
        }
        return true;
    }

    std::string describe() const override {
        std::ostringstream out;
        out << "ClientWorker{done=" << (done() ? "true" : "false")
            << ", results_ok=" << (resultsOk() ? "true" : "false")
            << ", sent=" << sent_commands_.size()
            << ", results=[";
        for (size_t i = 0; i < results_.size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << describeResult(results_[i]);
        }
        out << "], retries=" << retries_
            << ", stale_replies=" << stale_replies_ << "}";
        return out.str();
    }

private:
    void sendNextCommand(NodeContext& ctx) {
        if (!workload_.hasNext()) {
            return;
        }

        waiting_ = true;
        in_flight_request_id_ = next_request_id_++;
        in_flight_command_ = workload_.nextCommand();
        sent_commands_.push_back(in_flight_command_);
        sendInFlight(ctx);
    }

    void sendInFlight(NodeContext& ctx) {
        ctx.send(server_, ClientRequest{
            client_id_,
            in_flight_request_id_,
            in_flight_command_
        });
        ctx.setTimer(RetryTimer{in_flight_request_id_}, 5, 7);
    }

    Address server_;
    int client_id_;
    Workload workload_;
    bool waiting_ = false;
    bool waiting_to_send_ = false;
    int next_request_id_ = 1;
    int in_flight_request_id_ = 0;
    Command in_flight_command_ = PingCommand{""};
    int retries_ = 0;
    int stale_replies_ = 0;
    std::vector<Command> sent_commands_;
    std::vector<Result> results_;
};

struct CheckResult {
    bool ok;
    std::string message;
};

CheckResult resultsOk(const SimState& state, const Address& client) {
    const auto* worker = state.nodeAs<ClientWorker>(client);
    if (worker == nullptr) {
        return {false, client.str() + " is missing"};
    }
    if (!worker->resultsOk()) {
        return {false, client.str() + " received an unexpected result"};
    }
    return {true, ""};
}

CheckResult clientDone(const SimState& state, const Address& client) {
    const auto* worker = state.nodeAs<ClientWorker>(client);
    if (worker == nullptr) {
        return {false, client.str() + " is missing"};
    }
    if (!worker->done()) {
        return {false, client.str() + " still has work"};
    }
    return {true, ""};
}

class TestRunner {
public:
    template <typename Fn>
    void test(const std::string& name, Fn fn) {
        try {
            fn();
            ++passed_;
            std::cout << "[PASS] " << name << std::endl;
        } catch (const std::exception& e) {
            ++failed_;
            std::cout << "[FAIL] " << name << ": " << e.what() << std::endl;
        }
    }

    void check(bool condition, const std::string& message) const {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    int finish() const {
        std::cout << "Tests: " << passed_ << " passed, " << failed_
                  << " failed" << std::endl;
        return failed_ == 0 ? 0 : 1;
    }

private:
    int passed_ = 0;
    int failed_ = 0;
};

Workload twoPingWorkload(uint64_t millis_between_requests = 2) {
    return Workload(
        {PingCommand{"alpha"}, PingCommand{"beta"}},
        {PingResult{"pong(alpha)"}, PingResult{"pong(beta)"}},
        millis_between_requests);
}

Workload putAppendGetWorkload() {
    std::vector<Command> commands{
        PutCommand{"foo", "bar"},
        AppendCommand{"foo", "baz"},
        GetCommand{"foo"}
    };
    return Workload(commands, kvOracleResults(commands));
}

Workload appendAppendGetWorkload() {
    std::vector<Command> commands{
        AppendCommand{"foo", "bar"},
        AppendCommand{"foo", "bar"},
        GetCommand{"foo"}
    };
    return Workload(commands, kvOracleResults(commands));
}

Workload appendDifferentKeyWorkload(const std::string& key, int num_rounds) {
    std::vector<Command> commands;
    for (int i = 0; i < num_rounds; ++i) {
        std::string piece = std::to_string(i);
        commands.push_back(AppendCommand{key, piece});
    }
    return Workload(commands, kvOracleResults(commands));
}

Workload appendSameKeyWorkload(const std::string& client_tag, int num_rounds) {
    std::vector<Command> commands;
    for (int i = 0; i < num_rounds; ++i) {
        commands.push_back(AppendCommand{"foo", client_tag + ":" + std::to_string(i)});
    }
    return Workload(commands, {});
}

bool appendsLinearizable(const std::vector<const ClientWorker*>& clients) {
    std::vector<std::string> all_results;
    for (const auto* client : clients) {
        if (client == nullptr) {
            return false;
        }
        const auto& commands = client->sentCommands();
        const auto& results = client->results();
        for (size_t i = 0; i < results.size(); ++i) {
            if (i >= commands.size()) {
                return false;
            }
            const auto* append = std::get_if<AppendCommand>(&commands[i]);
            const auto* result = std::get_if<AppendResult>(&results[i]);
            if (append == nullptr || result == nullptr) {
                return false;
            }
            if (result->value.size() < append->value.size() ||
                result->value.substr(result->value.size() - append->value.size()) !=
                    append->value) {
                return false;
            }
            all_results.push_back(result->value);
        }
    }

    std::sort(
        all_results.begin(),
        all_results.end(),
        [](const std::string& lhs, const std::string& rhs) {
            return lhs.size() < rhs.size();
        });
    for (size_t i = 0; i + 1 < all_results.size(); ++i) {
        if (all_results[i + 1].find(all_results[i]) != 0 ||
            all_results[i + 1] == all_results[i]) {
            return false;
        }
    }
    return true;
}

SimState clientServerScenario(Workload workload,
                              std::unique_ptr<Application> app =
                                  std::make_unique<PingApplication>()) {
    SimState state;
    Address server("server1");
    Address client("client1");

    state.addNode(std::make_unique<ApplicationServer>(
        server,
        std::move(app)));

    state.addNode(std::make_unique<ClientWorker>(
        client,
        server,
        1,
        std::move(workload)));

    state.initAll();
    return state;
}

SimState kvScenario(const std::vector<std::pair<Address, Workload>>& clients) {
    SimState state;
    Address server("server1");
    state.addNode(std::make_unique<ApplicationServer>(
        server,
        std::make_unique<KVApplication>()));

    int client_id = 1;
    for (const auto& client : clients) {
        state.addNode(std::make_unique<ClientWorker>(
            client.first,
            server,
            client_id++,
            client.second));
    }

    state.initAll();
    return state;
}

int main() {
    std::cout << "BuzzDB v106: timers, workloads, client workers" << std::endl;
    TestRunner tests;

    tests.test("Workload rejects mismatched expectations", [&] {
        bool threw = false;
        try {
            Workload bad(
                {PingCommand{"alpha"}, PingCommand{"beta"}},
                {PingResult{"pong(alpha)"}});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        tests.check(threw, "mismatched workload did not throw");
    });

    tests.test("Single client finishes a rate-limited workload", [&] {
        SimState state = clientServerScenario(twoPingWorkload());
        state.run();
        CheckResult result_check = resultsOk(state, Address("client1"));
        CheckResult done_check = clientDone(state, Address("client1"));
        const auto* worker = state.nodeAs<ClientWorker>(Address("client1"));
        tests.check(result_check.ok, result_check.message);
        tests.check(done_check.ok, done_check.message);
        tests.check(worker != nullptr, "client is missing");
        tests.check(worker->sentCount() == 2, "client sent the wrong number of commands");
        tests.check(worker->resultCount() == 2, "client got the wrong number of results");
        tests.check(worker->retryCount() == 0, "reliable run should not retry");
        tests.check(state.pendingEvents() == 0, "event queue did not drain");
    });

    tests.test("KVApplication supports Put, Append, Get, and missing keys", [&] {
        KVApplication app;
        tests.check(app.execute(GetCommand{"missing"}) == Result{KeyNotFoundResult{}},
                    "missing key should return KeyNotFound");
        tests.check(app.execute(PutCommand{"foo", "bar"}) == Result{PutOkResult{}},
                    "put should return PutOk");
        tests.check(app.execute(AppendCommand{"foo", "baz"}) ==
                        Result{AppendResult{"barbaz"}},
                    "append should return the full appended value");
        tests.check(app.execute(GetCommand{"foo"}) == Result{GetResult{"barbaz"}},
                    "get should see the appended value");
    });

    tests.test("KV oracle derives expected workload results", [&] {
        Workload workload = putAppendGetWorkload();
        tests.check(workload.expectedResult(0) == Result{PutOkResult{}},
                    "oracle should expect PutOk");
        tests.check(workload.expectedResult(1) == Result{AppendResult{"barbaz"}},
                    "oracle should expect appended value");
        tests.check(workload.expectedResult(2) == Result{GetResult{"barbaz"}},
                    "oracle should expect final Get");
    });

    tests.test("Single client Put/Append/Get workload", [&] {
        SimState state = clientServerScenario(
            putAppendGetWorkload(),
            std::make_unique<KVApplication>());
        state.run();
        CheckResult result_check = resultsOk(state, Address("client1"));
        CheckResult done_check = clientDone(state, Address("client1"));
        const auto* worker = state.nodeAs<ClientWorker>(Address("client1"));
        tests.check(result_check.ok, result_check.message);
        tests.check(done_check.ok, done_check.message);
        tests.check(worker != nullptr, "client is missing");
        tests.check(worker->resultCount() == 3, "client should receive 3 results");
    });

    tests.test("Multi-client different-key appends preserve expected results", [&] {
        SimState state = kvScenario({
            {Address("client1"), appendDifferentKeyWorkload("client1-key", 5)},
            {Address("client2"), appendDifferentKeyWorkload("client2-key", 5)}
        });
        state.run();
        for (const Address& client : {Address("client1"), Address("client2")}) {
            CheckResult result_check = resultsOk(state, client);
            CheckResult done_check = clientDone(state, client);
            tests.check(result_check.ok, result_check.message);
            tests.check(done_check.ok, done_check.message);
        }
    });

    tests.test("Multi-client same-key appends are linearizable", [&] {
        SimState state = kvScenario({
            {Address("client1"), appendSameKeyWorkload("c1", 3)},
            {Address("client2"), appendSameKeyWorkload("c2", 3)}
        });
        state.run();
        const auto* c1 = state.nodeAs<ClientWorker>(Address("client1"));
        const auto* c2 = state.nodeAs<ClientWorker>(Address("client2"));
        tests.check(c1 != nullptr && c2 != nullptr, "clients are missing");
        tests.check(c1->done() && c2->done(), "clients should finish");
        tests.check(appendsLinearizable({c1, c2}),
                    "same-key append results are not linearizable");
    });

    tests.test("Retry timer resends when the server is missing", [&] {
        SimState state;
        Address client("client1");
        state.addNode(std::make_unique<ClientWorker>(
            client,
            Address("missing-server"),
            1,
            Workload(
                {PingCommand{"alpha"}},
                {PingResult{"pong(alpha)"}})));
        state.initAll();
        size_t steps = state.run(2);
        const auto* worker = state.nodeAs<ClientWorker>(client);
        tests.check(steps == 2, "expected request delivery and one retry timer");
        tests.check(worker != nullptr, "client is missing");
        tests.check(!worker->done(), "client should still be waiting");
        tests.check(worker->retryCount() == 1, "retry timer did not resend");
        tests.check(state.pendingEvents() == 2,
                    "retry should leave a resent request and retry timer pending");
    });

    tests.test("Stale replies are ignored", [&] {
        SimState state = clientServerScenario(Workload(
            {PingCommand{"alpha"}},
            {PingResult{"pong(alpha)"}}));
        state.run();
        state.send(Address("server1"), Address("client1"),
                   ClientReply{1, 99, PingResult{"pong(stale)"}});
        state.run();
        const auto* worker = state.nodeAs<ClientWorker>(Address("client1"));
        tests.check(worker != nullptr, "client is missing");
        tests.check(worker->done(), "client should remain done");
        tests.check(worker->resultCount() == 1, "stale reply changed results");
        tests.check(worker->staleReplyCount() == 1, "stale reply was not counted");
    });

    tests.test("Timer minimum must not exceed maximum", [&] {
        SimState state;
        bool threw = false;
        try {
            state.setTimer(Address("client1"), RetryTimer{1}, 7, 5);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        tests.check(threw, "invalid timer range did not throw");
    });

    SimState state = clientServerScenario(twoPingWorkload());
    state.run();

    state.printTrace();
    state.printNodes();

    CheckResult result_check = resultsOk(state, Address("client1"));
    CheckResult done_check = clientDone(state, Address("client1"));
    std::cout << "Invariant RESULTS_OK: "
              << (result_check.ok ? "passed" : result_check.message) << std::endl;
    std::cout << "Goal CLIENT_DONE: "
              << (done_check.ok ? "matched" : done_check.message) << std::endl;

    int test_status = tests.finish();
    return result_check.ok && done_check.ok && test_status == 0 ? 0 : 1;
}
