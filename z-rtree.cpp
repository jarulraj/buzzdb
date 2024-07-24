#include <iostream>
#include <vector>
#include <limits>
#include <cmath>
#include <queue>

// Define a point in 2D space
struct Point {
    float x, y;
    Point() : x(0), y(0) {}
    Point(float x, float y) : x(x), y(y) {}

    bool operator<(const Point& other) const {
        return x < other.x || (x == other.x && y < other.y);
    }
};

// Define a rectangle in 2D space
struct Rectangle {
    float minX, minY, maxX, maxY;
    Rectangle() : minX(0), minY(0), maxX(0), maxY(0) {}
    Rectangle(float minX, float minY, float maxX, float maxY)
        : minX(minX), minY(minY), maxX(maxX), maxY(maxY) {}

    bool contains(const Point& p) const {
        return (p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY);
    }

    bool intersects(const Rectangle& other) const {
        return !(other.minX > maxX || other.maxX < minX || other.minY > maxY || other.maxY < minY);
    }

    void expand(const Rectangle& other) {
        if (other.minX < minX) minX = other.minX;
        if (other.minY < minY) minY = other.minY;
        if (other.maxX > maxX) maxX = other.maxX;
        if (other.maxY > maxY) maxY = other.maxY;
    }

    float area() const {
        return (maxX - minX) * (maxY - minY);
    }

    float enlargement(const Rectangle& other) const {
        Rectangle enlarged = *this;
        enlarged.expand(other);
        return enlarged.area() - this->area();
    }

    void print() const {
        std::cout << "[" << minX << ", " << minY << ", " << maxX << ", " << maxY << "]";
    }

    float minDistance(const Point& p) const {
        float dx = std::max({minX - p.x, 0.0f, p.x - maxX});
        float dy = std::max({minY - p.y, 0.0f, p.y - maxY});
        return std::sqrt(dx * dx + dy * dy);
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
            if (i == (size_t)seed1 || i == (size_t)seed2) continue;
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
            if (i == (size_t)seed1 || i == (size_t)seed2) continue;
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
                float distance = std::sqrt(std::pow(points[i].x - points[j].x, 2) +
                                           std::pow(points[i].y - points[j].y, 2));
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
                float distance = std::sqrt(std::pow(rectangles[i].minX - rectangles[j].minX, 2) +
                                           std::pow(rectangles[i].minY - rectangles[j].minY, 2));
                if (distance > maxDistance) {
                    maxDistance = distance;
                    seed1 = i;
                    seed2 = j;
                }
            }
        }
    }

    void distributeEntry(RTreeNode* node, RTreeNode* newNode, const Point& point) {
        Rectangle rect(point.x, point.y, point.x, point.y);
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
            float enlargement = (enlarged.maxX - enlarged.minX) * (enlarged.maxY - enlarged.minY) -
                                (node->childrenRectangles[i].maxX - node->childrenRectangles[i].minX) *
                                (node->childrenRectangles[i].maxY - node->childrenRectangles[i].minY);
            if (enlargement < minEnlargement) {
                minEnlargement = enlargement;
                bestChild = i;
            }
        }
        return bestChild;
    }

    Rectangle calculateBoundingRectangle(RTreeNode* node) {
        float minX = std::numeric_limits<float>::max(), minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest(), maxY = std::numeric_limits<float>::lowest();
        for (const Point& p : node->points) {
            if (p.x < minX) minX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.x > maxX) maxX = p.x;
            if (p.y > maxY) maxY = p.y;
        }
        for (const Rectangle& rect : node->childrenRectangles) {
            if (rect.minX < minX) minX = rect.minX;
            if (rect.minY < minY) minY = rect.minY;
            if (rect.maxX > maxX) maxX = rect.maxX;
            if (rect.maxY > maxY) maxY = rect.maxY;
        }
        return Rectangle(minX, minY, maxX, maxY);
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
                std::cout << "(" << p.x << ", " << p.y << ") ";
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
        Rectangle rect(point.x, point.y, point.x, point.y);
        insert(root, point, rect);
        std::cout << "Inserted point (" << point.x << ", " << point.y << ")" << std::endl;
        printTree(root);
    }

    std::vector<Point> query(const Rectangle& rect) {
        std::vector<Point> results;
        query(root, rect, results);
        return results;
    }

    // Define the nearest neighbor search function for the R-tree
    void nearestNeighbor(RTreeNode* node, const Point& queryPoint, int k, 
        std::priority_queue<std::pair<float, Point>, std::vector<std::pair<float, Point>>, std::less<>>& pq) {
        // If the current node is a leaf node
        if (node->isLeaf) {
            // Iterate through all points in the leaf node
            for (const Point& p : node->points) {
                // Calculate the Euclidean distance from the query point to the current point
                float distance = std::sqrt(std::pow(p.x - queryPoint.x, 2) + std::pow(p.y - queryPoint.y, 2));
                // Add the distance and point to the priority queue
                pq.push(std::make_pair(distance, p));
                // If the size of the priority queue exceeds k, remove the farthest point
                if (pq.size() > static_cast<size_t>(k)) {
                    pq.pop();
                }
            }
        } else { // If the current node is an internal node
            // Create a vector to store the distances from the query point to each child rectangle and the corresponding child nodes
            std::vector<std::pair<float, RTreeNode*>> childDistances;
            for (size_t i = 0; i < node->children.size(); ++i) {
                // Calculate the minimum distance from the query point to the current child rectangle
                float distance = node->childrenRectangles[i].minDistance(queryPoint);
                // Add the distance and child node to the vector
                childDistances.push_back(std::make_pair(distance, node->children[i]));
            }
            // Sort the vector in ascending order based on distance
            std::sort(childDistances.begin(), childDistances.end());

            // Recursively call the nearest neighbor function for each child node in order of increasing distance
            for (const auto& child : childDistances) {
                nearestNeighbor(child.second, queryPoint, k, pq);
            }
        }
    }

    // Function to perform nearest neighbor search
    std::vector<Point> nearestNeighbor(const Point& queryPoint, int k) {
        // Create a priority queue to store the k nearest neighbors
        std::priority_queue<std::pair<float, Point>, 
        std::vector<std::pair<float, Point>>, std::less<>> pq;
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

};

