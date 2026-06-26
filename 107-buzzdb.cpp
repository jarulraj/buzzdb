#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// BuzzDB v107: run settings and controlled network faults.
//
// This is the Lab 4 runtime shape without real sockets or threads. Tests can
// add/remove nodes, add clients after the run starts, partition the network,
// disable individual links/nodes, and use seeded unreliable delivery.

struct Address {
    std::string id;

    explicit Address(std::string value = "") : id(std::move(value)) {}

    Address rootAddress() const {
        return *this;
    }

    std::string str() const {
        return id;
    }

    bool operator<(const Address& other) const {
        return id < other.id;
    }

    bool operator==(const Address& other) const {
        return id == other.id;
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

struct ClientRequest {
    int client_id;
    int request_id;
    Command command;
    int acknowledged_request_id = 0;
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
                    << ", ack=" << value.acknowledged_request_id
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
    uint64_t id;
    Address from;
    Address to;
    Message message;
};

struct TimerEnvelope {
    uint64_t id;
    Address to;
    Timer timer;
};

using EventBody = std::variant<MessageEnvelope, TimerEnvelope>;

struct ScheduledEvent {
    uint64_t deliver_at_ms;
    uint64_t order;
    EventBody body;
};

struct RunSettings {
    bool network_active = true;
    bool deliver_timers = true;
    std::optional<double> network_deliver_rate;
    std::map<std::pair<Address, Address>, bool> link_active;
    std::map<Address, bool> sender_active;
    std::map<Address, bool> receiver_active;
    std::mt19937 rng{107};

    bool linkAllowed(const Address& from, const Address& to) const {
        if (from.rootAddress() == to.rootAddress()) {
            return true;
        }

        auto link = link_active.find({from.rootAddress(), to.rootAddress()});
        if (link != link_active.end()) {
            return link->second;
        }

        auto sender = sender_active.find(from.rootAddress());
        if (sender != sender_active.end()) {
            return sender->second;
        }

        auto receiver = receiver_active.find(to.rootAddress());
        if (receiver != receiver_active.end()) {
            return receiver->second;
        }

        return network_active;
    }

    bool shouldDeliver(const MessageEnvelope& envelope) {
        if (!linkAllowed(envelope.from, envelope.to)) {
            return false;
        }
        if (!network_deliver_rate) {
            return true;
        }
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng) < *network_deliver_rate;
    }

    RunSettings& linkActive(Address from, Address to, bool active) {
        link_active[{from.rootAddress(), to.rootAddress()}] = active;
        return *this;
    }

    RunSettings& senderActive(Address from, bool active) {
        sender_active[from.rootAddress()] = active;
        return *this;
    }

    RunSettings& receiverActive(Address to, bool active) {
        receiver_active[to.rootAddress()] = active;
        return *this;
    }

    RunSettings& nodeActive(Address node, bool active) {
        senderActive(node, active);
        receiverActive(node, active);
        return *this;
    }

    RunSettings& partition(const std::vector<std::vector<Address>>& partitions) {
        network_active = false;
        link_active.clear();
        for (const auto& group : partitions) {
            for (const auto& from : group) {
                for (const auto& to : group) {
                    if (!(from.rootAddress() == to.rootAddress())) {
                        linkActive(from, to, true);
                    }
                }
            }
        }
        return *this;
    }

    RunSettings& reconnect() {
        network_active = true;
        link_active.clear();
        sender_active.clear();
        receiver_active.clear();
        return *this;
    }

    RunSettings& resetNetwork() {
        return reconnect();
    }

    RunSettings& networkDeliverRate(double rate) {
        if (rate < 0.0 || rate > 1.0) {
            throw std::runtime_error("Delivery rate must be in [0, 1].");
        }
        network_deliver_rate = rate;
        return *this;
    }
};

class FaultScript {
public:
    using Action = std::function<void(RunSettings&)>;

    FaultScript& atStep(size_t step, std::string description, Action action) {
        actions_.push_back(ScheduledAction{
            step,
            std::move(description),
            std::move(action)
        });
        return *this;
    }

