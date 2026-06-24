#include <deque>
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

// BuzzDB v105: framework primitives.
//
// This version intentionally leaves the real BuzzDB engine out. The goal is to
// establish a distributed-systems vocabulary: addresses, commands, results,
// applications, nodes, messages, a node context, and an in-process SimState.

struct Address {
    std::string root;
    std::vector<std::string> path;

    explicit Address(std::string root_id = "") : root(std::move(root_id)) {}

    Address(std::string root_id, std::vector<std::string> sub_path)
        : root(std::move(root_id)), path(std::move(sub_path)) {}

    Address rootAddress() const {
        return Address(root);
    }

    Address subAddress(const std::string& id) const {
        std::vector<std::string> next = path;
        next.push_back(id);
        return Address(root, next);
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

using Command = std::variant<PingCommand>;

struct PingResult {
    std::string value;
};

using Result = std::variant<PingResult>;

bool operator==(const PingResult& lhs, const PingResult& rhs) {
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

struct ClientRequest {
    int request_id;
    Command command;
};

struct ClientReply {
    int request_id;
    Result result;
};

using Message = std::variant<ClientRequest, ClientReply>;

std::string describeMessage(const Message& message) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ClientRequest>) {
                out << "ClientRequest(req=" << value.request_id
                    << ", " << describeCommand(value.command) << ")";
            } else if constexpr (std::is_same_v<T, ClientReply>) {
                out << "ClientReply(req=" << value.request_id
                    << ", " << describeResult(value.result) << ")";
            }
            return out.str();
        },
        message);
}

struct MessageEnvelope {
    Address from;
    Address to;
    Message message;
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

    virtual std::unique_ptr<Node> clone() const = 0;
    virtual std::string describe() const = 0;

private:
    Address address_;
};

class NodeGenerator {
public:
    using Factory = std::function<std::unique_ptr<Node>(Address)>;

    explicit NodeGenerator(Factory factory) : factory_(std::move(factory)) {}

    std::unique_ptr<Node> make(Address address) const {
        return factory_(std::move(address));
    }

private:
    Factory factory_;
};

class SimState {
public:
    void addNode(std::unique_ptr<Node> node) {
        Address address = node->address();
        nodes_.emplace(address.rootAddress(), std::move(node));
    }

    void addNode(const NodeGenerator& generator, Address address) {
        addNode(generator.make(std::move(address)));
    }

    void initAll() {
        for (auto& entry : nodes_) {
            NodeContext ctx(*this, entry.first);
            entry.second->init(ctx);
        }
    }

    void send(const Address& from, const Address& to, Message message) {
        MessageEnvelope envelope{from, to, std::move(message)};
        trace_.push_back("enqueue " + describe(envelope));
        network_.push_back(std::move(envelope));
    }

    bool step() {
        if (network_.empty()) {
            return false;
        }

        MessageEnvelope envelope = network_.front();
        network_.pop_front();
        trace_.push_back("deliver " + describe(envelope));

        auto node = nodes_.find(envelope.to.rootAddress());
        if (node == nodes_.end()) {
            trace_.push_back("drop missing destination " + envelope.to.str());
            return true;
        }

        NodeContext ctx(*this, envelope.to.rootAddress());
        node->second->onMessage(ctx, envelope.from, envelope.message);
        return true;
    }

    void run() {
        while (step()) {}
    }

    size_t networkSize() const {
        return network_.size();
    }

