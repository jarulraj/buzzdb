#include <algorithm>
#include <vector>
#include <iostream>
#include <queue>
using namespace std;

/* ========================= Geometry ========================= */

struct Point {
    float x, y;
};

struct Rect {
    float minX, minY, maxX, maxY;

    Rect() : minX(0), minY(0), maxX(0), maxY(0) {}
    Rect(float x1, float y1, float x2, float y2)
        : minX(min(x1,x2)), minY(min(y1,y2)),
          maxX(max(x1,x2)), maxY(max(y1,y2)) {}

    static Rect fromPoint(Point p) { return Rect(p.x, p.y, p.x, p.y); }

    bool valid() const { return minX <= maxX && minY <= maxY; }

    bool intersects(const Rect& o) const {
        return !(o.minX > maxX || o.maxX < minX || o.minY > maxY || o.maxY < minY);
    }

    bool contains(const Point& p) const {
        return p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY;
    }

    float area() const {
        return max(0.0f, (maxX - minX)) * max(0.0f, (maxY - minY));
    }

    float enlargement(const Rect& o) const {
        Rect e = *this;
        e.expand(o);
        return e.area() - this->area();
    }

    void expand(const Rect& o) {
        if (!valid()) { *this = o; return; }
        minX = std::min(minX, o.minX);
        minY = std::min(minY, o.minY);
        maxX = std::max(maxX, o.maxX);
        maxY = std::max(maxY, o.maxY);
    }

    static Rect combine(const Rect& a, const Rect& b) {
        Rect r = a; r.expand(b); return r;
    }

    // min distance from point to rectangle (0 if inside)
    float minDist(const Point& p) const {
        float dx = 0.0f;
        if      (p.x < minX) dx = minX - p.x;
        else if (p.x > maxX) dx = p.x - maxX;
        float dy = 0.0f;
        if      (p.y < minY) dy = minY - p.y;
        else if (p.y > maxY) dy = p.y - maxY;
        return std::sqrt(dx*dx + dy*dy);
    }
};

/* ========================= R-Tree ========================= */

struct Node;

struct Entry {
    Rect mbr;
    Node* child;   // nullptr for leaf entries
    int   id;      // payload id if leaf; undefined for internal
    Entry(const Rect& r, Node* c, int id=-1) : mbr(r), child(c), id(id) {}
};

struct Node {
    bool isLeaf;
    vector<Entry> entries;
    Node* parent = nullptr;

    explicit Node(bool leaf): isLeaf(leaf) {}
};

class RTree {
public:
    explicit RTree(int maxEntries = 8)
        : M(max(4, maxEntries)), m((M+1)/2)  // min-fill = ceil((M+1)/2)
    {
        root = new Node(true);
    }

    ~RTree() { destroy(root); }

    void insertPoint(Point p, int id) {
        Rect r = Rect::fromPoint(p);
        insertRect(r, id);
    }

    void insertRect(const Rect& r, int id) {
        Node* leaf = chooseSubtree(root, r);
        leaf->entries.emplace_back(r, nullptr, id);
        if ((int)leaf->entries.size() > M) handleOverflow(leaf);
        adjustMBRsUpward(leaf);
    }

    vector<int> windowQuery(const Rect& q) const {
        vector<int> out;
        windowQuery(root, q, out);
        return out;
    }

    vector<int> kNearest(const Point& q, int k) const {
        if (k <= 0) return {};
        using QItem = pair<float, const Node*>; // min-heap by bound
        auto cmp = [](const QItem& a, const QItem& b){ return a.first > b.first; };
        priority_queue<QItem, vector<QItem>, decltype(cmp)> pq(cmp);

        float kth = numeric_limits<float>::infinity();
        vector<pair<float,int>> best; best.reserve(k);

        // best-first traversal over nodes by MBR distance
        pq.emplace(0.0f, root);
        while(!pq.empty()) {
            auto [bound, node] = pq.top(); pq.pop();
            if (bound > kth) break; // prune

            if (node->isLeaf) {
                for (auto& e : node->entries) {
                    // distance to the point payload
                    Point p{e.mbr.minX, e.mbr.minY}; // zero-area rect ⇒ point
                    float d = std::sqrt((p.x-q.x)*(p.x-q.x) + (p.y-q.y)*(p.y-q.y));
                    insertBest(best, k, d, e.id, kth);
                }
            } else {
                for (auto& e : node->entries) {
                    float b = e.mbr.minDist(q);
                    if (b <= kth) pq.emplace(b, e.child);
                }
            }
        }
        // sort results by distance ascending
        sort(best.begin(), best.end());
        vector<int> ids; ids.reserve(best.size());
        for (auto& x : best) ids.push_back(x.second);
        return ids;
    }

    void print(ostream& os=std::cout) const { printNode(root, 0, os); }

private:
    Node* root;
    const int M; // max entries per node
    const int m; // min fill

    /* -------- Utilities -------- */

    static void destroy(Node* n){
        if (!n) return;
        if (!n->isLeaf) for (auto& e : n->entries) destroy(e.child);
        delete n;
    }

