#include <iostream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

const std::vector<std::string> VARIABLES = {"p", "q", "r", "x", "y", "z", "a", "b", "c"};

// --- Runtime Execution Engine ---
struct Term;
using Func = std::function<Term(const Term&)>;

struct Term {
    std::shared_ptr<Func> f;
    std::string name;
    bool is_abstract;

    Term(Func func, std::string n, bool abs = false) 
        : f(std::make_shared<Func>(func)), name(n), is_abstract(abs) {}
    Term(std::string n) : f(nullptr), name(n), is_abstract(false) {}
    Term() : f(nullptr), name("?"), is_abstract(false) {}

    Term operator()(const Term& x) const {
        if (!f || !(*f)) return Term("(" + this->name + " " + x.name + ")");
        Term result = (*f)(x);
        if (result.name == "f_sub") result.name = "(" + this->name + " " + x.name + ")";
        if (this->is_abstract && result.f) result.is_abstract = true;
        return result;
    }

    std::string toLambda(size_t idx = 0) const {
        if (idx >= VARIABLES.size() || !is_abstract || !f) return name;
        Term v(VARIABLES[idx]);
        Term applied = (*this)(v);
        if (applied.f && applied.is_abstract) return "λ" + VARIABLES[idx] + "." + applied.toLambda(idx + 1);
        return "λ" + VARIABLES[idx] + "." + applied.name;
    }
};

auto I_term = Term([](Term x) { return x; }, "I", true);
auto K_term = Term([](Term x) { return Term([x](Term y) { return x; }, "f_sub", true); }, "K", true);
auto S_term = Term([](Term x) {
    return Term([x](Term y) {
        return Term([x, y](Term z) { return x(z)(y(z)); }, "f_sub", true);
    }, "f_sub", true);
}, "S", true);

// --- AST Structure ---
enum class NodeType { VAR, APP, ABS, COMB };
struct Node {
    NodeType type;
    std::string var; 
    char comb;       
    std::shared_ptr<Node> l, r;

    Node(NodeType t, std::string v, char c, std::shared_ptr<Node> left, std::shared_ptr<Node> right)
        : type(t), var(v), comb(c), l(left), r(right) {}
};

using NodePtr = std::shared_ptr<Node>;
NodePtr nVar(std::string v) { return std::make_shared<Node>(NodeType::VAR, v, ' ', nullptr, nullptr); }
NodePtr nApp(NodePtr l, NodePtr r) { return std::make_shared<Node>(NodeType::APP, "", ' ', l, r); }
NodePtr nAbs(std::string v, NodePtr b) { return std::make_shared<Node>(NodeType::ABS, v, ' ', nullptr, b); }
NodePtr nComb(char c) { return std::make_shared<Node>(NodeType::COMB, std::string(1, c), c, nullptr, nullptr); }

bool hasVar(NodePtr n, const std::string& v) {
    if (!n) return false;
    if (n->type == NodeType::VAR) return n->var == v;
    if (n->type == NodeType::APP) return hasVar(n->l, v) || hasVar(n->r, v);
    if (n->type == NodeType::ABS) return n->var != v && hasVar(n->r, v);
    return false;
}

// --- Bracket Abstraction Logic ---
NodePtr abstract(const std::string& x, NodePtr E) {
    // 1. [x]x => I
    if (E->type == NodeType::VAR && E->var == x) return nComb('I');

    // 2. [x]c => K c (if x is not free in c)
    if (!hasVar(E, x)) return nApp(nComb('K'), E);

    // 3. [x](E1 E2) => S ([x]E1) ([x]E2)
    if (E->type == NodeType::APP) {
        auto e1 = abstract(x, E->l);
        auto e2 = abstract(x, E->r);

        // Optimization: S (K E1) (K E2) => K (E1 E2)
        if (e1->type == NodeType::APP && e1->l->type == NodeType::COMB && e1->l->comb == 'K' &&
            e2->type == NodeType::APP && e2->l->type == NodeType::COMB && e2->l->comb == 'K') {
            return nApp(nComb('K'), nApp(e1->r, e2->r));
        }
        // Optimization: S (K E1) I => E1
        if (e1->type == NodeType::APP && e1->l->type == NodeType::COMB && e1->l->comb == 'K' &&
            e2->type == NodeType::COMB && e2->comb == 'I') {
            return e1->r;
        }
        return nApp(nApp(nComb('S'), e1), e2);
    }

    // 4. [x]([y]E) => recurse
    if (E->type == NodeType::ABS) {
        // This case shouldn't be reached if transform is called from inside-out
        return abstract(x, abstract(E->var, E->r));
    }

    return nullptr;
}