    const std::vector<std::string>& trace() const {
        return trace_;
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
    static std::string describe(const MessageEnvelope& envelope) {
        std::ostringstream out;
        out << envelope.from << " -> " << envelope.to << " "
            << describeMessage(envelope.message);
        return out.str();
    }

    std::map<Address, std::unique_ptr<Node>> nodes_;
    std::deque<MessageEnvelope> network_;
    std::vector<std::string> trace_;
};

void NodeContext::send(const Address& to, Message message) {
    state_.send(self_, to, std::move(message));
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
            ctx.send(from, ClientReply{request->request_id, result});
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

class OneShotClient : public Node {
public:
    OneShotClient(Address address, Address server, Command command)
        : Node(std::move(address)),
          server_(std::move(server)),
          command_(std::move(command)) {}

    void init(NodeContext& ctx) override {
        ctx.send(server_, ClientRequest{1, command_});
    }

    void onMessage(NodeContext& ctx,
                   const Address& from,
                   const Message& message) override {
        (void) ctx;
        (void) from;
        if (const auto* reply = std::get_if<ClientReply>(&message)) {
            last_result_ = reply->result;
        }
    }

    std::unique_ptr<Node> clone() const override {
        return std::make_unique<OneShotClient>(*this);
    }

    const std::optional<Result>& lastResult() const {
        return last_result_;
    }

    std::string describe() const override {
        if (!last_result_) {
            return "OneShotClient{result=<none>}";
        }
        return "OneShotClient{result=" + describeResult(*last_result_) + "}";
    }

private:
    Address server_;
    Command command_;
    std::optional<Result> last_result_;
};

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

bool traceContains(const SimState& state, const std::string& needle) {
    for (const auto& line : state.trace()) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

struct ScenarioSpec {
    std::string title;
    std::function<SimState()> build;
};

SimState oneShotScenario(const std::string& value) {
    SimState state;

    NodeGenerator server_generator([](Address address) {
        return std::make_unique<ApplicationServer>(
            std::move(address),
            std::make_unique<PingApplication>());
    });

    state.addNode(server_generator, Address("server1"));
    state.addNode(std::make_unique<OneShotClient>(
        Address("client1"),
        Address("server1"),
        PingCommand{value}));

    state.initAll();
    return state;
}

int main() {
    std::cout << "BuzzDB v105: framework primitives" << std::endl;
    TestRunner tests;

    tests.test("Address root and sub-addresses", [&] {
        Address root("server1");
        Address child = root.subAddress("client").subAddress("worker");
        tests.check(root.rootAddress() == Address("server1"), "root address changed");
        tests.check(child.rootAddress() == Address("server1"),
                    "sub-address root is wrong");
        tests.check(child.str() == "server1/client/worker",
                    "sub-address string is wrong: " + child.str());
    });

    tests.test("PingApplication executes and clones state", [&] {
        PingApplication app;
        Result alpha = app.execute(PingCommand{"alpha"});
        tests.check(alpha == Result{PingResult{"pong(alpha)"}},
                    "wrong ping result");
        std::unique_ptr<Application> clone = app.clone();
        Result beta = clone->execute(PingCommand{"beta"});
        tests.check(beta == Result{PingResult{"pong(beta)"}},
                    "wrong clone result");
        tests.check(app.describe() == "PingApplication{executed=1}",
                    "clone execution leaked into original app");
    });

    tests.test("Single client/server round trip", [&] {
        SimState state = oneShotScenario("hello");
        state.run();
        const auto* client = state.nodeAs<OneShotClient>(Address("client1"));
        tests.check(client != nullptr, "client is missing");
        tests.check(client->lastResult().has_value(), "client did not get a result");
        tests.check(*client->lastResult() == Result{PingResult{"pong(hello)"}},
                    "client got the wrong result");
        tests.check(state.networkSize() == 0, "network did not drain");
    });

    tests.test("ScenarioSpec builds a named one-shot scenario", [&] {
        ScenarioSpec spec{
            "HappyPing",
            [] {
                return oneShotScenario("gamma");
            }
        };
        SimState state = spec.build();
        state.run();
        const auto* client = state.nodeAs<OneShotClient>(Address("client1"));
        tests.check(spec.title == "HappyPing", "scenario title was not preserved");
        tests.check(client != nullptr, "client is missing");
        tests.check(client->lastResult().has_value(),
                    "named scenario did not produce a result");
    });

    tests.test("Missing destinations are dropped", [&] {
        SimState state;
        state.addNode(std::make_unique<OneShotClient>(
            Address("client1"),
            Address("missing-server"),
            PingCommand{"lost"}));
        state.initAll();
        state.run();
        tests.check(traceContains(state, "drop missing destination missing-server"),
                    "missing destination was not reported");
    });

    SimState state = oneShotScenario("hello");
    state.run();
    state.printTrace();
    state.printNodes();

    return tests.finish();
}
