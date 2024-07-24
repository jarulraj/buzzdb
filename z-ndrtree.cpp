#include <iostream>
#include <vector>
#include <limits>
#include <cmath>
#include <queue>
#include <cassert>
#include <random>

#define POINT_COORDINATES 512

// Define a point in N-dimensional space
struct Point {
    std::vector<float> coordinates;
    std::string label;

    Point() {}
    Point(const std::vector<float>& coords) : coordinates(coords) {}

    Point(const std::vector<float>& coords, const std::string& lbl) : coordinates(coords), label(lbl) {}

    bool operator<(const Point& other) const {
        return coordinates < other.coordinates;
    }

    bool operator==(const Point& other) const {
        return coordinates == other.coordinates;
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
    std::cout << ")\n";
}

// Define a rectangle in N-dimensional space
struct Rectangle {
    std::vector<float> minCoords, maxCoords;
    Rectangle() {}
    Rectangle(const std::vector<float>& minC, const std::vector<float>& maxC)
        : minCoords(minC), maxCoords(maxC) {}

    bool contains(const Point& p) const {
        for (size_t i = 0; i < p.coordinates.size(); ++i) {
            if (p.coordinates[i] < minCoords[i] || p.coordinates[i] > maxCoords[i]) {
                return false;
            }
        }
        return true;
    }

    bool intersects(const Rectangle& other) const {
        for (size_t i = 0; i < minCoords.size(); ++i) {
            if (other.minCoords[i] > maxCoords[i] || other.maxCoords[i] < minCoords[i]) {
                return false;
            }
        }
        return true;
    }

    void expand(const Rectangle& other) {
        for (size_t i = 0; i < minCoords.size(); ++i) {
            if (other.minCoords[i] < minCoords[i]) minCoords[i] = other.minCoords[i];
            if (other.maxCoords[i] > maxCoords[i]) maxCoords[i] = other.maxCoords[i];
        }
    }

    float area() const {
        float a = 1.0;
        for (size_t i = 0; i < minCoords.size(); ++i) {
            a *= (maxCoords[i] - minCoords[i]);
        }
        return a;
    }

    float enlargement(const Rectangle& other) const {
        Rectangle enlarged = *this;
        enlarged.expand(other);
        return enlarged.area() - this->area();
    }

    void print() const {
        std::cout << "[";
        for (size_t i = 0; i < minCoords.size(); ++i) {
            std::cout << minCoords[i] << (i < minCoords.size() - 1 ? ", " : "");
        }
        std::cout << "] - [";
        for (size_t i = 0; i < maxCoords.size(); ++i) {
            std::cout << maxCoords[i] << (i < maxCoords.size() - 1 ? ", " : "");
        }
        std::cout << "]";
    }

    float minDistance(const Point& p) const {
        float distance = 0.0;
        for (size_t i = 0; i < minCoords.size(); ++i) {
            float dx = std::max({minCoords[i] - p.coordinates[i], 0.0f, p.coordinates[i] - maxCoords[i]});
            distance += dx * dx;
        }
        return std::sqrt(distance);
    }
};

// Define a node in the R-tree
struct RTreeNode {
    bool isLeaf;
    std::vector<Point> points;
    std::vector<Rectangle> childrenRectangles;
    std::vector<RTreeNode*> children;

    RTreeNode(bool isLeaf) : isLeaf(isLeaf) {}
};

// Define the R-tree
class RTree {
private:
    RTreeNode* root;
    int maxPoints;

    void insert(RTreeNode* node, const Point& point, const Rectangle& rect) {
        if (node->isLeaf) {
            node->points.push_back(point);
            if (node->points.size() > static_cast<size_t>(maxPoints)) {
                split(node);
            }
        } else {
            int bestChild = chooseBestChild(node, rect);
            insert(node->children[bestChild], point, rect);
            node->childrenRectangles[bestChild].expand(rect);
        }
    }

    void split(RTreeNode* node) {
        if (node->isLeaf) {
            quadraticSplitLeaf(node);
        } else {
            quadraticSplitInternal(node);
        }
    }

