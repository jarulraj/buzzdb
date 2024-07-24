#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <cmath>
#include <random>
#include <limits>
#include <algorithm>
#include <cassert>

#define POINT_COORDINATES 512

// Define the Point structure with better constructors
struct Point {
    std::vector<float> coordinates;
    std::string label;

    // Use explicit constructors to avoid unexpected conversions
    explicit Point(const std::vector<float>& coords, const std::string& lbl) 
        : coordinates(coords), label(lbl) {}

    bool operator<(const Point& other) const {
        return coordinates < other.coordinates;
    }

    bool operator==(const Point& other) const {
        return coordinates == other.coordinates && label == other.label;
    }
};

void printPoint(const Point& p) {
    std::cout << p.label << " (";
    size_t numCoordinates = p.coordinates.size();
    for (size_t i = 0; i < std::min(numCoordinates, size_t(10)); ++i) {
        std::cout << p.coordinates[i];
        if (i < std::min(numCoordinates, size_t(10)) - 1) {
            std::cout << ", ";
        }
    }
    if (numCoordinates > 10) {
        std::cout << ", ...";
    }
    std::cout << ")";
}

// Calculate the Euclidean distance between two points
float euclideanDistance(const Point& p1, const Point& p2) {
    float distance = 0.0f;
    for (size_t i = 0; i < std::min(p1.coordinates.size(), p2.coordinates.size()); ++i) {
        distance += pow(p1.coordinates[i] - p2.coordinates[i], 2);
    }
    return sqrt(distance);
}

// Define the Node structure
struct Node {
    Point point;
    std::vector<std::vector<Node*>> neighbors;

    explicit Node(const Point& pt, int maxLevel) 
        : point(pt), neighbors(maxLevel + 1) {}
};

// Define the Graph structure
class Graph {
public:
    std::vector<Node*> nodes;
    Node* entrance;

    Graph() : entrance(nullptr) {}
    ~Graph() {
        for (Node* node : nodes) {
            delete node;
        }
        nodes.clear();
    }
};

// Define the HNSW structure
class HNSW {
private:
    Graph graph;
    int maxLevel;
    int maxNeighbors;
    int efConstruction;
    float levelMult;
    std::default_random_engine generator;

    int getRandomLevel() {
        std::uniform_real_distribution<float> distribution(0.0, 1.0);
        float r = distribution(generator);
        return static_cast<int>(-std::log(r) * levelMult);
    }

    void insertNode(Node* newNode) {
        if (graph.nodes.empty()) {
            graph.nodes.push_back(newNode);
            graph.entrance = newNode;
            return;
        }

        Node* enterPoint = graph.entrance;
        int level = maxLevel;

        // Ensure layer access is within bounds
        int effectiveLevel = std::min(static_cast<int>(newNode->neighbors.size()) - 1, level);
        for (int lc = level; lc > effectiveLevel; --lc) {
            enterPoint = searchLayer(enterPoint, newNode->point, lc, 1).top().second;
        }

        for (int lc = effectiveLevel; lc >= 0; --lc) {
            auto topCandidates = searchLayer(enterPoint, newNode->point, lc, efConstruction);
            selectNeighbors(newNode, topCandidates, lc);
        }

        maxLevel = std::max(maxLevel, effectiveLevel);
        graph.nodes.push_back(newNode);
    }

    std::priority_queue<std::pair<float, Node*>> searchLayer(Node* enterPoint, const Point& point, int level, int ef) {
        std::priority_queue<std::pair<float, Node*>> topCandidates;
        std::priority_queue<std::pair<float, Node*>, std::vector<std::pair<float, Node*>>, std::greater<>> candidates;
        std::unordered_map<Node*, bool> visited;

        float lowerBound = euclideanDistance(point, enterPoint->point);
        candidates.emplace(lowerBound, enterPoint);
        topCandidates.emplace(lowerBound, enterPoint);
        visited[enterPoint] = true;

        while (!candidates.empty()) {
            auto currPair = candidates.top();
            float dist = currPair.first;
            Node* currNode = currPair.second;
            candidates.pop();

            if (dist > lowerBound) break;

            for (Node* neighbor : currNode->neighbors[level]) {
                if (!visited[neighbor]) {
                    visited[neighbor] = true;
                    float d = euclideanDistance(point, neighbor->point);
                    if (topCandidates.size() < static_cast<size_t>(ef) || d < lowerBound) {
                        candidates.emplace(d, neighbor);
                        topCandidates.emplace(d, neighbor);
                        if (topCandidates.size() > static_cast<size_t>(ef)) {
                            topCandidates.pop();
                            lowerBound = topCandidates.top().first;
                        }
                    }
                }
            }
        }

        return topCandidates;
    }