    static Rect computeNodeMBR(const Node* n) {
        Rect r; bool first=true;
        for (auto& e : n->entries) {
            if (first) { r = e.mbr; first=false; }
            else r.expand(e.mbr);
        }
        return r;
    }

    void adjustMBRsUpward(Node* n) {
        while (n && n->parent) {
            // update the entry in the parent that points to n
            for (auto& pe : n->parent->entries) {
                if (pe.child == n) {
                    pe.mbr = computeNodeMBR(n);
                    break;
                }
            }
            n = n->parent;
        }
    }

    /* -------- ChooseSubtree (least enlargement, then area, then fewest entries) -------- */
    Node* chooseSubtree(Node* cur, const Rect& r) const {
        while (!cur->isLeaf) {
            int best = -1;
            float bestEnl = numeric_limits<float>::infinity();
            float bestArea = numeric_limits<float>::infinity();
            int bestCount = numeric_limits<int>::max();

            for (int i=0;i<(int)cur->entries.size();++i) {
                const Rect& m = cur->entries[i].mbr;
                float enl = m.enlargement(r);
                float area = m.area();
                int   cnt = (int)cur->entries[i].child->entries.size();
                if (enl < bestEnl ||
                    (enl == bestEnl && area < bestArea) ||
                    (enl == bestEnl && area == bestArea && cnt < bestCount)) {
                    best = i; bestEnl = enl; bestArea = area; bestCount = cnt;
                }
            }
            cur = cur->entries[best].child;
        }
        return cur;
    }

    /* -------- Overflow handling (split + upward propagation) -------- */

    struct SplitPack {
        Node* n0;
        Node* n1;
        Rect  mbr0;
        Rect  mbr1;
    };

    void handleOverflow(Node* n) {
        SplitPack sp = quadraticSplit(n);

        if (n == root) {
            // new root
            Node* newRoot = new Node(false);
            newRoot->entries.emplace_back(sp.mbr0, sp.n0);
            newRoot->entries.emplace_back(sp.mbr1, sp.n1);
            sp.n0->parent = newRoot;
            sp.n1->parent = newRoot;
            root = newRoot;
            return;
        }

        // replace parent's child entry pointing to n with n0,
        // and add a new entry for n1. (We re-use n as n0 to avoid dangling pointers.)
        Node* parent = n->parent;

        for (auto& e : parent->entries) {
            if (e.child == n) {
                e.child = sp.n0;
                e.mbr   = sp.mbr0;
                break;
            }
        }
        parent->entries.emplace_back(sp.mbr1, sp.n1);
        sp.n0->parent = parent;
        sp.n1->parent = parent;

        if ((int)parent->entries.size() > M) handleOverflow(parent);
    }

    /* -------- Quadratic split (true wasted-area seeds, min-fill honored) -------- */

    // PickSeeds: maximize wasted area if placed together
    pair<int,int> pickSeedsQuadratic(const vector<Entry>& E) const {
        float worstWaste = -1.0f;
        pair<int,int> seeds{0,1};
        int N = (int)E.size();
        for (int i=0;i<N;i++){
            for (int j=i+1;j<N;j++){
                float waste = Rect::combine(E[i].mbr, E[j].mbr).area()
                              - E[i].mbr.area() - E[j].mbr.area();
                if (waste > worstWaste) { worstWaste = waste; seeds = {i,j}; }
            }
        }
        return seeds;
    }