    void quadraticSplitLeaf(RTreeNode* node) {
        std::vector<Point> points = node->points;
        node->points.clear();

        // Choose seeds
        int seed1, seed2;
        chooseSeeds(points, seed1, seed2);

        RTreeNode* newNode = new RTreeNode(true);
        node->points.push_back(points[seed1]);
        newNode->points.push_back(points[seed2]);

        // Distribute remaining entries
        for (size_t i = 0; i < points.size(); ++i) {
            if (i == static_cast<size_t>(seed1) || i == static_cast<size_t>(seed2)) continue;
            distributeEntry(node, newNode, points[i]);
        }

        if (node == root) {
            RTreeNode* newRoot = new RTreeNode(false);
            newRoot->children.push_back(node);
            newRoot->children.push_back(newNode);
            newRoot->childrenRectangles.push_back(calculateBoundingRectangle(node));
            newRoot->childrenRectangles.push_back(calculateBoundingRectangle(newNode));
            root = newRoot;
        } else {
            // Update parent node with new child
            updateParent(node, newNode);
        }
    }

    void quadraticSplitInternal(RTreeNode* node) {
        std::vector<Rectangle> rectangles = node->childrenRectangles;
        std::vector<RTreeNode*> children = node->children;
        node->childrenRectangles.clear();
        node->children.clear();

        // Choose seeds
        int seed1, seed2;
        chooseSeeds(rectangles, seed1, seed2);

        RTreeNode* newNode = new RTreeNode(false);
        node->children.push_back(children[seed1]);
        newNode->children.push_back(children[seed2]);
        node->childrenRectangles.push_back(rectangles[seed1]);
        newNode->childrenRectangles.push_back(rectangles[seed2]);

        // Distribute remaining entries
        for (size_t i = 0; i < rectangles.size(); ++i) {
            if (i == static_cast<size_t>(seed1) || i == static_cast<size_t>(seed2)) continue;
            distributeEntry(node, newNode, rectangles[i], children[i]);
        }

        if (node == root) {
            RTreeNode* newRoot = new RTreeNode(false);
            newRoot->children.push_back(node);
            newRoot->children.push_back(newNode);
            newRoot->childrenRectangles.push_back(calculateBoundingRectangle(node));
            newRoot->childrenRectangles.push_back(calculateBoundingRectangle(newNode));
            root = newRoot;
        } else {
            // Update parent node with new child
            updateParent(node, newNode);
        }
    }

    void chooseSeeds(const std::vector<Point>& points, int& seed1, int& seed2) {
        float maxDistance = -1;
        for (size_t i = 0; i < points.size(); ++i) {
            for (size_t j = i + 1; j < points.size(); ++j) {
                float distance = 0.0;
                for (size_t d = 0; d < points[i].coordinates.size(); ++d) {
                    distance += std::pow(points[i].coordinates[d] - points[j].coordinates[d], 2);
                }
                distance = std::sqrt(distance);
                if (distance > maxDistance) {
                    maxDistance = distance;
                    seed1 = i;
                    seed2 = j;
                }
            }
        }
    }

    void chooseSeeds(const std::vector<Rectangle>& rectangles, int& seed1, int& seed2) {
        float maxDistance = -1;
        for (size_t i = 0; i < rectangles.size(); ++i) {
            for (size_t j = i + 1; j < rectangles.size(); ++j) {
                float distance = 0.0;
                for (size_t d = 0; d < rectangles[i].minCoords.size(); ++d) {
                    distance += std::pow(rectangles[i].minCoords[d] - rectangles[j].minCoords[d], 2);
                }
                distance = std::sqrt(distance);
                if (distance > maxDistance) {
                    maxDistance = distance;
                    seed1 = i;
                    seed2 = j;
                }
            }
        }
    }

    void distributeEntry(RTreeNode* node, RTreeNode* newNode, const Point& point) {
        Rectangle rect(point.coordinates, point.coordinates);
        float enlargement1 = calculateBoundingRectangle(node).enlargement(rect);
        float enlargement2 = calculateBoundingRectangle(newNode).enlargement(rect);

        if (enlargement1 < enlargement2) {
            node->points.push_back(point);
        } else {
            newNode->points.push_back(point);
        }
    }

