#include <list>
#include <string>
#include <istream>
#include <variant>

// Tokens are split into two categories: atomic and list. Atomic tokens are
// values such as numbers and strings, and lists are compound tokens formed by
// an ordered set of other tokens. 

using token = std::variant<class token_list, class token_atomic>;

class token_atomic {
public:
    token_atomic(std::string &&value)
        : m_value{std::move(value)} {}
    
    const std::string &value() const { return m_value; }
private:
    std::string m_value;
};

class token_list {
private:
    // Children are stored as a linked list to avoid insertion invalidating the
    // parent pointers of previously inserted children. This could be changed to
    // a vector at the cost of having to update pointers if fragmentation is a
    // concern.
    using container_type = std::list<token>;
    
public:
    token_list()
        : m_parent{nullptr} {}
    
    using iterator = container_type::iterator;
    using const_iterator = container_type::const_iterator;
    
    // Create a child token (atomic or list specified by TokenT) with the given
    // parameters forwarded to the constructor.
    
    template <typename TokenT, typename... Args>
    TokenT &emplace_back(Args &&...args) {
        m_children.push_back(token{TokenT{std::forward<Args>(args)...}});
        if (auto *arg = std::get_if<token_list>(&m_children.back()))
            arg->m_parent = this;
        return std::get<TokenT>(m_children.back());
    }
        
    iterator begin() { return m_children.begin(); }
    const_iterator begin() const { return m_children.begin(); }
    const_iterator cbegin() const { return m_children.cbegin(); }
    iterator end() { return m_children.end(); }
    const_iterator end() const { return m_children.end(); }
    const_iterator cend() const { return m_children.cend(); }

    // While both types of token have a parent, only compound tokens track this,
    // since atomic tokens will almost always be obtained through thier parent
    // anyway.
    token_list *parent() const { return m_parent; }    
private:
    token_list *m_parent;
    container_type m_children;
};

// Construct a (possibly compound) Lisp object based on a character stream.
// Loosely based on the specification provided in chapter 22 of the Common Lisp
// (second edition) but with most of the flexibility removed in the interest of
// efficiency.

token_list read(std::istream &is)
{
    token_list root;
    token_list *parent = &root;
    
    std::string accum;
    
    auto ll = is.tellg();
    long ln = 1;
    while (is) {
        auto c = is.get();
        
        // Keep track of the current line number, as well as the position of the
        // last line for error reporting.
        if (c == '\n') {
            ++ln;
            ll = is.tellg();
        }
        
        if (std::isspace(c))
            ;
        else if (c == '(')
            parent = &parent->emplace_back<token_list>();
        else if (c == ')') {
            parent = parent->parent();
            if (!parent)
                throw std::runtime_error("Underflow!");
        }
        else {
            accum.push_back(c);
            continue;
        }
        
        // If we reach this point in the loop this means a terminator was
        // encountered so we can create a token with what was in the
        // accumulation buffer and start a new token.
        if (!accum.empty()) {
            parent->emplace_back<token_atomic>(std::move(accum));
            accum.clear();
        }
    }
    return root;
}

// Create a character stream representation of a Lisp object. Loosely based on
// the specification provided in chapter 22 of the Common Lisp (second edition)
// but with most of the flexibility removed in the interest of efficiency.

std::ostream &print(std::ostream &out, const token_list &root)
{   
    out << "(";
    for (const auto &child : root) {
        if (auto *arg = std::get_if<token_list>(&child))
            print(out, *arg);
        else if (auto *arg = std::get_if<token_atomic>(&child))
            out << " " << arg->value();
    }
    return out << ")";
}