    size_t applyBeforeStep(size_t step,
                           RunSettings& settings,
                           std::vector<std::string>& trace) const {
        size_t applied = 0;
        for (const auto& action : actions_) {
            if (action.step == step) {
                trace.push_back("fault@step=" + std::to_string(step) +
                                " " + action.description);
                action.action(settings);
                ++applied;
            }
        }
        return applied;
    }

private:
    struct ScheduledAction {
        size_t step;
        std::string description;
        Action action;
    };

    std::vector<ScheduledAction> actions_;
};

class SimState;

class NodeContext {
public:
    NodeContext(SimState& state, Address self)
        : state_(state), self_(std::move(self)) {}

    const Address& self() const {
        return self_;
    }

    uint64_t now() const;
    void send(const Address& to, Message message);
    void setTimer(Timer timer, uint64_t delay_ms);

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

private:
    std::vector<Command> commands_;
    std::vector<Result> expected_results_;
    uint64_t millis_between_requests_;
    size_t index_ = 0;
};

class SimState {
public:
    RunSettings& settings() {
        return settings_;
    }

    void addNode(std::unique_ptr<Node> node) {
        Address address = node->address().rootAddress();
        nodes_[address] = std::move(node);
        if (initialized_) {
            NodeContext ctx(*this, address);
            nodes_[address]->init(ctx);
        }
    }

    void removeNode(const Address& address) {
        trace_.push_back(timestamp() + " remove node " + address.str());
        nodes_.erase(address.rootAddress());
    }

    void initAll() {
        initialized_ = true;
        for (auto& entry : nodes_) {
            NodeContext ctx(*this, entry.first);
            entry.second->init(ctx);
        }
    }

    uint64_t now() const {
        return now_ms_;
    }

    void send(const Address& from, const Address& to, Message message) {
        MessageEnvelope envelope{next_message_id_++, from, to, std::move(message)};
        trace_.push_back(timestamp() + " enqueue " + describe(envelope));
        schedule(1, std::move(envelope));
    }