    void selectNeighbors(Node* newNode, std::priority_queue<std::pair<float, Node*>>& topCandidates, int level) {
        std::vector<Node*> neighbors;
        std::unordered_set<Node*> selected;
        while (!topCandidates.empty() && neighbors.size() < static_cast<size_t>(maxNeighbors)) {
            Node* neighbor = topCandidates.top().second;
            topCandidates.pop();
            if (selected.insert(neighbor).second) {
                neighbors.push_back(neighbor);
            }
        }
        for (Node* neighbor : neighbors) {
            newNode->neighbors[level].push_back(neighbor);
            neighbor->neighbors[level].push_back(newNode);
        }
    }

public:
    HNSW(int maxNeighbors, int efConstr, float mult) 
        : maxLevel(0), maxNeighbors(maxNeighbors), efConstruction(efConstr), levelMult(mult) {}

    void insert(const Point& point) {
        Node* newNode = new Node(point, getRandomLevel());
        insertNode(newNode);
    }

    std::vector<Point> search(const Point& query, int k) {
        if (graph.nodes.empty()) return {};

        Node* enterPoint = graph.entrance;
        int level = maxLevel;

        while (level > 0) {
            auto searchResults = searchLayer(enterPoint, query, level, 1);
            if (!searchResults.empty()) {
                enterPoint = searchResults.top().second;
            }
            --level;
        }

        auto topCandidates = searchLayer(enterPoint, query, 0, k);
        std::vector<Point> results;
        while (!topCandidates.empty()) {
            results.push_back(topCandidates.top().second->point);
            topCandidates.pop();
        }

        return results;
    }
};

bool verifyNearestNeighbors(const Point& queryPoint, std::vector<Point>& results, const std::vector<Point>& allPoints, size_t k) {
    // Create a priority queue to find the k nearest neighbors in the original data
    std::priority_queue<std::pair<float, Point>> pq;

    for (const Point& point : allPoints) {
        float distance = euclideanDistance(queryPoint, point);
        pq.push(std::make_pair(distance, point));
        if (pq.size() > k) {
            pq.pop();
        }
    }

    // Extract the k nearest neighbors from the priority queue
    std::vector<std::pair<float, Point>> expectedResults;
    while (!pq.empty()) {
        expectedResults.push_back(pq.top());
        pq.pop();
    }

    // Sort the expected results and the actual results by distance to compare them
    auto distanceComparator = [](const std::pair<float, Point>& a, const std::pair<float, Point>& b) {
        return a.first < b.first;
    };

    std::sort(expectedResults.begin(), expectedResults.end(), distanceComparator);

    std::vector<std::pair<float, Point>> actualResults;
    for (const Point& point : results) {
        actualResults.push_back(std::make_pair(euclideanDistance(queryPoint, point), point));
    }
    std::sort(actualResults.begin(), actualResults.end(), distanceComparator);

    // Verify that the actual results match the expected results
    bool isCorrect = (expectedResults == actualResults);

    std::cout << "Optimal neighbors:\n";
    for (const auto& pair : expectedResults) {
        const Point& point = pair.second;
        printPoint(point);
        std::cout << " Distance: " << pair.first << "\n";
    }

    std::cout << "HNSW neighbors:\n";
    for (const auto& pair : actualResults) {
        const Point& point = pair.second;
        printPoint(point);
        std::cout << " Distance: " << pair.first << "\n";
    }

    return isCorrect;
}

int main() {
    HNSW hnsw(4, 200, 1.0f);

    std::mt19937 gen(42);
    std::uniform_real_distribution<float> dis(0.0, 1.0);

    std::vector<Point> points;
    for (int cluster = 0; cluster < 10; ++cluster) {
        std::vector<float> center(POINT_COORDINATES);
        for (float &val : center) {
            val = dis(gen) * 100;
        }

        for (int i = 0; i < 10; ++i) {
            std::vector<float> coordinates(POINT_COORDINATES);
            for (int j = 0; j < POINT_COORDINATES; ++j) {
                coordinates[j] = center[j] + dis(gen) * 10;
            }
            points.emplace_back(coordinates, "Point_" + std::to_string(cluster * 10 + i));
        }
    }

    for (const Point& point : points) {
        hnsw.insert(point);
    }

    const int pointID = 66;  // Selecting a specific point for consistent results
    Point queryPoint = points[pointID];

    int k = 3;  // Number of nearest neighbors to find
    std::vector<Point> results = hnsw.search(queryPoint, k);

    std::cout << "The " << k << " nearest neighbors to (" << queryPoint.label << "):" << std::endl;
    std::cout << "\n";
    for (const Point& result : results) {
        printPoint(result);
        std::cout << "\n";
    }

    // Verify the nearest neighbors
    verifyNearestNeighbors(queryPoint, results, points, k);

    return 0;
}