    SplitPack quadraticSplit(Node* n) {
        // Gather entries to split
        vector<Entry> E = std::move(n->entries); // steal
        // Create two groups
        Node* g0 = new Node(n->isLeaf);
        Node* g1 = new Node(n->isLeaf);
        g0->parent = n->parent; // keep old parent
        g1->parent = n->parent;

        // pick seeds
        auto [s0, s1] = pickSeedsQuadratic(E);
        vector<char> taken(E.size(), 0);
        g0->entries.push_back(E[s0]); taken[s0]=1;
        g1->entries.push_back(E[s1]); taken[s1]=1;
        Rect cov0 = E[s0].mbr, cov1 = E[s1].mbr;

        int remaining = (int)E.size()-2;

        // Distribute remaining
        while (remaining > 0) {
            // enforce min-fill if remaining equals required for a group
            int freeCount = 0;
            for (size_t i=0;i<E.size();++i) if (!taken[i]) ++freeCount;

            if ( (int)g0->entries.size() + freeCount == m ) {
                for (size_t i=0;i<E.size();++i) if (!taken[i]) {
                    g0->entries.push_back(E[i]); cov0.expand(E[i].mbr); taken[i]=1; --remaining;
                }
                break;
            }
            if ( (int)g1->entries.size() + freeCount == m ) {
                for (size_t i=0;i<E.size();++i) if (!taken[i]) {
                    g1->entries.push_back(E[i]); cov1.expand(E[i].mbr); taken[i]=1; --remaining;
                }
                break;
            }

            // choose the entry with the greatest preference (area gain difference)
            float bestDiff = -1.0f;
            int bestIdx = -1;
            int bestGroup = -1;

            for (int i=0;i<(int)E.size();++i) if (!taken[i]) {
                float gain0 = cov0.enlargement(E[i].mbr);
                float gain1 = cov1.enlargement(E[i].mbr);
                float diff = fabs(gain0 - gain1);
                int g = (gain0 < gain1) ? 0 : (gain1 < gain0 ? 1 : -1);

                // tie-breaks when equal gain: pick smaller area, then fewer entries
                if (g == -1) {
                    float a0 = Rect::combine(cov0, E[i].mbr).area();
                    float a1 = Rect::combine(cov1, E[i].mbr).area();
                    if      (a0 < a1) g = 0;
                    else if (a1 < a0) g = 1;
                    else {
                        int c0 = (int)g0->entries.size();
                        int c1 = (int)g1->entries.size();
                        g = (c0 <= c1) ? 0 : 1;
                    }
                }

                if (diff > bestDiff) {
                    bestDiff = diff; bestIdx = i; bestGroup = g;
                }
            }

            if (bestIdx == -1) break; // shouldn't happen
            taken[bestIdx] = 1;
            if (bestGroup == 0) { g0->entries.push_back(E[bestIdx]); cov0.expand(E[bestIdx].mbr); }
            else                { g1->entries.push_back(E[bestIdx]); cov1.expand(E[bestIdx].mbr); }
            --remaining;
        }

        // Reuse n as group 0 to keep pointers stable; delete old children? (no—entries owned by node)
        n->entries = std::move(g0->entries);
        Node* n0 = n;
        Node* n1 = g1;

        // Set parent on internal children
        if (!n0->isLeaf) for (auto& e : n0->entries) e.child->parent = n0;
        if (!n1->isLeaf) for (auto& e : n1->entries) e.child->parent = n1;

        return { n0, n1, computeNodeMBR(n0), computeNodeMBR(n1) };
    }

    /* -------- Query -------- */
    static void windowQuery(const Node* n, const Rect& q, vector<int>& out) {
        if (n->isLeaf) {
            for (auto& e : n->entries) if (q.intersects(e.mbr)) out.push_back(e.id);
        } else {
            for (auto& e : n->entries) if (q.intersects(e.mbr)) windowQuery(e.child, q, out);
        }
    }

    /* -------- kNN helpers -------- */
    static void insertBest(vector<pair<float,int>>& best, int k, float d, int id, float& kth){
        // maintain max-heap semantics via vector + sort at end; here keep size <= k and update kth
        if ((int)best.size() < k) {
            best.emplace_back(d, id);
        } else if (d < best.back().first) {
            best.back() = {d,id};
        }
        // keep worst at back: partial sort
        sort(best.begin(), best.end()); // small k, fine
        if ((int)best.size() == k) kth = best.back().first;
    }

    /* -------- Debug print -------- */
    static void printNode(const Node* n, int depth, ostream& os){
        string ind(depth*2, ' ');
        if (n->isLeaf) {
            os << ind << "Leaf(" << n->entries.size() << "): ";
            for (auto& e : n->entries) {
                os << "[" << e.mbr.minX << "," << e.mbr.minY << " "
                   << e.mbr.maxX << "," << e.mbr.maxY << "]#" << e.id << " ";
            }
            os << "\n";
        } else {
            os << ind << "Internal(" << n->entries.size() << ")\n";
            for (auto& e : n->entries) {
                os << ind << "  MBR [" << e.mbr.minX << "," << e.mbr.minY
                   << " " << e.mbr.maxX << "," << e.mbr.maxY << "]\n";
                printNode(e.child, depth+1, os);
            }
        }
    }
};

/* ========================= Demo ========================= */
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    RTree rt(/*maxEntries*/ 6);

    vector<Point> pts = {
        {3,3},{2,5},{8,8},{7,1},{5,5},
        {6,3},{4,7},{1,4},{9,9},{10,10}
    };
    for (int i=0;i<(int)pts.size();++i) rt.insertPoint(pts[i], /*id*/ i);

    cout << "Tree:\n"; rt.print();

    // window queries
    vector<Rect> qs = {
        Rect(1.5,1.5,4.5,4.5),
        Rect(5.5,5.5,9.5,9.5),
        Rect(0.0,0.0,10.0,10.0)
    };
    for (auto& q : qs) {
        auto ans = rt.windowQuery(q);
        cout << "Query ["<<q.minX<<","<<q.minY<<" "<<q.maxX<<","<<q.maxY<<"] ->";
        for (int id: ans) cout<<" ["<<pts[id].x << ", " <<pts[id].y << "]";
        cout<<"\n";
    }

    // kNN
    Point q{9,8}; int k=3;
    auto knn = rt.kNearest(q,k);
    cout << k << "NN of ("<<q.x<<","<<q.y<<") :";
    for (int id: knn) cout<<" ["<<pts[id].x << ", " <<pts[id].y << "]";
    cout<<"\n";
    return 0;
}