    void setTimer(const Address& to, Timer timer, uint64_t delay_ms) {
        TimerEnvelope envelope{next_timer_id_++, to, std::move(timer)};
        trace_.push_back(timestamp() + " set " + describe(envelope) +
                         " delay=" + std::to_string(delay_ms));
        schedule(delay_ms, std::move(envelope));
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

    size_t run(size_t max_steps = 1000) {
        size_t steps = 0;
        while (steps < max_steps && step()) {
            ++steps;
        }
        return steps;
    }

    size_t run(size_t max_steps, const FaultScript& script) {
        size_t steps = 0;
        while (steps < max_steps) {
            fault_actions_applied_ +=
                script.applyBeforeStep(steps, settings_, trace_);
            if (!step()) {
                break;
            }
            ++steps;
        }
        return steps;
    }

    size_t pendingEvents() const {
        return events_.size();
    }

    const std::vector<std::string>& trace() const {
        return trace_;
    }

    size_t droppedMessages() const {
        return dropped_messages_;
    }

    size_t faultActionsApplied() const {
        return fault_actions_applied_;
    }

    template <typename T>
    const T* nodeAs(const Address& address) const {
        auto it = nodes_.find(address.rootAddress());
        if (it == nodes_.end()) {
            return nullptr;
        }
        return dynamic_cast<const T*>(it->second.get());
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
        if (!settings_.shouldDeliver(envelope)) {
            ++dropped_messages_;
            trace_.push_back(timestamp() + " network drops " + describe(envelope));
            return;
        }

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
        if (!settings_.deliver_timers) {
            trace_.push_back(timestamp() + " timer suppressed " + describe(envelope));
            return;
        }

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
        out << "m#" << envelope.id << " " << envelope.from << " -> "
            << envelope.to << " " << describeMessage(envelope.message);
        return out.str();
    }

    static std::string describe(const TimerEnvelope& envelope) {
        std::ostringstream out;
        out << "tm#" << envelope.id << " -> " << envelope.to << " "
            << describeTimer(envelope.timer);
        return out.str();
    }

    std::map<Address, std::unique_ptr<Node>> nodes_;
    std::vector<ScheduledEvent> events_;
    std::vector<std::string> trace_;
    RunSettings settings_;
    bool initialized_ = false;
    uint64_t now_ms_ = 0;
    uint64_t next_event_order_ = 1;
    uint64_t next_message_id_ = 1;
    uint64_t next_timer_id_ = 1;
    size_t dropped_messages_ = 0;
    size_t fault_actions_applied_ = 0;
};

uint64_t NodeContext::now() const {
    return state_.now();
}

void NodeContext::send(const Address& to, Message message) {
    state_.send(self_, to, std::move(message));
}

void NodeContext::setTimer(Timer timer, uint64_t delay_ms) {
    state_.setTimer(self_, std::move(timer), delay_ms);
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

class AtMostOnceServer : public Node {
public:
    AtMostOnceServer(Address address, std::unique_ptr<Application> app)
        : Node(std::move(address)), app_(std::move(app)) {}

    void onMessage(NodeContext& ctx,
                   const Address& from,
                   const Message& message) override {
        const auto* request = std::get_if<ClientRequest>(&message);
        if (request == nullptr) {
            return;
        }

        int& high_watermark = acknowledged_request_id_[request->client_id];
        high_watermark = std::max(
            high_watermark,
            request->acknowledged_request_id);
        compactClientCache(request->client_id, high_watermark);

        RequestKey key{request->client_id, request->request_id};
        if (request->request_id <= high_watermark) {
            return;
        }

        auto cached = cache_.find(key);
        if (cached == cache_.end()) {
            execution_count_[key]++;
            Result result = app_->execute(request->command);
            cached = cache_.emplace(key, result).first;
        }

        ctx.send(from, ClientReply{
            request->client_id,
            request->request_id,
            cached->second
        });
    }

    std::unique_ptr<Node> clone() const override {
        return std::unique_ptr<Node>(new AtMostOnceServer(
            address(),
            app_->clone(),
            cache_,
            execution_count_,
            acknowledged_request_id_));
    }

    int maxExecutionCount() const {
        int max_seen = 0;
        for (const auto& entry : execution_count_) {
            max_seen = std::max(max_seen, entry.second);
        }
        return max_seen;
    }

    std::string describe() const override {
        return "AtMostOnceServer{cache=" + std::to_string(cache_.size()) +
               ", max_exec=" + std::to_string(maxExecutionCount()) +
               ", app=" + app_->describe() + "}";
    }

private:
    using RequestKey = std::pair<int, int>;

    AtMostOnceServer(Address address,
                     std::unique_ptr<Application> app,
                     std::map<RequestKey, Result> cache,
                     std::map<RequestKey, int> execution_count,
                     std::map<int, int> acknowledged_request_id)
        : Node(std::move(address)),
          app_(std::move(app)),
          cache_(std::move(cache)),
          execution_count_(std::move(execution_count)),
          acknowledged_request_id_(std::move(acknowledged_request_id)) {}

    void compactClientCache(int client_id, int high_watermark) {
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->first.first == client_id && it->first.second <= high_watermark) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::unique_ptr<Application> app_;
    std::map<RequestKey, Result> cache_;
    std::map<RequestKey, int> execution_count_;
    std::map<int, int> acknowledged_request_id_;
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
        last_completed_request_id_ = reply->request_id;
        max_wait_ms_ = std::max(max_wait_ms_, ctx.now() - wait_started_at_ms_);
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

    uint64_t maxWaitMs(uint64_t now) const {
        if (!waiting_) {
            return max_wait_ms_;
        }
        return std::max(max_wait_ms_, now - wait_started_at_ms_);
    }

    std::string describe() const override {
        std::ostringstream out;
        out << "ClientWorker{done=" << (done() ? "true" : "false")
            << ", results_ok=" << (resultsOk() ? "true" : "false")
            << ", results=[";
        for (size_t i = 0; i < results_.size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << describeResult(results_[i]);
        }
        out << "], retries=" << retries_
            << ", stale_replies=" << stale_replies_
            << ", ack=" << last_completed_request_id_
            << ", max_wait_ms=" << max_wait_ms_ << "}";
        return out.str();
    }

private:
    void sendNextCommand(NodeContext& ctx) {
        if (!workload_.hasNext()) {
            return;
        }

        waiting_ = true;
        wait_started_at_ms_ = ctx.now();
        in_flight_request_id_ = next_request_id_++;
        in_flight_command_ = workload_.nextCommand();
        sent_commands_.push_back(in_flight_command_);
        sendInFlight(ctx);
    }

    void sendInFlight(NodeContext& ctx) {
        ctx.send(server_, ClientRequest{
            client_id_,
            in_flight_request_id_,
            in_flight_command_,
            last_completed_request_id_
        });
        ctx.setTimer(RetryTimer{in_flight_request_id_}, 5);
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
    int last_completed_request_id_ = 0;
    uint64_t wait_started_at_ms_ = 0;
    uint64_t max_wait_ms_ = 0;
    std::vector<Command> sent_commands_;
    std::vector<Result> results_;
};

Workload pingWorkload(const std::string& value) {
    return Workload(
        {PingCommand{value}},
        {PingResult{"pong(" + value + ")"}});
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

Workload putAppendGetWorkload() {
    std::vector<Command> commands{
        PutCommand{"foo", "bar"},
        AppendCommand{"foo", "baz"},
        GetCommand{"foo"}
    };
    std::vector<Result> results{
        PutOkResult{},
        AppendResult{"barbaz"},
        GetResult{"barbaz"}
    };
    return Workload(commands, results);
}

Workload singleClientAppendWorkload(int num_rounds) {
    std::vector<Command> commands;
    std::vector<Result> results;
    std::string value;
    for (int i = 0; i < num_rounds; ++i) {
        std::string piece = std::to_string(i);
        value += piece;
        commands.push_back(AppendCommand{"foo", piece});
        results.push_back(AppendResult{value});
    }
    return Workload(commands, results);
}

Workload appendDifferentKeyWorkload(const std::string& key, int num_rounds) {
    std::vector<Command> commands;
    std::vector<Result> results;
    std::string value;
    for (int i = 0; i < num_rounds; ++i) {
        std::string piece = std::to_string(i);
        value += piece;
        commands.push_back(AppendCommand{key, piece});
        results.push_back(AppendResult{value});
    }
    return Workload(commands, results);
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

void addPingServer(SimState& state, const Address& server) {
    state.addNode(std::make_unique<ApplicationServer>(
        server,
        std::make_unique<PingApplication>()));
}

void addPingClient(SimState& state,
                   const Address& client,
                   const Address& server,
                   int client_id,
                   const std::string& value) {
    state.addNode(std::make_unique<ClientWorker>(
        client,
        server,
        client_id,
        pingWorkload(value)));
}

SimState unreliableKVScenario(
    const std::vector<std::pair<Address, Workload>>& clients) {
    SimState state;
    Address server("server1");
    state.settings().networkDeliverRate(0.8);
    state.addNode(std::make_unique<AtMostOnceServer>(
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

SimState linkRecoveryDemo() {
    SimState state;
    Address server("server1");
    Address spare("server2");
    Address client1("client1");
    Address client2("client2");

    state.addNode(std::make_unique<ApplicationServer>(
        server,
        std::make_unique<PingApplication>()));
    state.addNode(std::make_unique<ApplicationServer>(
        spare,
        std::make_unique<PingApplication>()));
    state.removeNode(spare);

    state.settings().linkActive(client1, server, false);
    state.addNode(std::make_unique<ClientWorker>(
        client1,
        server,
        1,
        pingWorkload("alpha")));

    state.initAll();
    state.step(); // Drop the first request while the client/server link is down.

    state.settings().reconnect();
    state.run();

    state.addNode(std::make_unique<ClientWorker>(
        client2,
        server,
        2,
        pingWorkload("beta")));
    state.run();

    return state;
}

int main() {
    std::cout << "BuzzDB v107: run settings and network faults" << std::endl;
    TestRunner tests;

    tests.test("Disabled link drops then reconnect lets retry finish", [&] {
        SimState state;
        Address server("server1");
        Address client("client1");
        addPingServer(state, server);
        state.settings().linkActive(client, server, false);
        addPingClient(state, client, server, 1, "alpha");
        state.initAll();
        state.step();
        tests.check(state.droppedMessages() == 1,
                    "first request was not dropped");
        state.settings().resetNetwork();
        state.run();
        const auto* worker = state.nodeAs<ClientWorker>(client);
        tests.check(worker != nullptr, "client is missing");
        tests.check(worker->done(), "client did not finish after reconnect");
        tests.check(worker->resultsOk(), "client result is wrong");
        tests.check(worker->retryCount() == 1, "client should retry exactly once");
    });

    tests.test("Partition prevents progress but timers keep retrying", [&] {
        SimState state;
        Address server("server1");
        Address client("client1");
        addPingServer(state, server);
        addPingClient(state, client, server, 1, "alpha");
        state.settings().partition({{client}, {server}});
        state.initAll();
        state.run(4);
        const auto* worker = state.nodeAs<ClientWorker>(client);
        tests.check(worker != nullptr, "client is missing");
        tests.check(!worker->done(), "partitioned client should not finish");
        tests.check(worker->resultCount() == 0,
                    "partitioned client should not receive a result");
        tests.check(worker->retryCount() >= 1,
                    "partitioned client should keep retrying");
        tests.check(state.pendingEvents() > 0,
                    "retry loop should leave work pending");
    });

    tests.test("FaultScript can heal a partition during a run", [&] {
        SimState state;
        Address server("server1");
        Address client("client1");
        addPingServer(state, server);
        addPingClient(state, client, server, 1, "alpha");
        state.settings().partition({{client}, {server}});
        state.initAll();

        FaultScript script;
        script.atStep(2, "reset network", [](RunSettings& settings) {
            settings.resetNetwork();
        });

        state.run(20, script);
        const auto* worker = state.nodeAs<ClientWorker>(client);
        tests.check(worker != nullptr, "client is missing");
        tests.check(worker->done(), "scripted network recovery should finish client");
        tests.check(worker->resultsOk(), "client result is wrong");
        tests.check(state.faultActionsApplied() == 1,
                    "fault script should apply exactly one action");
    });

    tests.test("Disabled timers prevent retry progress", [&] {
        SimState state;
        Address server("server1");
        Address client("client1");
        addPingServer(state, server);
        addPingClient(state, client, server, 1, "alpha");
        state.settings().linkActive(client, server, false);
        state.settings().deliver_timers = false;
        state.initAll();
        size_t steps = state.run(10);
        const auto* worker = state.nodeAs<ClientWorker>(client);
        tests.check(worker != nullptr, "client is missing");
        tests.check(steps == 2, "request and retry timer should be the only events");
        tests.check(!worker->done(), "client should not finish without retry timers");
        tests.check(worker->resultCount() == 0,
                    "client should not receive a result while disconnected");
        tests.check(worker->retryCount() == 0,
                    "disabled retry timer should not resend");
        tests.check(state.pendingEvents() == 0,
                    "timer suppression should leave no retry loop");
    });

    tests.test("nodeActive can pause and resume a server", [&] {
        SimState state;
        Address server("server1");
        Address client("client1");
        addPingServer(state, server);
        addPingClient(state, client, server, 1, "alpha");
        state.settings().nodeActive(server, false);
        state.initAll();
        state.run(2);
        const auto* before = state.nodeAs<ClientWorker>(client);
        tests.check(before != nullptr, "client is missing");
        tests.check(!before->done(), "inactive server should block completion");
        state.settings().nodeActive(server, true);
        state.run();
        const auto* after = state.nodeAs<ClientWorker>(client);
        tests.check(after != nullptr, "client is missing after resume");
        tests.check(after->done(), "client did not finish after server resumed");
        tests.check(after->resultsOk(), "client result is wrong after resume");
    });

    tests.test("Dynamic client addition after initialization", [&] {
        SimState state = linkRecoveryDemo();
        const auto* c1 = state.nodeAs<ClientWorker>(Address("client1"));
        const auto* c2 = state.nodeAs<ClientWorker>(Address("client2"));
        tests.check(c1 != nullptr && c2 != nullptr, "clients are missing");
        tests.check(c1->done() && c2->done(), "dynamic clients did not finish");
        tests.check(c1->resultsOk() && c2->resultsOk(),
                    "dynamic clients received wrong results");
        tests.check(c1->retryCount() == 1,
                    "first client should have retried after link drop");
        tests.check(c2->retryCount() == 0,
                    "second client should not need to retry");
    });

    tests.test("Removing the only server leaves the client without a result", [&] {
        SimState state;
        Address server("server1");
        Address client("client1");
        addPingServer(state, server);
        addPingClient(state, client, server, 1, "alpha");
        state.initAll();
        state.removeNode(server);
        state.run(4);
        const auto* worker = state.nodeAs<ClientWorker>(client);
        tests.check(worker != nullptr, "client is missing");
        tests.check(!worker->done(), "client should not finish with no server");
        tests.check(worker->resultCount() == 0,
                    "client should not have a result after all servers are removed");
        tests.check(worker->retryCount() >= 1,
                    "client should keep retrying while no server exists");
    });

    tests.test("Delivery rate zero drops, delivery rate one recovers", [&] {
        SimState state;
        Address server("server1");
        Address client("client1");
        addPingServer(state, server);
        addPingClient(state, client, server, 1, "alpha");
        state.settings().networkDeliverRate(0.0);
        state.initAll();
        state.run(4);
        const auto* blocked = state.nodeAs<ClientWorker>(client);
        tests.check(blocked != nullptr, "client is missing");
        tests.check(!blocked->done(), "zero delivery rate should block progress");
        tests.check(blocked->retryCount() >= 1,
                    "zero delivery rate should force retries");
        state.settings().networkDeliverRate(1.0);
        state.run();
        const auto* recovered = state.nodeAs<ClientWorker>(client);
        tests.check(recovered != nullptr, "client is missing after recovery");
        tests.check(recovered->done(), "client did not finish after recovery");
        tests.check(recovered->resultsOk(), "client result is wrong after recovery");
    });

    tests.test("Unreliable single client Put/Append/Get finishes", [&] {
        Address client("client1");
        SimState state = unreliableKVScenario({
            {client, putAppendGetWorkload()}
        });
        size_t steps = state.run(20000);
        const auto* worker = state.nodeAs<ClientWorker>(client);
        const auto* server = state.nodeAs<AtMostOnceServer>(Address("server1"));
        tests.check(steps < 20000, "unreliable run did not quiesce");
        tests.check(state.droppedMessages() > 0,
                    "unreliable run should include message loss");
        tests.check(worker != nullptr, "client is missing");
        tests.check(worker->done(), "client did not finish");
        tests.check(worker->resultsOk(), "client result is wrong");
        tests.check(server != nullptr, "server is missing");
        tests.check(server->maxExecutionCount() == 1,
                    "duplicate retries should not re-execute commands");
    });

    tests.test("Unreliable single client appends finish", [&] {
        Address client("client1");
        SimState state = unreliableKVScenario({
            {client, singleClientAppendWorkload(10)}
        });
        size_t steps = state.run(20000);
        const auto* worker = state.nodeAs<ClientWorker>(client);
        const auto* server = state.nodeAs<AtMostOnceServer>(Address("server1"));
        tests.check(steps < 20000, "unreliable append run did not quiesce");
        tests.check(state.droppedMessages() > 0,
                    "unreliable append run should include message loss");
        tests.check(worker != nullptr, "client is missing");
        tests.check(worker->done(), "client did not finish");
        tests.check(worker->resultsOk(), "append results are wrong");
        tests.check(server != nullptr, "server is missing");
        tests.check(server->maxExecutionCount() == 1,
                    "duplicate append retries should not re-execute");
    });

    tests.test("Unreliable multi-client different-key appends finish", [&] {
        std::vector<std::pair<Address, Workload>> clients;
        for (int i = 1; i <= 10; ++i) {
            std::string name = "client" + std::to_string(i);
            clients.push_back({
                Address(name),
                appendDifferentKeyWorkload(name + "-key", 5)
            });
        }

        SimState state = unreliableKVScenario(clients);
        size_t steps = state.run(50000);
        const auto* server = state.nodeAs<AtMostOnceServer>(Address("server1"));
        tests.check(steps < 50000,
                    "unreliable multi-client run did not quiesce");
        tests.check(state.droppedMessages() > 0,
                    "unreliable multi-client run should include message loss");
        for (const auto& client : clients) {
            const auto* worker = state.nodeAs<ClientWorker>(client.first);
            tests.check(worker != nullptr, client.first.str() + " is missing");
            tests.check(worker->done(), client.first.str() + " did not finish");
            tests.check(worker->resultsOk(),
                        client.first.str() + " received wrong results");
        }
        tests.check(server != nullptr, "server is missing");
        tests.check(server->maxExecutionCount() == 1,
                    "different-key retries should not re-execute commands");
    });

    tests.test("Unreliable multi-client same-key appends are linearizable", [&] {
        std::vector<std::pair<Address, Workload>> clients;
        for (int i = 1; i <= 10; ++i) {
            std::string name = "client" + std::to_string(i);
            clients.push_back({
                Address(name),
                appendSameKeyWorkload("c" + std::to_string(i), 3)
            });
        }

        SimState state = unreliableKVScenario(clients);
        size_t steps = state.run(50000);
        std::vector<const ClientWorker*> workers;
        for (const auto& client : clients) {
            const auto* worker = state.nodeAs<ClientWorker>(client.first);
            workers.push_back(worker);
            tests.check(worker != nullptr, client.first.str() + " is missing");
            tests.check(worker->done(), client.first.str() + " did not finish");
            tests.check(worker->resultsOk(),
                        client.first.str() + " received wrong results");
        }
        const auto* server = state.nodeAs<AtMostOnceServer>(Address("server1"));
        tests.check(steps < 50000,
                    "unreliable same-key run did not quiesce");
        tests.check(state.droppedMessages() > 0,
                    "unreliable same-key run should include message loss");
        tests.check(appendsLinearizable(workers),
                    "same-key append results are not linearizable");
        tests.check(server != nullptr, "server is missing");
        tests.check(server->maxExecutionCount() == 1,
                    "same-key retries should not re-execute commands");
    });

    tests.test("RunSettings uses link, sender, receiver, network priority", [&] {
        RunSettings settings;
        MessageEnvelope envelope{
            1,
            Address("client1"),
            Address("server1"),
            ClientRequest{1, 1, PingCommand{"alpha"}}
        };
        tests.check(settings.shouldDeliver(envelope),
                    "default network should deliver");
        settings.network_active = false;
        tests.check(!settings.shouldDeliver(envelope),
                    "inactive network should block delivery");
        settings.linkActive(Address("client1"), Address("server1"), true);
        tests.check(settings.shouldDeliver(envelope),
                    "explicit link should override inactive network");
        settings.senderActive(Address("client1"), false);
        tests.check(settings.shouldDeliver(envelope),
                    "explicit link should override inactive sender");
        settings.resetNetwork();
        settings.senderActive(Address("client1"), false);
        settings.receiverActive(Address("server1"), true);
        tests.check(!settings.shouldDeliver(envelope),
                    "sender should take priority over receiver");
        settings.resetNetwork();
        settings.receiverActive(Address("server1"), false);
        tests.check(!settings.shouldDeliver(envelope),
                    "inactive receiver should block delivery");
    });

    tests.test("Invalid delivery rates are rejected", [&] {
        RunSettings settings;
        bool low_threw = false;
        bool high_threw = false;
        try {
            settings.networkDeliverRate(-0.1);
        } catch (const std::runtime_error&) {
            low_threw = true;
        }
        try {
            settings.networkDeliverRate(1.1);
        } catch (const std::runtime_error&) {
            high_threw = true;
        }
        tests.check(low_threw && high_threw,
                    "delivery rates outside [0, 1] should throw");
    });

    SimState state = linkRecoveryDemo();
    state.printTrace();
    state.printNodes();

    const auto* c1 = state.nodeAs<ClientWorker>(Address("client1"));
    const auto* c2 = state.nodeAs<ClientWorker>(Address("client2"));
    bool ok = c1 != nullptr && c2 != nullptr &&
              c1->done() && c2->done() &&
              c1->resultsOk() && c2->resultsOk() &&
              c1->maxWaitMs(state.now()) <= 20 &&
              c2->maxWaitMs(state.now()) <= 20;

    std::cout << "Run invariants: " << (ok ? "passed" : "failed") << std::endl;
    int test_status = tests.finish();
    return ok && test_status == 0 ? 0 : 1;
}
