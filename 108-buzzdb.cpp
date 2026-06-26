#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// BuzzDB v108: search state, search-time controls, scenario builders,
// reusable predicates, BFS/DFS, and replay.
//
// This is the first model-checking snapshot. It is still a tiny
// ping application, but the framework can now search possible message/timer
// delivery orders, search under partitions and timer restrictions, find goal
// states, continue searching from those states, and replay event traces.

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
    virtual std::string digest() const = 0;
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

    std::string digest() const override {
        return "PingApplication(executed=" + std::to_string(num_executed_) + ")";
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

    std::string digest() const override {
        std::ostringstream out;
        out << "KVApplication(keys=" << data_.size();
        for (const auto& entry : data_) {
            out << "," << entry.first << "=" << entry.second;
        }
        out << ")";
        return out.str();
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

using Timer = std::variant<RetryTimer>;

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
    uint64_t min_delay_ms;
    uint64_t max_delay_ms;
    uint64_t set_order;
};

struct EventRef {
    enum class Kind { Message, Timer };

    Kind kind;
    uint64_t id;

    std::string key() const {
        return std::string(kind == Kind::Message ? "m#" : "tm#") +
               std::to_string(id);
    }
};

struct CheckResult {
    bool ok = true;
    std::string message;
};

constexpr int kDefaultMaxDepth = 20;
constexpr int kDefaultMaxStates = 10000;
constexpr uint32_t kDefaultSearchSeed = 108;
constexpr int kDefaultRandomDfsProbes = 100;

namespace DemoAddress {

Address server1() {
    return Address("server1");
}

Address client1() {
    return Address("client1");
}

Address client2() {
    return Address("client2");
}

}  // namespace DemoAddress

class SearchState;

using Predicate = std::function<CheckResult(const SearchState&)>;

struct NamedPredicate {
    std::string name;
    Predicate predicate;
};

struct SearchSettings {
    int max_depth = kDefaultMaxDepth;
    int max_states = kDefaultMaxStates;
    bool deliver_timers = true;
    bool network_active = true;
    uint32_t seed = kDefaultSearchSeed;
    int random_dfs_probes = kDefaultRandomDfsProbes;
    bool internal_framework_test = false;
    std::string test_title;
    std::vector<std::string> workload_names;
    std::vector<std::string> fault_names;
    std::map<std::pair<Address, Address>, bool> link_active;
    std::map<Address, bool> sender_active;
    std::map<Address, bool> receiver_active;
    std::map<Address, bool> timers_active;
    std::vector<NamedPredicate> invariants;
    std::vector<NamedPredicate> goals;
    std::vector<NamedPredicate> prunes;

    bool shouldDeliver(const MessageEnvelope& envelope) const {
        Address from = envelope.from.rootAddress();
        Address to = envelope.to.rootAddress();
        if (from == to) {
            return true;
        }

        auto link = link_active.find({from, to});
        if (link != link_active.end()) {
            return link->second;
        }

        auto sender = sender_active.find(from);
        if (sender != sender_active.end()) {
            return sender->second;
        }

        auto receiver = receiver_active.find(to);
        if (receiver != receiver_active.end()) {
            return receiver->second;
        }

        return network_active;
    }

    bool deliverTimers(const Address& address) const {
        auto timer = timers_active.find(address.rootAddress());
        if (timer != timers_active.end()) {
            return timer->second;
        }
        return deliver_timers;
    }

    SearchSettings& networkActive(bool active) {
        network_active = active;
        return *this;
    }

    SearchSettings& linkActive(Address from, Address to, bool active) {
        link_active[{from.rootAddress(), to.rootAddress()}] = active;
        return *this;
    }

    SearchSettings& senderActive(Address from, bool active) {
        sender_active[from.rootAddress()] = active;
        return *this;
    }

    SearchSettings& receiverActive(Address to, bool active) {
        receiver_active[to.rootAddress()] = active;
        return *this;
    }

    SearchSettings& nodeActive(Address node, bool active) {
        senderActive(node, active);
        receiverActive(node, active);
        return *this;
    }

    SearchSettings& deliverTimers(bool active) {
        deliver_timers = active;
        return *this;
    }

    SearchSettings& deliverTimers(Address address, bool active) {
        timers_active[address.rootAddress()] = active;
        return *this;
    }

    SearchSettings& timerActive(Address address, bool active) {
        return deliverTimers(std::move(address), active);
    }

    SearchSettings& clearDeliverTimers() {
        deliver_timers = true;
        timers_active.clear();
        return *this;
    }

    SearchSettings& partition(const std::vector<std::vector<Address>>& partitions) {
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

    SearchSettings& reconnect() {
        network_active = true;
        link_active.clear();
        sender_active.clear();
        receiver_active.clear();
        return *this;
    }

    SearchSettings& resetNetwork() {
        return reconnect();
    }
};

class NodeContext {
public:
    NodeContext(SearchState& state, Address self)
        : state_(state), self_(std::move(self)) {}

    void send(const Address& to, Message message);
    void setTimer(Timer timer, uint64_t delay_ms);
    void setTimer(Timer timer, uint64_t min_delay_ms, uint64_t max_delay_ms);

private:
    SearchState& state_;
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
    virtual std::string digest() const = 0;
    virtual std::string describe() const = 0;

private:
    Address address_;
};

class Workload {
public:
    Workload(std::vector<Command> commands, std::vector<Result> expected_results)
        : commands_(std::move(commands)),
          expected_results_(std::move(expected_results)) {
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

    std::string digest() const {
        return "Workload(index=" + std::to_string(index_) +
               ", size=" + std::to_string(commands_.size()) + ")";
    }

private:
    std::vector<Command> commands_;
    std::vector<Result> expected_results_;
    size_t index_ = 0;
};

class SearchState {
public:
    SearchState() = default;

    SearchState(const SearchState& other)
        : network_(other.network_),
          timers_(other.timers_),
          new_messages_(other.new_messages_),
          new_timers_(other.new_timers_),
          history_(other.history_),
          depth_(other.depth_),
          next_message_id_(other.next_message_id_),
          next_timer_id_(other.next_timer_id_),
          next_timer_order_(other.next_timer_order_),
          exception_(other.exception_) {
        for (const auto& entry : other.nodes_) {
            nodes_.emplace(entry.first, entry.second->clone());
        }
    }

    SearchState& operator=(const SearchState& other) {
        if (this == &other) {
            return *this;
        }
        nodes_.clear();
        for (const auto& entry : other.nodes_) {
            nodes_.emplace(entry.first, entry.second->clone());
        }
        network_ = other.network_;
        timers_ = other.timers_;
        new_messages_ = other.new_messages_;
        new_timers_ = other.new_timers_;
        history_ = other.history_;
        depth_ = other.depth_;
        next_message_id_ = other.next_message_id_;
        next_timer_id_ = other.next_timer_id_;
        next_timer_order_ = other.next_timer_order_;
        exception_ = other.exception_;
        return *this;
    }

    void addNode(std::unique_ptr<Node> node) {
        Address address = node->address().rootAddress();
        nodes_[address] = std::move(node);
        timers_[address];
        NodeContext ctx(*this, address);
        nodes_[address]->init(ctx);
    }

    void send(const Address& from, const Address& to, Message message) {
        MessageEnvelope envelope{
            next_message_id_++,
            from,
            to,
            std::move(message)
        };
        network_.push_back(envelope);
        new_messages_.push_back(std::move(envelope));
    }

    void setTimer(const Address& to,
                  Timer timer,
                  uint64_t min_delay_ms,
                  uint64_t max_delay_ms) {
        TimerEnvelope envelope{
            next_timer_id_++,
            to,
            std::move(timer),
            min_delay_ms,
            max_delay_ms,
            next_timer_order_++
        };
        timers_[to.rootAddress()].push_back(envelope);
        new_timers_.push_back(std::move(envelope));
    }

    std::vector<EventRef> events() const {
        SearchSettings settings;
        return events(settings);
    }

    std::vector<EventRef> events(const SearchSettings& settings) const {
        std::vector<EventRef> out;
        for (const auto& message : network_) {
            if (nodes_.find(message.to.rootAddress()) != nodes_.end() &&
                settings.shouldDeliver(message)) {
                out.push_back({EventRef::Kind::Message, message.id});
            }
        }

        for (const auto& entry : timers_) {
            if (!settings.deliverTimers(entry.first)) {
                continue;
            }
            uint64_t min_seen_max = 0;
            bool have_min = false;
            for (const auto& timer : entry.second) {
                if (have_min && timer.min_delay_ms >= min_seen_max) {
                    continue;
                }
                out.push_back({EventRef::Kind::Timer, timer.id});
                if (!have_min || timer.max_delay_ms < min_seen_max) {
                    min_seen_max = timer.max_delay_ms;
                    have_min = true;
                }
            }
        }
        return out;
    }

    std::optional<SearchState> stepEvent(const EventRef& event,
                                         const SearchSettings& settings) const {
        auto available = events(settings);
        auto match = std::find_if(
            available.begin(),
            available.end(),
            [&](const EventRef& candidate) {
                return candidate.kind == event.kind && candidate.id == event.id;
            });
        if (match == available.end()) {
            return std::nullopt;
        }
        return stepEvent(event);
    }

    std::vector<EventRef> messageEventsMatching(
        const SearchSettings& settings,
        const std::function<bool(const MessageEnvelope&)>& predicate) const {
        std::vector<EventRef> out;
        for (const auto& message : network_) {
            if (nodes_.find(message.to.rootAddress()) != nodes_.end() &&
                settings.shouldDeliver(message) &&
                predicate(message)) {
                out.push_back({EventRef::Kind::Message, message.id});
            }
        }
        return out;
    }

    std::optional<SearchState> stepMessageMatching(
        const SearchSettings& settings,
        const std::function<bool(const MessageEnvelope&)>& predicate) const {
        std::vector<EventRef> matches = messageEventsMatching(settings, predicate);
        if (matches.empty()) {
            return std::nullopt;
        }
        return stepEvent(matches.front(), settings);
    }

    std::optional<SearchState> stepEvent(const EventRef& event) const {
        SearchState next(*this);
        next.depth_++;
        next.history_.push_back(event.key());
        next.new_messages_.clear();
        next.new_timers_.clear();

        try {
            if (event.kind == EventRef::Kind::Message) {
                auto message = std::find_if(
                    next.network_.begin(),
                    next.network_.end(),
                    [&](const MessageEnvelope& envelope) {
                        return envelope.id == event.id;
                    });
                if (message == next.network_.end()) {
                    return std::nullopt;
                }

                auto node = next.nodes_.find(message->to.rootAddress());
                if (node == next.nodes_.end()) {
                    return std::nullopt;
                }

                NodeContext ctx(next, message->to.rootAddress());
                node->second->onMessage(ctx, message->from, message->message);
                return next;
            }

            for (auto& entry : next.timers_) {
                auto timer = std::find_if(
                    entry.second.begin(),
                    entry.second.end(),
                    [&](const TimerEnvelope& envelope) {
                        return envelope.id == event.id;
                    });
                if (timer == entry.second.end()) {
                    continue;
                }

                TimerEnvelope timer_copy = *timer;
                entry.second.erase(timer);
                auto node = next.nodes_.find(timer_copy.to.rootAddress());
                if (node == next.nodes_.end()) {
                    return std::nullopt;
                }

                NodeContext ctx(next, timer_copy.to.rootAddress());
                node->second->onTimer(ctx, timer_copy.timer);
                return next;
            }
        } catch (const std::exception& e) {
            next.exception_ = e.what();
            return next;
        }

        return std::nullopt;
    }

    template <typename T>
    const T* nodeAs(const Address& address) const {
        auto it = nodes_.find(address.rootAddress());
        if (it == nodes_.end()) {
            return nullptr;
        }
        return dynamic_cast<const T*>(it->second.get());
    }

    bool hasNode(const Address& address) const {
        return nodes_.find(address.rootAddress()) != nodes_.end();
    }

    const std::vector<MessageEnvelope>& network() const {
        return network_;
    }

    const std::vector<MessageEnvelope>& newMessages() const {
        return new_messages_;
    }

    const std::map<Address, std::vector<TimerEnvelope>>& timers() const {
        return timers_;
    }

    const std::vector<TimerEnvelope>& newTimers() const {
        return new_timers_;
    }

    int depth() const {
        return depth_;
    }

    const std::vector<std::string>& history() const {
        return history_;
    }

    const std::string& exception() const {
        return exception_;
    }

    std::string digest() const {
        std::ostringstream out;
        out << "depth=" << depth_;
        for (const auto& entry : nodes_) {
            out << ";node(" << entry.first.str() << ")=" << entry.second->digest();
        }
        out << ";network=";
        for (const auto& message : network_) {
            out << "{" << message.id << ":" << message.from.str()
                << ">" << message.to.str() << ":" << describeMessage(message.message)
                << "}";
        }
        out << ";timers=";
        for (const auto& entry : timers_) {
            for (const auto& timer : entry.second) {
                out << "{" << timer.id << ":" << timer.to.str()
                    << ":" << describeTimer(timer.timer) << "}";
            }
        }
        return out.str();
    }

    void printSummary() const {
        std::cout << "State depth=" << depth_ << std::endl;
        for (const auto& entry : nodes_) {
            std::cout << "  " << entry.first << " -> "
                      << entry.second->describe() << std::endl;
        }
        std::cout << "  network:";
        for (const auto& message : network_) {
            std::cout << " m#" << message.id;
        }
        std::cout << std::endl;
        std::cout << "  timers:";
        for (const auto& entry : timers_) {
            for (const auto& timer : entry.second) {
                std::cout << " tm#" << timer.id;
            }
        }
        std::cout << std::endl;
    }

private:
    std::map<Address, std::unique_ptr<Node>> nodes_;
    std::vector<MessageEnvelope> network_;
    std::map<Address, std::vector<TimerEnvelope>> timers_;
    std::vector<MessageEnvelope> new_messages_;
    std::vector<TimerEnvelope> new_timers_;
    std::vector<std::string> history_;
    int depth_ = 0;
    uint64_t next_message_id_ = 1;
    uint64_t next_timer_id_ = 1;
    uint64_t next_timer_order_ = 1;
    std::string exception_;
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

        if (request->request_id <= high_watermark) {
            return;
        }

        RequestKey key{request->client_id, request->request_id};
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
        return std::unique_ptr<Node>(
            new AtMostOnceServer(
                address(),
                app_->clone(),
                cache_,
                execution_count_,
                acknowledged_request_id_));
    }

    size_t cacheSize() const {
        return cache_.size();
    }

    bool hasCachedResult(int client_id, int request_id) const {
        return cache_.find(RequestKey{client_id, request_id}) != cache_.end();
    }

    int acknowledgedRequestId(int client_id) const {
        auto it = acknowledged_request_id_.find(client_id);
        if (it == acknowledged_request_id_.end()) {
            return 0;
        }
        return it->second;
    }

    int maxExecutionCount() const {
        int max_seen = 0;
        for (const auto& entry : execution_count_) {
            max_seen = std::max(max_seen, entry.second);
        }
        return max_seen;
    }

    std::string digest() const override {
        std::ostringstream out;
        out << "Server(cache=" << cache_.size()
            << ", max_exec=" << maxExecutionCount()
            << ", app=" << app_->digest() << ")";
        return out.str();
    }

    std::string describe() const override {
        return digest();
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
        sendNext(ctx);
    }

    void onMessage(NodeContext& ctx,
                   const Address& from,
                   const Message& message) override {
        (void) ctx;
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
        waiting_ = false;
        in_flight_request_id_ = 0;
        sendNext(ctx);
    }

    void onTimer(NodeContext& ctx, const Timer& timer) override {
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
        return !waiting_ && !workload_.hasNext();
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

    std::string digest() const override {
        std::ostringstream out;
        out << "Client(waiting=" << waiting_
            << ", req=" << in_flight_request_id_
            << ", results=" << results_.size()
            << ", stale=" << stale_replies_
            << ", retries=" << retries_
            << ", ack=" << last_completed_request_id_
            << ", workload=" << workload_.digest() << ")";
        return out.str();
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
            << ", stale=" << stale_replies_
            << ", ack=" << last_completed_request_id_ << "}";
        return out.str();
    }

private:
    void sendNext(NodeContext& ctx) {
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
            in_flight_command_,
            last_completed_request_id_
        });
        ctx.setTimer(RetryTimer{in_flight_request_id_}, 5, 7);
    }

    Address server_;
    int client_id_;
    Workload workload_;
    bool waiting_ = false;
    int next_request_id_ = 1;
    int in_flight_request_id_ = 0;
    Command in_flight_command_ = PingCommand{""};
    int retries_ = 0;
    int stale_replies_ = 0;
    int last_completed_request_id_ = 0;
    std::vector<Command> sent_commands_;
    std::vector<Result> results_;
};

struct SearchResults {
    enum class EndCondition {
        SpaceExhausted,
        StateLimit,
        InvariantViolated,
        GoalFound,
        ExceptionThrown
    };

    EndCondition end_condition = EndCondition::SpaceExhausted;
    std::string message;
    std::optional<SearchState> terminal_state;
    int explored = 0;
    std::string test_title;
    std::vector<std::string> workload_names;
    std::vector<std::string> fault_names;
    bool internal_framework_test = false;
};

void attachSearchMetadata(SearchResults& results, const SearchSettings& settings) {
    results.test_title = settings.test_title;
    results.workload_names = settings.workload_names;
    results.fault_names = settings.fault_names;
    results.internal_framework_test = settings.internal_framework_test;
}

std::string endConditionName(SearchResults::EndCondition condition) {
    switch (condition) {
        case SearchResults::EndCondition::SpaceExhausted:
            return "space exhausted";
        case SearchResults::EndCondition::StateLimit:
            return "state limit";
        case SearchResults::EndCondition::InvariantViolated:
            return "invariant violated";
        case SearchResults::EndCondition::GoalFound:
            return "goal found";
        case SearchResults::EndCondition::ExceptionThrown:
            return "exception thrown";
    }
    return "unknown";
}

std::optional<SearchResults> checkState(const SearchState& state,
                                        const SearchSettings& settings) {
    if (!state.exception().empty()) {
        SearchResults results;
        results.end_condition = SearchResults::EndCondition::ExceptionThrown;
        results.message = state.exception();
        results.terminal_state = state;
        attachSearchMetadata(results, settings);
        return results;
    }

    for (const auto& invariant : settings.invariants) {
        CheckResult result = invariant.predicate(state);
        if (!result.ok) {
            SearchResults results;
            results.end_condition =
                SearchResults::EndCondition::InvariantViolated;
            results.message = invariant.name + ": " + result.message;
            results.terminal_state = state;
            attachSearchMetadata(results, settings);
            return results;
        }
    }

    for (const auto& goal : settings.goals) {
        CheckResult result = goal.predicate(state);
        if (result.ok) {
            SearchResults results;
            results.end_condition = SearchResults::EndCondition::GoalFound;
            results.message = goal.name;
            results.terminal_state = state;
            attachSearchMetadata(results, settings);
            return results;
        }
    }

    return std::nullopt;
}

bool shouldPrune(const SearchState& state, const SearchSettings& settings) {
    if (state.depth() >= settings.max_depth) {
        return true;
    }
    for (const auto& prune : settings.prunes) {
        if (prune.predicate(state).ok) {
            return true;
        }
    }
    return false;
}

SearchResults bfs(const SearchState& initial, const SearchSettings& settings) {
    std::deque<SearchState> queue;
    std::set<std::string> seen;
    queue.push_back(initial);
    seen.insert(initial.digest());

    SearchResults results;
    attachSearchMetadata(results, settings);
    while (!queue.empty()) {
        SearchState current = queue.front();
        queue.pop_front();
        results.explored++;

        if (auto terminal = checkState(current, settings)) {
            terminal->explored = results.explored;
            return *terminal;
        }

        if (shouldPrune(current, settings)) {
            continue;
        }

        if (results.explored >= settings.max_states) {
            results.end_condition = SearchResults::EndCondition::StateLimit;
            results.message = "state limit reached";
            results.terminal_state = current;
            return results;
        }

        for (const auto& event : current.events(settings)) {
            auto next = current.stepEvent(event, settings);
            if (!next) {
                continue;
            }
            std::string key = next->digest();
            if (seen.insert(key).second) {
                queue.push_back(*next);
            }
        }
    }

    results.end_condition = SearchResults::EndCondition::SpaceExhausted;
    results.message = "all reachable states searched";
    return results;
}

SearchResults randomDfs(const SearchState& initial,
                        SearchSettings settings) {
    std::mt19937 rng(settings.seed);
    SearchResults aggregate;
    attachSearchMetadata(aggregate, settings);
    for (int probe = 0; probe < settings.random_dfs_probes; ++probe) {
        SearchState current = initial;
        for (;;) {
            aggregate.explored++;
            if (auto terminal = checkState(current, settings)) {
                terminal->explored = aggregate.explored;
                return *terminal;
            }
            if (shouldPrune(current, settings)) {
                break;
            }
            auto events = current.events(settings);
            if (events.empty()) {
                break;
            }
            std::shuffle(events.begin(), events.end(), rng);
            auto next = current.stepEvent(events.front(), settings);
            if (!next) {
                break;
            }
            current = *next;
            if (aggregate.explored >= settings.max_states) {
                aggregate.end_condition = SearchResults::EndCondition::StateLimit;
                aggregate.message = "state limit reached";
                aggregate.terminal_state = current;
                return aggregate;
            }
        }
    }
    aggregate.end_condition = SearchResults::EndCondition::SpaceExhausted;
    aggregate.message = "random DFS probes completed";
    return aggregate;
}

SearchResults replayTrace(SearchState state,
                          const SearchSettings& settings,
                          const std::vector<std::string>& event_keys) {
    SearchResults results;
    attachSearchMetadata(results, settings);
    for (const auto& key : event_keys) {
        auto events = state.events(settings);
        auto event = std::find_if(
            events.begin(),
            events.end(),
            [&](const EventRef& candidate) {
                return candidate.key() == key;
            });
        if (event == events.end()) {
            results.end_condition = SearchResults::EndCondition::ExceptionThrown;
            results.message = "could not replay event " + key;
            results.terminal_state = state;
            return results;
        }
        auto next = state.stepEvent(*event, settings);
        if (!next) {
            results.end_condition = SearchResults::EndCondition::ExceptionThrown;
            results.message = "event became undeliverable " + key;
            results.terminal_state = state;
            return results;
        }
        state = *next;
        results.explored++;
        if (auto terminal = checkState(state, settings)) {
            terminal->explored = results.explored;
            return *terminal;
        }
    }
    results.end_condition = SearchResults::EndCondition::SpaceExhausted;
    results.message = "trace replay completed";
    results.terminal_state = state;
    return results;
}

CheckResult resultsOk(const SearchState& state, const Address& client) {
    const auto* worker = state.nodeAs<ClientWorker>(client);
    if (worker == nullptr) {
        return {false, client.str() + " is missing"};
    }
    if (!worker->resultsOk()) {
        return {false, client.str() + " received an unexpected result"};
    }
    return {true, ""};
}

CheckResult resultsOk(const SearchState& state,
                      const std::vector<Address>& clients) {
    for (const auto& client : clients) {
        CheckResult result = resultsOk(state, client);
        if (!result.ok) {
            return result;
        }
    }
    return {true, ""};
}

CheckResult clientDone(const SearchState& state, const Address& client) {
    const auto* worker = state.nodeAs<ClientWorker>(client);
    if (worker == nullptr) {
        return {false, client.str() + " is missing"};
    }
    return {worker->done(), client.str() + " is not done"};
}

CheckResult clientsDone(const SearchState& state,
                        const std::vector<Address>& clients) {
    for (const auto& client : clients) {
        CheckResult result = clientDone(state, client);
        if (!result.ok) {
            return result;
        }
    }
    return {true, ""};
}

CheckResult clientHasExactlyResults(const SearchState& state,
                                    const Address& client,
                                    size_t count) {
    const auto* worker = state.nodeAs<ClientWorker>(client);
    if (worker == nullptr) {
        return {false, client.str() + " is missing"};
    }
    return {worker->resultCount() == count,
            client.str() + " has " + std::to_string(worker->resultCount()) +
                " results, expected exactly " + std::to_string(count)};
}

CheckResult clientHasAtLeastResults(const SearchState& state,
                                    const Address& client,
                                    size_t count) {
    const auto* worker = state.nodeAs<ClientWorker>(client);
    if (worker == nullptr) {
        return {false, client.str() + " is missing"};
    }
    return {worker->resultCount() >= count,
            client.str() + " has fewer results than requested"};
}

CheckResult clientHasResults(const SearchState& state,
                             const Address& client,
                             size_t count) {
    return clientHasExactlyResults(state, client, count);
}

CheckResult clientHasResult(const SearchState& state,
                            const Address& client,
                            size_t count = 1) {
    return clientHasAtLeastResults(state, client, count);
}

CheckResult serverAtMostOnce(const SearchState& state, const Address& server) {
    const auto* node = state.nodeAs<AtMostOnceServer>(server);
    if (node == nullptr) {
        return {false, server.str() + " is missing"};
    }
    if (node->maxExecutionCount() > 1) {
        return {false, "server executed a request more than once"};
    }
    return {true, ""};
}

CheckResult noInvariantViolation(const SearchState& state,
                                 const std::vector<NamedPredicate>& invariants) {
    for (const auto& invariant : invariants) {
        CheckResult result = invariant.predicate(state);
        if (!result.ok) {
            return {false, invariant.name + ": " + result.message};
        }
    }
    return {true, ""};
}

CheckResult noneDecided(const SearchState& state,
                        const std::vector<Address>& clients) {
    for (const auto& client : clients) {
        const auto* worker = state.nodeAs<ClientWorker>(client);
        if (worker == nullptr) {
            return {false, client.str() + " is missing"};
        }
        if (worker->resultCount() != 0) {
            return {false, client.str() + " received a result"};
        }
    }
    return {true, ""};
}

CheckResult allResultsSame(const SearchState& state,
                           const std::vector<Address>& clients) {
    std::optional<std::vector<Result>> first;
    for (const auto& client : clients) {
        const auto* worker = state.nodeAs<ClientWorker>(client);
        if (worker == nullptr) {
            return {false, client.str() + " is missing"};
        }
        if (!first) {
            first = worker->results();
        } else if (!(worker->results() == *first)) {
            return {false, client.str() + " results differ"};
        }
    }
    return {true, ""};
}

CheckResult containsEnvelopeMatching(
    const SearchState& state,
    const std::function<bool(const MessageEnvelope&)>& predicate,
    const std::string& description = "message envelope") {
    for (const auto& envelope : state.network()) {
        if (predicate(envelope)) {
            return {true, ""};
        }
    }
    return {false, "network does not contain " + description};
}

CheckResult containsMessageMatching(
    const SearchState& state,
    const std::function<bool(const Message&)>& predicate,
    const std::string& description = "message") {
    return containsEnvelopeMatching(
        state,
        [&](const MessageEnvelope& envelope) {
            return predicate(envelope.message);
        },
        description);
}

CheckResult appendsLinearizable(const SearchState& state,
                                const std::vector<Address>& clients) {
    std::vector<std::string> all_results;
    for (const auto& client : clients) {
        const auto* worker = state.nodeAs<ClientWorker>(client);
        if (worker == nullptr) {
            return {false, client.str() + " is missing"};
        }
        const auto& commands = worker->sentCommands();
        const auto& results = worker->results();
        for (size_t i = 0; i < results.size(); ++i) {
            if (i >= commands.size()) {
                return {false, client.str() + " has a result without a command"};
            }
            const auto* append = std::get_if<AppendCommand>(&commands[i]);
            const auto* result = std::get_if<AppendResult>(&results[i]);
            if (append == nullptr || result == nullptr) {
                return {false, client.str() + " has a non-append history"};
            }
            if (result->value.size() < append->value.size() ||
                result->value.substr(result->value.size() - append->value.size()) !=
                    append->value) {
                return {false, client.str() + " got " + result->value +
                                    " for append " + append->value};
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
            return {false, all_results[i] + " is inconsistent with " +
                                all_results[i + 1]};
        }
    }
    return {true, ""};
}

CheckResult messageInFlight(const SearchState& state,
                            std::optional<Address> from = std::nullopt,
                            std::optional<Address> to = std::nullopt) {
    for (const auto& message : state.network()) {
        if (from && !(message.from.rootAddress() == from->rootAddress())) {
            continue;
        }
        if (to && !(message.to.rootAddress() == to->rootAddress())) {
            continue;
        }
        return {true, ""};
    }

    std::ostringstream out;
    out << "no message in flight";
    if (from) {
        out << " from " << from->str();
    }
    if (to) {
        out << " to " << to->str();
    }
    return {false, out.str()};
}

CheckResult timerPending(const SearchState& state,
                         std::optional<Address> to = std::nullopt) {
    for (const auto& entry : state.timers()) {
        if (to && !(entry.first.rootAddress() == to->rootAddress())) {
            continue;
        }
        if (!entry.second.empty()) {
            return {true, ""};
        }
    }

    std::ostringstream out;
    out << "no timer pending";
    if (to) {
        out << " for " << to->str();
    }
    return {false, out.str()};
}

namespace Predicates {

NamedPredicate resultsOk(std::vector<Address> clients) {
    return {
        "RESULTS_OK",
        [clients = std::move(clients)](const SearchState& state) {
            return ::resultsOk(state, clients);
        }
    };
}

NamedPredicate clientsDone(std::vector<Address> clients) {
    return {
        "CLIENTS_DONE",
        [clients = std::move(clients)](const SearchState& state) {
            return ::clientsDone(state, clients);
        }
    };
}

NamedPredicate clientHasResult(Address client, size_t count = 1) {
    return {
        "CLIENT_HAS_RESULT",
        [client = std::move(client), count](const SearchState& state) {
            return ::clientHasResult(state, client, count);
        }
    };
}

NamedPredicate clientHasExactlyResults(Address client, size_t count) {
    return {
        "CLIENT_HAS_EXACTLY_RESULTS",
        [client = std::move(client), count](const SearchState& state) {
            return ::clientHasExactlyResults(state, client, count);
        }
    };
}

NamedPredicate noneDecided(std::vector<Address> clients) {
    return {
        "NONE_DECIDED",
        [clients = std::move(clients)](const SearchState& state) {
            return ::noneDecided(state, clients);
        }
    };
}

NamedPredicate allResultsSame(std::vector<Address> clients) {
    return {
        "ALL_RESULTS_SAME",
        [clients = std::move(clients)](const SearchState& state) {
            return ::allResultsSame(state, clients);
        }
    };
}

NamedPredicate containsEnvelopeMatching(
    std::string name,
    std::function<bool(const MessageEnvelope&)> predicate) {
    return {
        "CONTAINS_ENVELOPE_MATCHING",
        [name = std::move(name),
         predicate = std::move(predicate)](const SearchState& state) {
            return ::containsEnvelopeMatching(state, predicate, name);
        }
    };
}

NamedPredicate containsMessageMatching(
    std::string name,
    std::function<bool(const Message&)> predicate) {
    return {
        "CONTAINS_MESSAGE_MATCHING",
        [name = std::move(name),
         predicate = std::move(predicate)](const SearchState& state) {
            return ::containsMessageMatching(state, predicate, name);
        }
    };
}

NamedPredicate appendsLinearizable(std::vector<Address> clients) {
    return {
        "APPENDS_LINEARIZABLE",
        [clients = std::move(clients)](const SearchState& state) {
            return ::appendsLinearizable(state, clients);
        }
    };
}

NamedPredicate noInvariantViolation(std::vector<NamedPredicate> invariants) {
    return {
        "NO_INVARIANT_VIOLATION",
        [invariants = std::move(invariants)](const SearchState& state) {
            return ::noInvariantViolation(state, invariants);
        }
    };
}

NamedPredicate messageInFlight(std::optional<Address> from = std::nullopt,
                               std::optional<Address> to = std::nullopt) {
    return {
        "MESSAGE_IN_FLIGHT",
        [from = std::move(from), to = std::move(to)](const SearchState& state) {
            return ::messageInFlight(state, from, to);
        }
    };
}

NamedPredicate timerPending(std::optional<Address> to = std::nullopt) {
    return {
        "TIMER_PENDING",
        [to = std::move(to)](const SearchState& state) {
            return ::timerPending(state, to);
        }
    };
}

NamedPredicate atMostOnce(Address server) {
    return {
        "AT_MOST_ONCE",
        [server = std::move(server)](const SearchState& state) {
            return serverAtMostOnce(state, server);
        }
    };
}

}  // namespace Predicates

struct Scenario {
    SearchState state;
    SearchSettings settings;
};

class FaultWorkload {
public:
    virtual ~FaultWorkload() = default;
    virtual std::string description() const = 0;
    virtual void apply(SearchSettings& settings) const = 0;
};

class PartitionFaultWorkload final : public FaultWorkload {
public:
    explicit PartitionFaultWorkload(std::vector<std::vector<Address>> partitions)
        : partitions_(std::move(partitions)) {}

    std::string description() const override {
        return "Partition";
    }

    void apply(SearchSettings& settings) const override {
        settings.partition(partitions_);
    }

private:
    std::vector<std::vector<Address>> partitions_;
};

class NodeUnavailableFault final : public FaultWorkload {
public:
    explicit NodeUnavailableFault(std::vector<Address> nodes,
                                  bool pause_timers = true)
        : nodes_(std::move(nodes)), pause_timers_(pause_timers) {}

    std::string description() const override {
        return "NodeUnavailable";
    }

    void apply(SearchSettings& settings) const override {
        for (const auto& node : nodes_) {
            settings.nodeActive(node, false);
            if (pause_timers_) {
                settings.timerActive(node, false);
            }
        }
    }

private:
    std::vector<Address> nodes_;
    bool pause_timers_;
};

class TimerPauseFaultWorkload final : public FaultWorkload {
public:
    explicit TimerPauseFaultWorkload(std::vector<Address> nodes)
        : nodes_(std::move(nodes)) {}

    std::string description() const override {
        return "TimerPause";
    }

    void apply(SearchSettings& settings) const override {
        for (const auto& node : nodes_) {
            settings.timerActive(node, false);
        }
    }

private:
    std::vector<Address> nodes_;
};

class RandomDisabledLinksFault final : public FaultWorkload {
public:
    RandomDisabledLinksFault(std::vector<Address> nodes,
                             int disabled_pairs,
                             uint32_t seed)
        : nodes_(std::move(nodes)),
          disabled_pairs_(disabled_pairs),
          seed_(seed) {}

    std::string description() const override {
        return "RandomDisabledLinks";
    }

    void apply(SearchSettings& settings) const override {
        if (nodes_.size() < 2 || disabled_pairs_ <= 0) {
            return;
        }

        std::mt19937 rng(seed_);
        std::uniform_int_distribution<size_t> pick(0, nodes_.size() - 1);
        for (int i = 0; i < disabled_pairs_; ++i) {
            Address from = nodes_[pick(rng)];
            Address to = nodes_[pick(rng)];
            if (from == to) {
                to = nodes_[(pick(rng) + 1) % nodes_.size()];
            }
            settings.linkActive(from, to, false);
        }
    }

private:
    std::vector<Address> nodes_;
    int disabled_pairs_;
    uint32_t seed_;
};

class ScenarioBuilder;

class SearchTestWorkload {
public:
    virtual ~SearchTestWorkload() = default;
    virtual std::string description() const = 0;
    virtual void setup(ScenarioBuilder& builder) const {
        (void) builder;
    }
    virtual void configure(SearchSettings& settings) const = 0;
    virtual CheckResult check(const SearchState& state) const = 0;
};

class ClientCompletionWorkload final : public SearchTestWorkload {
public:
    explicit ClientCompletionWorkload(std::vector<Address> clients)
        : clients_(std::move(clients)) {}

    std::string description() const override {
        return "ClientCompletion";
    }

    void configure(SearchSettings& settings) const override {
        settings.invariants.push_back(Predicates::resultsOk(clients_));
        settings.goals.push_back(Predicates::clientsDone(clients_));
    }

    CheckResult check(const SearchState& state) const override {
        return clientsDone(state, clients_);
    }

private:
    std::vector<Address> clients_;
};

class CompoundSearchWorkload {
public:
    CompoundSearchWorkload& addWorkload(std::shared_ptr<const SearchTestWorkload> workload) {
        if (!workload) {
            throw std::runtime_error("CompoundSearchWorkload cannot add a null workload.");
        }
        workloads_.push_back(std::move(workload));
        return *this;
    }

    CompoundSearchWorkload& addFailure(std::shared_ptr<const FaultWorkload> failure) {
        if (!failure) {
            throw std::runtime_error("CompoundSearchWorkload cannot add a null failure.");
        }
        failures_.push_back(std::move(failure));
        return *this;
    }

    std::string description() const {
        std::ostringstream out;
        bool first = true;
        for (const auto& workload : workloads_) {
            if (!first) {
                out << ";";
            }
            out << workload->description();
            first = false;
        }
        for (const auto& failure : failures_) {
            if (!first) {
                out << ";";
            }
            out << failure->description();
            first = false;
        }
        return out.str();
    }

    void setup(ScenarioBuilder& builder) const {
        for (const auto& workload : workloads_) {
            workload->setup(builder);
        }
    }

    void configure(SearchSettings& settings) const {
        for (const auto& failure : failures_) {
            settings.fault_names.push_back(failure->description());
            failure->apply(settings);
        }
        for (const auto& workload : workloads_) {
            settings.workload_names.push_back(workload->description());
            workload->configure(settings);
        }
    }

    CheckResult check(const SearchState& state) const {
        for (const auto& workload : workloads_) {
            CheckResult result = workload->check(state);
            if (!result.ok) {
                return result;
            }
        }
        return {true, ""};
    }

private:
    std::vector<std::shared_ptr<const SearchTestWorkload>> workloads_;
    std::vector<std::shared_ptr<const FaultWorkload>> failures_;
};

class ScenarioBuilder {
public:
    ScenarioBuilder& addNode(std::unique_ptr<Node> node) {
        if (!node) {
            throw std::runtime_error("ScenarioBuilder cannot add a null node.");
        }
        std::shared_ptr<const Node> prototype(std::move(node));
        node_factories_.push_back([prototype]() {
            return prototype->clone();
        });
        return *this;
    }

    ScenarioBuilder& addServer(Address address, std::unique_ptr<Application> app) {
        return addNode(std::make_unique<AtMostOnceServer>(
            std::move(address),
            std::move(app)));
    }

    ScenarioBuilder& addServer(
        Address address,
        std::function<std::unique_ptr<Application>()> app_factory) {
        node_factories_.push_back(
            [address = std::move(address),
             app_factory = std::move(app_factory)]() {
                return std::make_unique<AtMostOnceServer>(
                    address,
                    app_factory());
            });
        return *this;
    }

    ScenarioBuilder& addClient(Address address,
                               Address server,
                               int client_id,
                               Workload workload) {
        node_factories_.push_back(
            [address = std::move(address),
             server = std::move(server),
             client_id,
             workload = std::move(workload)]() {
                return std::make_unique<ClientWorker>(
                    address,
                    server,
                    client_id,
                    workload);
            });
        return *this;
    }

    ScenarioBuilder& partition(
        const std::vector<std::vector<Address>>& partitions) {
        settings_.partition(partitions);
        return *this;
    }

    ScenarioBuilder& resetNetwork() {
        settings_.resetNetwork();
        return *this;
    }

    ScenarioBuilder& nodeActive(Address address, bool active) {
        settings_.nodeActive(std::move(address), active);
        return *this;
    }

    ScenarioBuilder& linkActive(Address from, Address to, bool active) {
        settings_.linkActive(std::move(from), std::move(to), active);
        return *this;
    }

    ScenarioBuilder& senderActive(Address from, bool active) {
        settings_.senderActive(std::move(from), active);
        return *this;
    }

    ScenarioBuilder& receiverActive(Address to, bool active) {
        settings_.receiverActive(std::move(to), active);
        return *this;
    }

    ScenarioBuilder& timerActive(Address address, bool active) {
        settings_.timerActive(std::move(address), active);
        return *this;
    }

    ScenarioBuilder& addFault(std::shared_ptr<const FaultWorkload> fault) {
        if (!fault) {
            throw std::runtime_error("ScenarioBuilder cannot add a null fault.");
        }
        faults_.push_back(std::move(fault));
        return *this;
    }

    SearchSettings& settings() {
        return settings_;
    }

    const SearchSettings& settings() const {
        return settings_;
    }

    SearchState buildSearchState() const {
        SearchState state;
        for (const auto& factory : node_factories_) {
            state.addNode(factory());
        }
        return state;
    }

    SearchSettings buildSearchSettings() const {
        SearchSettings settings = settings_;
        for (const auto& fault : faults_) {
            settings.fault_names.push_back(fault->description());
            fault->apply(settings);
        }
        return settings;
    }

    Scenario build() const {
        Scenario scenario;
        scenario.state = buildSearchState();
        scenario.settings = buildSearchSettings();
        return scenario;
    }

private:
    std::vector<std::function<std::unique_ptr<Node>()>> node_factories_;
    std::vector<std::shared_ptr<const FaultWorkload>> faults_;
    SearchSettings settings_;
};

struct SearchProfile {
    int max_depth = kDefaultMaxDepth;
    int max_states = kDefaultMaxStates;
    uint32_t seed = kDefaultSearchSeed;
    int random_dfs_probes = kDefaultRandomDfsProbes;
};

SearchProfile searchProfile(int max_depth,
                            int max_states,
                            uint32_t seed = kDefaultSearchSeed,
                            int random_dfs_probes = kDefaultRandomDfsProbes) {
    return SearchProfile{max_depth, max_states, seed, random_dfs_probes};
}

struct TestSpec {
    std::string title;
    SearchProfile profile;
    bool internal_framework_test = false;
    ScenarioBuilder builder;
    std::vector<std::shared_ptr<const SearchTestWorkload>> workloads;
    std::vector<std::shared_ptr<const FaultWorkload>> faults;

    Scenario build() const {
        ScenarioBuilder working = builder;
        for (const auto& workload : workloads) {
            workload->setup(working);
        }
        for (const auto& fault : faults) {
            working.addFault(fault);
        }

        Scenario scenario = working.build();
        scenario.settings.test_title = title;
        scenario.settings.max_depth = profile.max_depth;
        scenario.settings.max_states = profile.max_states;
        scenario.settings.seed = profile.seed;
        scenario.settings.random_dfs_probes = profile.random_dfs_probes;
        scenario.settings.internal_framework_test = internal_framework_test;

        for (const auto& workload : workloads) {
            scenario.settings.workload_names.push_back(workload->description());
            workload->configure(scenario.settings);
        }
        return scenario;
    }
};

std::unique_ptr<Node> makePingServer(Address address) {
    return std::make_unique<AtMostOnceServer>(
        std::move(address),
        std::make_unique<PingApplication>());
}

SearchState buildScenario() {
    Address server = DemoAddress::server1();
    Address client = DemoAddress::client1();

    Workload workload(
        {PingCommand{"alpha"}, PingCommand{"beta"}},
        {PingResult{"pong(alpha)"}, PingResult{"pong(beta)"}});

    return ScenarioBuilder()
        .addNode(makePingServer(server))
        .addClient(client, server, 1, workload)
        .buildSearchState();
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

Workload onePingWorkload(const std::string& value) {
    return Workload(
        {PingCommand{value}},
        {PingResult{"pong(" + value + ")"}});
}

class PingClientWorkload final : public SearchTestWorkload {
public:
    PingClientWorkload(Address server,
                       Address client,
                       int client_id,
                       Workload workload)
        : server_(std::move(server)),
          client_(std::move(client)),
          client_id_(client_id),
          workload_(std::move(workload)) {}

    std::string description() const override {
        return "PingClient";
    }

    void setup(ScenarioBuilder& builder) const override {
        builder.addNode(makePingServer(server_));
        builder.addClient(client_, server_, client_id_, workload_);
    }

    void configure(SearchSettings& settings) const override {
        settings.invariants.push_back(
            Predicates::resultsOk(std::vector<Address>{client_}));
        settings.goals.push_back(
            Predicates::clientsDone(std::vector<Address>{client_}));
    }

    CheckResult check(const SearchState& state) const override {
        return clientDone(state, client_);
    }

private:
    Address server_;
    Address client_;
    int client_id_;
    Workload workload_;
};

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

Scenario oneClientScenario(const std::string& value = "alpha") {
    Address server = DemoAddress::server1();
    Address client = DemoAddress::client1();
    return ScenarioBuilder()
        .addNode(makePingServer(server))
        .addClient(client, server, 1, onePingWorkload(value))
        .build();
}

Scenario oneClientKVScenario(Workload workload) {
    Address server = DemoAddress::server1();
    Address client = DemoAddress::client1();
    return ScenarioBuilder()
        .addServer(server, std::make_unique<KVApplication>())
        .addClient(client, server, 1, std::move(workload))
        .build();
}

Scenario multiClientKVScenario(
    const std::vector<std::pair<Address, Workload>>& clients) {
    Address server = DemoAddress::server1();
    ScenarioBuilder builder;
    builder.addServer(server, std::make_unique<KVApplication>());

    int client_id = 1;
    for (const auto& client : clients) {
        builder.addClient(client.first, server, client_id++, client.second);
    }
    return builder.build();
}

int main() {
    std::cout << "BuzzDB v108: search controls, scenario builder, predicates"
              << std::endl;
    TestRunner tests;

    Address server("server1");
    Address client("client1");

    tests.test("SearchSettings uses explicit delivery priority", [&] {
        SearchSettings settings;
        MessageEnvelope envelope{
            1,
            client,
            server,
            ClientRequest{1, 1, PingCommand{"alpha"}}
        };
        tests.check(settings.shouldDeliver(envelope),
                    "default network should deliver");
        settings.networkActive(false);
        tests.check(!settings.shouldDeliver(envelope),
                    "inactive network should block delivery");
        settings.linkActive(client, server, true);
        tests.check(settings.shouldDeliver(envelope),
                    "explicit link should override network");
        settings.senderActive(client, false);
        tests.check(settings.shouldDeliver(envelope),
                    "explicit link should override sender");
        settings.resetNetwork();
        settings.senderActive(client, false);
        tests.check(!settings.shouldDeliver(envelope),
                    "inactive sender should block delivery");
        settings.receiverActive(server, true);
        tests.check(!settings.shouldDeliver(envelope),
                    "sender should take priority over receiver");
        settings.resetNetwork();
        settings.receiverActive(server, false);
        tests.check(!settings.shouldDeliver(envelope),
                    "inactive receiver should block delivery");
        settings.resetNetwork();
        tests.check(settings.shouldDeliver(envelope),
                    "resetNetwork should restore delivery");
    });

    tests.test("TestSpec runs workload setup, search config, and metadata", [&] {
        TestSpec spec;
        spec.title = "SpecHappyPing";
        spec.profile = searchProfile(8, 1000);
        spec.workloads.push_back(std::make_shared<PingClientWorkload>(
            server,
            client,
            1,
            onePingWorkload("alpha")));

        Scenario scenario = spec.build();
        SearchResults result = bfs(scenario.state, scenario.settings);
        tests.check(result.end_condition == SearchResults::EndCondition::GoalFound,
                    "test spec workload should install a reachable goal");
        tests.check(result.terminal_state.has_value(),
                    "goal search should return a terminal state");
        tests.check(result.test_title == "SpecHappyPing",
                    "search result should carry the spec title");
        tests.check(result.workload_names == std::vector<std::string>{"PingClient"},
                    "search result should carry workload names");
        tests.check(!result.internal_framework_test,
                    "ordinary specs should not be marked internal");
        tests.check(spec.workloads.front()->check(*result.terminal_state).ok,
                    "workload check should pass at the goal");
    });

    tests.test("Internal fault specs compose into search settings", [&] {
        TestSpec spec;
        spec.title = "InternalPartitionTimerBlock";
        spec.profile = searchProfile(4, 100);
        spec.internal_framework_test = true;
        spec.workloads.push_back(std::make_shared<PingClientWorkload>(
            server,
            client,
            1,
            onePingWorkload("alpha")));
        spec.faults.push_back(std::make_shared<PartitionFaultWorkload>(
            std::vector<std::vector<Address>>{{client}, {server}}));
        spec.faults.push_back(
            std::make_shared<TimerPauseFaultWorkload>(
                std::vector<Address>{client}));

        Scenario scenario = spec.build();

        SearchResults result = bfs(scenario.state, scenario.settings);
        tests.check(result.end_condition ==
                        SearchResults::EndCondition::SpaceExhausted,
                    "composed partition and timer pause should block progress");
        tests.check(messageInFlight(scenario.state, client, server).ok,
                    "client request should remain in flight");
        tests.check(timerPending(scenario.state, client).ok,
                    "client retry timer should remain pending");
        tests.check(result.internal_framework_test,
                    "fault spec should be marked as internal framework coverage");
        tests.check(result.fault_names ==
                        std::vector<std::string>{"Partition", "TimerPause"},
                    "search result should carry fault names");
    });

    tests.test("RandomDisabledLinks fault deterministically disables links", [&] {
        SearchSettings settings;
        RandomDisabledLinksFault fault(
            {client, server},
            1,
            kDefaultSearchSeed);
        fault.apply(settings);
        tests.check(!settings.link_active.empty(),
                    "random disabled-links fault should disable at least one link");
    });

    Workload blocked_workload(
        {PingCommand{"alpha"}},
        {PingResult{"pong(alpha)"}});
    std::vector<std::vector<Address>> isolated_groups{{client}, {server}};
    Scenario blocked = ScenarioBuilder()
        .addNode(makePingServer(server))
        .addClient(client, server, 1, blocked_workload)
        .partition(isolated_groups)
        .timerActive(client, false)
        .build();
    blocked.settings.max_depth = 4;
    blocked.settings.max_states = 100;
    blocked.settings.goals.push_back(Predicates::clientHasResult(client, 1));

    SearchResults blocked_result = bfs(blocked.state, blocked.settings);
    CheckResult queued_message = messageInFlight(blocked.state, client, server);
    CheckResult queued_timer = timerPending(blocked.state, client);

    tests.test("Partition and timer controls can block progress", [&] {
        tests.check(blocked_result.end_condition ==
                        SearchResults::EndCondition::SpaceExhausted,
                    "blocked search should exhaust the space");
        tests.check(blocked_result.explored == 1,
                    "blocked search should have no deliverable events");
        tests.check(queued_message.ok, "client request should remain in flight");
        tests.check(queued_timer.ok, "retry timer should remain pending");
    });

    tests.test("Single-client Put/Append/Get search", [&] {
        Scenario scenario = oneClientKVScenario(putAppendGetWorkload());
        SearchSettings settings = scenario.settings;
        settings.max_depth = 12;
        settings.max_states = 10000;
        settings.invariants.push_back(
            Predicates::resultsOk(std::vector<Address>{client}));
        settings.invariants.push_back(Predicates::atMostOnce(server));
        settings.goals.push_back(
            Predicates::clientsDone(std::vector<Address>{client}));
        SearchResults result = bfs(scenario.state, settings);
        tests.check(result.end_condition == SearchResults::EndCondition::GoalFound,
                    "Put/Append/Get client should finish");
        tests.check(result.terminal_state.has_value(),
                    "goal search should return a terminal state");

        SearchSettings safety = settings;
        safety.goals.clear();
        safety.prunes.push_back(
            Predicates::clientsDone(std::vector<Address>{client}));
        safety.max_depth = 9;
        SearchResults safety_result = bfs(scenario.state, safety);
        tests.check(safety_result.end_condition !=
                        SearchResults::EndCondition::InvariantViolated,
                    "bounded KV safety search found an invariant violation");
        tests.check(safety_result.end_condition !=
                        SearchResults::EndCondition::ExceptionThrown,
                    "bounded KV safety search threw an exception");
    });

    tests.test("Single-client Append/Append/Get search", [&] {
        Scenario scenario = oneClientKVScenario(appendAppendGetWorkload());
        SearchSettings settings = scenario.settings;
        settings.max_depth = 12;
        settings.max_states = 10000;
        settings.invariants.push_back(
            Predicates::resultsOk(std::vector<Address>{client}));
        settings.goals.push_back(
            Predicates::clientsDone(std::vector<Address>{client}));
        SearchResults result = bfs(scenario.state, settings);
        tests.check(result.end_condition == SearchResults::EndCondition::GoalFound,
                    "Append/Append/Get client should finish");
    });

    tests.test("Multi-client different-key KV search", [&] {
        Address client2("client2");
        Scenario scenario = multiClientKVScenario({
            {client, appendDifferentKeyWorkload("client1-key", 1)},
            {client2, appendDifferentKeyWorkload("client2-key", 1)}
        });
        std::vector<Address> clients{client, client2};
        SearchSettings settings = scenario.settings;
        settings.max_depth = 8;
        settings.max_states = 20000;
        settings.invariants.push_back(Predicates::resultsOk(clients));
        settings.goals.push_back(Predicates::clientsDone(clients));
        SearchResults result = bfs(scenario.state, settings);
        tests.check(result.end_condition == SearchResults::EndCondition::GoalFound,
                    "different-key clients should finish");
    });

    tests.test("Multi-client same-key append search is linearizable", [&] {
        Address client2("client2");
        Scenario scenario = multiClientKVScenario({
            {client, appendSameKeyWorkload("c1", 1)},
            {client2, appendSameKeyWorkload("c2", 1)}
        });
        std::vector<Address> clients{client, client2};
        SearchSettings settings = scenario.settings;
        settings.max_depth = 8;
        settings.max_states = 30000;
        settings.invariants.push_back(Predicates::appendsLinearizable(clients));
        settings.goals.push_back(Predicates::clientsDone(clients));
        SearchResults result = bfs(scenario.state, settings);
        tests.check(result.end_condition == SearchResults::EndCondition::GoalFound,
                    "same-key append clients should finish");

        SearchSettings safety = settings;
        safety.goals.clear();
        safety.prunes.push_back(Predicates::clientsDone(clients));
        SearchResults safety_result = bfs(scenario.state, safety);
        tests.check(safety_result.end_condition !=
                        SearchResults::EndCondition::InvariantViolated,
                    "same-key append safety search found an invariant violation");
        tests.check(safety_result.end_condition !=
                        SearchResults::EndCondition::ExceptionThrown,
                    "same-key append safety search threw an exception");
    });

    tests.test("Staged message delivery can seed a later search", [&] {
        Scenario scenario = oneClientKVScenario(putAppendGetWorkload());
        SearchSettings settings = scenario.settings;
        settings.max_depth = 12;
        settings.max_states = 10000;
        settings.invariants.push_back(
            Predicates::resultsOk(std::vector<Address>{client}));
        tests.check(scenario.state.newMessages().size() == 1,
                    "client init should create one request message");
        tests.check(scenario.state.newTimers().size() == 1,
                    "client init should create one retry timer");

        auto after_request = scenario.state.stepMessageMatching(
            settings,
            [client, server](const MessageEnvelope& envelope) {
                return envelope.from == client && envelope.to == server;
            });
        tests.check(after_request.has_value(),
                    "could not deliver the selected client request");
        tests.check(messageInFlight(*after_request, server, client).ok,
                    "server reply should be staged in the network");
        tests.check(after_request->newMessages().size() == 1,
                    "server should create one reply message");
        tests.check(after_request->newTimers().empty(),
                    "server should not create timers");
        tests.check(containsMessageMatching(
                        *after_request,
                        [](const Message& message) {
                            return std::holds_alternative<ClientReply>(message);
                        },
                        "client reply").ok,
                    "containsMessageMatching should find the server reply");
        tests.check(Predicates::containsMessageMatching(
                        "client reply",
                        [](const Message& message) {
                            return std::holds_alternative<ClientReply>(message);
                        }).predicate(*after_request).ok,
                    "predicate wrapper should find the server reply");

        auto after_reply = after_request->stepMessageMatching(
            settings,
            [client, server](const MessageEnvelope& envelope) {
                return envelope.from == server && envelope.to == client;
            });
        tests.check(after_reply.has_value(),
                    "could not deliver the selected server reply");
        tests.check(clientHasExactlyResults(*after_reply, client, 1).ok,
                    "client should have exactly one result after staged reply");
        tests.check(after_reply->newMessages().size() == 1,
                    "client should send the next request after the reply");
        tests.check(after_reply->newTimers().size() == 1,
                    "client should set a retry timer for the next request");

        settings.goals.push_back(
            Predicates::clientsDone(std::vector<Address>{client}));
        SearchResults result = bfs(*after_reply, settings);
        tests.check(result.end_condition == SearchResults::EndCondition::GoalFound,
                    "client should finish after staged prefix");
    });

    tests.test("No progress when client and server cannot communicate", [&] {
        Scenario scenario = oneClientScenario();
        SearchSettings settings = scenario.settings;
        settings.networkActive(false);
        settings.max_depth = 5;
        settings.max_states = 1000;
        settings.invariants.push_back(
            Predicates::noneDecided(std::vector<Address>{client}));
        settings.goals.push_back(
            Predicates::clientsDone(std::vector<Address>{client}));
        SearchResults result = bfs(scenario.state, settings);
        tests.check(result.end_condition ==
                        SearchResults::EndCondition::SpaceExhausted,
                    "disconnected search should exhaust without a goal");
    });

    tests.test("Duplicate message delivery preserves at-most-once", [&] {
        Scenario scenario = oneClientScenario();
        SearchSettings settings = scenario.settings;
        settings.invariants.push_back(Predicates::atMostOnce(server));
        auto requests = scenario.state.messageEventsMatching(
            settings,
            [client, server](const MessageEnvelope& envelope) {
                return envelope.from == client &&
                       envelope.to == server &&
                       std::holds_alternative<ClientRequest>(envelope.message);
            });
        tests.check(!requests.empty(), "initial client request is missing");
        EventRef request = requests.front();
        auto after_first = scenario.state.stepEvent(request, settings);
        tests.check(after_first.has_value(), "first request was not deliverable");
        auto after_duplicate = after_first->stepEvent(request, settings);
        tests.check(after_duplicate.has_value(),
                    "duplicate request was not deliverable");
        tests.check(serverAtMostOnce(*after_duplicate, server).ok,
                    "server executed duplicate request more than once");
    });

    tests.test("At-most-once cache garbage collects acknowledged requests", [&] {
        Scenario scenario = oneClientKVScenario(putAppendGetWorkload());
        SearchSettings settings = scenario.settings;
        settings.max_depth = 25;
        settings.max_states = 20000;
        settings.invariants.push_back(
            Predicates::resultsOk(std::vector<Address>{client}));
        settings.invariants.push_back(Predicates::atMostOnce(server));
        settings.goals.push_back(
            Predicates::clientsDone(std::vector<Address>{client}));

        SearchResults result = bfs(scenario.state, settings);
        tests.check(result.end_condition == SearchResults::EndCondition::GoalFound,
                    "client should finish the KV workload");
        tests.check(result.terminal_state.has_value(),
                    "goal search should return a terminal state");

        const auto* server_node =
            result.terminal_state->nodeAs<AtMostOnceServer>(server);
        tests.check(server_node != nullptr, "server is missing");
        tests.check(server_node->acknowledgedRequestId(1) == 2,
                    "server should have compacted through request 2");
        tests.check(server_node->cacheSize() == 1,
                    "only the final unacknowledged request should remain cached");
        tests.check(!server_node->hasCachedResult(1, 1),
                    "request 1 should have been removed from the cache");
        tests.check(!server_node->hasCachedResult(1, 2),
                    "request 2 should have been removed from the cache");
        tests.check(server_node->hasCachedResult(1, 3),
                    "request 3 should remain cached until a later ack");

        SearchState old_duplicate = *result.terminal_state;
        old_duplicate.send(client, server, ClientRequest{
            1,
            1,
            PutCommand{"foo", "bar"},
            0
        });
        auto after_old = old_duplicate.stepMessageMatching(
            settings,
            [client, server](const MessageEnvelope& envelope) {
                const auto* request =
                    std::get_if<ClientRequest>(&envelope.message);
                return envelope.from == client &&
                       envelope.to == server &&
                       request != nullptr &&
                       request->request_id == 1;
            });
        tests.check(after_old.has_value(),
                    "old duplicate request should be deliverable");
        tests.check(after_old->newMessages().empty(),
                    "collected old duplicate should not get a fresh reply");
        tests.check(serverAtMostOnce(*after_old, server).ok,
                    "old duplicate should not re-execute after compaction");
        const auto* after_server = after_old->nodeAs<AtMostOnceServer>(server);
        tests.check(after_server != nullptr, "server is missing after duplicate");
        tests.check(after_server->cacheSize() == 1,
                    "old duplicate should not grow the compacted cache");
        tests.check(after_server->hasCachedResult(1, 3),
                    "old duplicate should not evict the live cached request");
    });

    Workload workload(
        {PingCommand{"alpha"}, PingCommand{"beta"}},
        {PingResult{"pong(alpha)"}, PingResult{"pong(beta)"}});
    Scenario connected = ScenarioBuilder()
        .addNode(makePingServer(server))
        .addClient(client, server, 1, workload)
        .build();

    SearchSettings setup_search = connected.settings;
    setup_search.max_depth = 12;
    setup_search.max_states = 5000;
    setup_search.invariants.push_back(
        Predicates::resultsOk(std::vector<Address>{client}));
    setup_search.invariants.push_back(Predicates::atMostOnce(server));
    setup_search.goals.push_back(Predicates::clientHasResult(client, 1));

    SearchState initial = connected.state;
    CheckResult initial_invariants =
        noInvariantViolation(initial, setup_search.invariants);
    SearchResults first = bfs(initial, setup_search);

    tests.test("First-result goal search succeeds", [&] {
        tests.check(first.end_condition == SearchResults::EndCondition::GoalFound,
                    "first-result goal should be found");
        tests.check(first.terminal_state.has_value(),
                    "goal search should return a terminal state");
        tests.check(clientHasExactlyResults(*first.terminal_state, client, 1).ok,
                    "exact result-count predicate should match");
        tests.check(!clientHasExactlyResults(*first.terminal_state, client, 2).ok,
                    "exact result-count predicate should reject larger counts");
    });

    std::cout << "Partitioned/timer-disabled search: "
              << endConditionName(blocked_result.end_condition)
              << " after " << blocked_result.explored << " states"
              << " (message_in_flight="
              << (queued_message.ok ? "true" : "false")
              << ", timer_pending=" << (queued_timer.ok ? "true" : "false")
              << ")" << std::endl;
    std::cout << "Initial invariants: "
              << (initial_invariants.ok ? "passed" : initial_invariants.message)
              << std::endl;
    std::cout << "First BFS: " << endConditionName(first.end_condition)
              << " after " << first.explored << " states"
              << " (" << first.message << ")" << std::endl;

    if (first.end_condition != SearchResults::EndCondition::GoalFound ||
        !first.terminal_state) {
        return 1;
    }

    SearchSettings safety_search = setup_search;
    safety_search.goals.clear();
    // Keep this demo bounded. With duplicate-message semantics,
    // even this tiny service can grow a large tree by repeatedly redelivering
    // old client requests. Later labs will add more focused pruning predicates.
    safety_search.max_depth = first.terminal_state->depth() + 4;
    safety_search.max_states = 20000;
    safety_search.prunes.push_back(
        Predicates::clientsDone(std::vector<Address>{client}));

    SearchResults safety = bfs(*first.terminal_state, safety_search);
    std::cout << "Safety BFS from goal: "
              << endConditionName(safety.end_condition)
              << " after " << safety.explored << " states"
              << " (" << safety.message << ")" << std::endl;

    SearchResults dfs_result = randomDfs(*first.terminal_state, safety_search);
    tests.test("Continuation searches exhaust without invariant violations", [&] {
        tests.check(safety.end_condition ==
                        SearchResults::EndCondition::SpaceExhausted,
                    "safety BFS should exhaust the bounded space");
        tests.check(dfs_result.end_condition ==
                        SearchResults::EndCondition::SpaceExhausted,
                    "random DFS probes should not find a violation");
    });

    std::cout << "Random DFS from goal: "
              << endConditionName(dfs_result.end_condition)
              << " after " << dfs_result.explored << " states"
              << " (" << dfs_result.message << ")" << std::endl;

    tests.test("Bounded infinite-style workload random search finds a safe prefix", [&] {
        Scenario scenario =
            oneClientKVScenario(appendDifferentKeyWorkload("stream-key", 12));
        SearchSettings settings = scenario.settings;
        settings.deliverTimers(false);
        settings.max_depth = 35;
        settings.max_states = 5000;
        settings.random_dfs_probes = 200;
        settings.seed = 10842;
        settings.invariants.push_back(
            Predicates::resultsOk(std::vector<Address>{client}));
        settings.invariants.push_back(Predicates::atMostOnce(server));
        settings.goals.push_back({
            "BOUNDED_STREAM_PREFIX",
            [client](const SearchState& state) {
                return clientHasResult(state, client, 4);
            }
        });

        SearchResults result = randomDfs(scenario.state, settings);
        tests.check(result.end_condition == SearchResults::EndCondition::GoalFound,
                    "random DFS should find a valid running-workload prefix");
        tests.check(result.terminal_state.has_value(),
                    "prefix search should return a terminal state");
        tests.check(clientHasResult(*result.terminal_state, client, 4).ok,
                    "client should have at least four prefix results");
        tests.check(!clientDone(*result.terminal_state, client).ok,
                    "long stream should not be exhausted at the prefix goal");
        tests.check(serverAtMostOnce(*result.terminal_state, server).ok,
                    "prefix search should preserve at-most-once execution");
    });

    SearchResults replay = replayTrace(initial, setup_search, first.terminal_state->history());
    tests.test("Trace replay reaches the same goal", [&] {
        tests.check(replay.end_condition == SearchResults::EndCondition::GoalFound,
                    "replay should find the recorded goal");
        tests.check(replay.explored ==
                        static_cast<int>(first.terminal_state->history().size()),
                    "replay should consume the whole recorded trace");
    });

    std::cout << "Replay first-goal trace: "
              << endConditionName(replay.end_condition)
              << " after " << replay.explored << " events"
              << " (" << replay.message << ")" << std::endl;

    std::cout << "Goal trace:";
    for (const auto& event : first.terminal_state->history()) {
        std::cout << " " << event;
    }
    std::cout << std::endl;

    first.terminal_state->printSummary();

    bool ok =
        blocked_result.end_condition ==
            SearchResults::EndCondition::SpaceExhausted &&
        queued_message.ok &&
        queued_timer.ok &&
        initial_invariants.ok &&
        safety.end_condition == SearchResults::EndCondition::SpaceExhausted &&
        dfs_result.end_condition == SearchResults::EndCondition::SpaceExhausted &&
        replay.end_condition == SearchResults::EndCondition::GoalFound;

    std::cout << "Search harness: " << (ok ? "passed" : "failed") << std::endl;
    int test_status = tests.finish();
    return ok && test_status == 0 ? 0 : 1;
}