NodePtr transform(NodePtr n) {
    if (!n) return nullptr;
    if (n->type == NodeType::VAR || n->type == NodeType::COMB) return n;
    if (n->type == NodeType::APP) return nApp(transform(n->l), transform(n->r));
    if (n->type == NodeType::ABS) {
        // Essential: Transform the body first to remove any inner lambdas
        return abstract(n->var, transform(n->r));
    }
    return n;
}

Term finalize(NodePtr n) {
    if (!n) return Term("?");
    if (n->type == NodeType::COMB) {
        if (n->comb == 'S') return S_term;
        if (n->comb == 'K') return K_term;
        if (n->comb == 'I') return I_term;
    }
    if (n->type == NodeType::VAR) return Term(n->var);
    if (n->type == NodeType::APP) return finalize(n->l)(finalize(n->r));
    return Term("?");
}

class Parser {
    std::string s;
    size_t p = 0;

    void skip() { 
        while (p < s.size() && (isspace(s[p]))) p++; 
    }

    std::string readVar() {
        skip();
        std::string res;
        while (p < s.size() && isalnum(s[p])) {
            res += s[p++];
        }
        return res;
    }

    // parseAtom: Handles the smallest units: Variables, Parentheses, or a full Lambda
    NodePtr parseAtom() {
        skip();
        if (p >= s.size()) return nullptr;

        if (s[p] == '\\' || s[p] == '@') {
            p++; // skip \ or @
            std::string v = readVar();
            skip();
            if (p < s.size() && s[p] == '.') p++; // skip .
            // A Lambda abstraction extends as far to the right as possible
            return nAbs(v, parseExpr()); 
        }

        if (s[p] == '(') {
            p++; // skip (
            NodePtr res = parseExpr();
            skip();
            if (p < s.size() && s[p] == ')') p++; // skip )
            return res;
        }

        std::string name = readVar();
        if (name.empty()) return nullptr;
        return nVar(name);
    }

public:
    Parser(std::string input) : s(input) {}

    // parseExpr: Handles left-associative application: f x y z -> ((f x) y) z
    NodePtr parseExpr() {
        NodePtr res = parseAtom();
        if (!res) return nullptr;

        while (true) {
            skip();
            // Stop if we hit the end, a closing paren, or a dot (though dot is handled in Atom)
            if (p >= s.size() || s[p] == ')') break;

            // If the next character is the start of a NEW atom, it's an application
            // We need to peek to see if a new atom (var, paren, or lambda) starts here
            size_t saved_p = p;
            NodePtr next = parseAtom();
            if (!next) {
                p = saved_p;
                break;
            }
            res = nApp(res, next);
        }
        return res;
    }
};

int main() {
    std::string input;
    std::cout << "Enter Lambda Expression: ";
    if (!std::getline(std::cin, input) || input.empty()) return 0;

    Parser parser(input);
    NodePtr raw_ast = parser.parseExpr();
    
    // Process the AST into SKI form
    NodePtr ski_ast = transform(raw_ast);
    Term compiled = finalize(ski_ast);
    
    std::cout << "--- Compilation Result ---" << std::endl;
    std::cout << "SKI Form:    " << compiled.name << std::endl;
    std::cout << "Lambda Form: " << compiled.toLambda() << std::endl;

    return 0;
}