    void distributeEntry(RTreeNode* node, RTreeNode* newNode, const Rectangle& rect, RTreeNode* child) {
        float enlargement1 = calculateBoundingRectangle(node).enlargement(rect);
        float enlargement2 = calculateBoundingRectangle(newNode).enlargement(rect);

        if (enlargement1 < enlargement2) {
            node->children.push_back(child);
            node->childrenRectangles.push_back(rect);
        } else {
            newNode->children.push_back(child);
            newNode->childrenRectangles.push_back(rect);
        }
    }

    void updateParent(RTreeNode* node, RTreeNode* newNode) {
        for (RTreeNode* parent : findParents(root, node)) {
            parent->children.push_back(newNode);
            parent->childrenRectangles.push_back(calculateBoundingRectangle(newNode));

            if (parent->children.size() > static_cast<size_t>(maxPoints)) {
                split(parent);
            }
        }
    }

    std::vector<RTreeNode*> findParents(RTreeNode* currentNode, RTreeNode* targetNode) {
        std::vector<RTreeNode*> parents;
        if (!currentNode->isLeaf) {
            for (size_t i = 0; i < currentNode->children.size(); ++i) {
                if (currentNode->children[i] == targetNode) {
                    parents.push_back(currentNode);
                } else {
                    std::vector<RTreeNode*> foundParents = findParents(currentNode->children[i], targetNode);
                    parents.insert(parents.end(), foundParents.begin(), foundParents.end());
                }
            }
        }
        return parents;
    }

    int chooseBestChild(RTreeNode* node, const Rectangle& rect) {
        int bestChild = 0;
        float minEnlargement = std::numeric_limits<float>::max();
        for (size_t i = 0; i < node->children.size(); ++i) {
            Rectangle enlarged = node->childrenRectangles[i];
            enlarged.expand(rect);
            float enlargement = enlarged.area() - node->childrenRectangles[i].area();
            if (enlargement < minEnlargement) {
                minEnlargement = enlargement;
                bestChild = i;
            }
        }
        return bestChild;
    }

    Rectangle calculateBoundingRectangle(RTreeNode* node) {
        size_t dims = node->points.empty() ? node->childrenRectangles[0].minCoords.size() : node->points[0].coordinates.size();
        std::vector<float> minCoords(dims, std::numeric_limits<float>::max());
        std::vector<float> maxCoords(dims, std::numeric_limits<float>::lowest());

        for (const Point& p : node->points) {
            for (size_t i = 0; i < p.coordinates.size(); ++i) {
                if (p.coordinates[i] < minCoords[i]) minCoords[i] = p.coordinates[i];
                if (p.coordinates[i] > maxCoords[i]) maxCoords[i] = p.coordinates[i];
            }
        }

        for (const Rectangle& rect : node->childrenRectangles) {
            for (size_t i = 0; i < rect.minCoords.size(); ++i) {
                if (rect.minCoords[i] < minCoords[i]) minCoords[i] = rect.minCoords[i];
                if (rect.maxCoords[i] > maxCoords[i]) maxCoords[i] = rect.maxCoords[i];
            }
        }
        return Rectangle(minCoords, maxCoords);
    }

    void query(RTreeNode* node, const Rectangle& rect, std::vector<Point>& results) {
        if (node->isLeaf) {
            for (const Point& p : node->points) {
                if (rect.contains(p)) {
                    results.push_back(p);
                }
            }
        } else {
            for (size_t i = 0; i < node->children.size(); ++i) {
                if (rect.intersects(node->childrenRectangles[i])) {
                    query(node->children[i], rect, results);
                }
            }
        }
    }

    void printTree(RTreeNode* node, int depth = 0) const {
        if (node == nullptr) return;
        std::string indent(depth * 2, ' ');
        if (node->isLeaf) {
            std::cout << indent << "Leaf Node with points: ";
            for (const Point& p : node->points) {
                std::cout << "(";
                for (size_t i = 0; i < p.coordinates.size(); ++i) {
                    std::cout << p.coordinates[i] << (i < p.coordinates.size() - 1 ? ", " : "");
                }
                std::cout << ") ";
            }
            std::cout << std::endl;
        } else {
            std::cout << indent << "Internal Node with bounding rectangles: ";
            for (const Rectangle& r : node->childrenRectangles) {
                r.print();
                std::cout << " ";
            }
            std::cout << std::endl;
            for (size_t i = 0; i < node->children.size(); ++i) {
                printTree(node->children[i], depth + 1);
            }
        }
    }

public:
    RTree(int maxPoints = 4) : root(new RTreeNode(true)), maxPoints(maxPoints) {}