// Main function demonstrating the use case
int main() {
    RTree tree;

    // Insert some ride pickup points in a more realistic order
    std::vector<Point> points = {
        Point(3.0, 3.0), Point(2.0, 5.0), Point(8.0, 8.0), Point(7.0, 1.0), Point(5.0, 5.0),
        Point(6.0, 3.0), Point(4.0, 7.0), Point(1.0, 4.0), Point(9.0, 9.0), Point(10.0, 10.0)
    };

    for (const Point& p : points) {
        tree.insert(p);
    }

    // Define three rectangular zones for queries
    std::vector<Rectangle> queryRects = {
        Rectangle(1.5, 1.5, 4.5, 4.5),
        Rectangle(5.5, 5.5, 9.5, 9.5),
        Rectangle(0.0, 0.0, 10.0, 10.0)
    };

    for (const Rectangle& rect : queryRects) {
        std::vector<Point> results = tree.query(rect);
        std::cout << "Points within the rectangle (" << rect.minX << ", " << rect.minY
                  << ", " << rect.maxX << ", " << rect.maxY << "):" << std::endl;
        for (const Point& p : results) {
            std::cout << "(" << p.x << ", " << p.y << ")" << std::endl;
            assert(rect.contains(p) && "Point is outside the query rectangle");
        }
    }

    // Define a query point
    Point queryPoint(9.0, 8.0);

    // Find the nearest neighbors
    int k = 3;
    std::vector<Point> nearestNeighbors = tree.nearestNeighbor(queryPoint, k);

    std::cout << "The " << k << " nearest neighbors to (" << queryPoint.x << ", " << queryPoint.y << ") are:" << std::endl;
    for (const Point& p : nearestNeighbors) {
        std::cout << "(" << p.x << ", " << p.y << ")" << std::endl;
    }

    return 0;
}