    void insert(const Point& point) {
        Rectangle rect(point.coordinates, point.coordinates);
        insert(root, point, rect);
    }

    std::vector<Point> query(const Rectangle& rect) {
        std::vector<Point> results;
        query(root, rect, results);
        return results;
    }

    // Function to perform nearest neighbor search
    std::vector<Point> nearestNeighbor(const Point& queryPoint, int k) {
        // Create a priority queue to store the k nearest neighbors
        std::priority_queue<std::pair<float, Point>, std::vector<std::pair<float, Point>>, std::less<>> pq;
        // Call the recursive nearest neighbor search function starting from the root node
        nearestNeighbor(root, queryPoint, k, pq);

        // Create a vector to store the results
        std::vector<Point> results;
        // Retrieve the k nearest neighbors from the priority queue
        while (!pq.empty()) {
            results.push_back(pq.top().second);
            pq.pop();
        }

        return results;
    }

    void print(){
        printTree(root);
    }

private:
    // Helper function to recursively search for nearest neighbors
    void nearestNeighbor(RTreeNode* node, const Point& queryPoint, int k, 
                         std::priority_queue<std::pair<float, Point>, std::vector<std::pair<float, Point>>, std::less<>>& pq) {
        if (node->isLeaf) {
            // Check all points in the leaf node
            for (const Point& p : node->points) {
                float distance = 0.0;
                for (size_t i = 0; i < p.coordinates.size(); ++i) {
                    distance += std::pow(p.coordinates[i] - queryPoint.coordinates[i], 2);
                }
                distance = std::sqrt(distance);
                pq.push(std::make_pair(distance, p));
                // Keep only the k closest points in the priority queue
                if (pq.size() > static_cast<size_t>(k)) {
                    pq.pop();
                }
            }
        } else {
            // Calculate distances from the query point to each child rectangle
            std::vector<std::pair<float, RTreeNode*>> childDistances;
            for (size_t i = 0; i < node->children.size(); ++i) {
                float distance = node->childrenRectangles[i].minDistance(queryPoint);
                childDistances.push_back(std::make_pair(distance, node->children[i]));
            }

            // Sort children by distance to ensure the closest children are processed first
            std::sort(childDistances.begin(), childDistances.end());

            // Traverse child nodes in order of distance
            for (const auto& child : childDistances) {
                nearestNeighbor(child.second, queryPoint, k, pq);
            }
        }
    }
};


int main() {
    RTree tree;

    std::mt19937 gen(42);  // Fixed seed for reproducibility
    std::uniform_real_distribution<float> dis(0.0, 1.0);

    // Generate clustered 100 128-dimensional feature vectors
    std::vector<Point> points;
    for (int cluster = 0; cluster < 10; ++cluster) {
        std::vector<float> center(POINT_COORDINATES);
        for (float &val : center) {
            val = dis(gen) * 100;  // Center of the cluster
        }

        for (int i = 0; i < 10; ++i) {
            std::vector<float> coordinates(POINT_COORDINATES);
            for (int j = 0; j < POINT_COORDINATES; ++j) {
                coordinates[j] = center[j] + dis(gen) * 10;  // Points around the center
            }
            points.emplace_back(
                coordinates, 
                "Point_" + std::to_string(cluster * 10 + i)
            );
        }
    }

    // Insert points into RTree
    for (const Point& point : points) {
        tree.insert(point);
    }

    // Select a random point from the generated points as the query point
    //std::uniform_int_distribution<int> pointDis(0, points.size() - 1);
    const int pointID = 66;
    Point queryPoint = points[pointID];

    // Find the nearest neighbors
    int k = 3;
    std::vector<Point> nearestNeighbors = tree.nearestNeighbor(queryPoint, k);

    std::cout << "The " << k << " nearest neighbors to (" << queryPoint.label << "):" << std::endl;
    printPoint(queryPoint);
    for (const Point& p : nearestNeighbors) {
        printPoint(p);
    }

    return 0;
}

