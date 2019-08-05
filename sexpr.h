#include <string>
#include <istream>
#include <variant>
#include <vector>
#include <functional>

#pragma once

namespace sexpr {

// Objects are split into two categories: atomic and list. Atomics are values
// such as numbers and strings, and lists are unordered sets of objects other
// objects.

using atomic = std::string;
using object = std::variant<class list, atomic>;

// List is simply a proxy class for a vector of objects. This is necessary since
// type aliases cannot refer to themselves.

// One caveat to note is that this should never be destructed through a pointer
// to vector, since vector does not have a virtual destructor! Since this seems
// a relatively unlikely scenario the benefit of concision has been deemed to
// outweighs the risk.

class list : public std::vector<object> {};

// Construct a (possibly compound) Lisp object based on a character stream.
// Loosely based on the specification provided in chapter 22 of the Common Lisp
// (second edition) but with most of the flexibility removed in the interest of
// efficiency.

list read(std::istream &is)
{
    list root;
    std::vector<std::reference_wrapper<list>> ctx = { root }; 
    
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
        
        auto &top = ctx.back().get();
        if (std::isspace(c))
            ;
        else if (c == '(') {
            auto &back = top.emplace_back(list{});
            ctx.push_back(*std::get_if<list>(&back));
        }
        else if (c == ')')
            ctx.pop_back();  // May throw on underflow.
        else {
            accum.push_back(c);
            continue;
        }
        
        // If we reach this point in the loop this means a terminator was
        // encountered so we can create a token with what was in the
        // accumulation buffer and start a new token.
        if (!accum.empty()) {
            top.push_back(std::move(accum));
            accum.clear();
        }
    }
    return root;
}

// Create a character stream representation of a Lisp object. Loosely based on
// the specification provided in chapter 22 of the Common Lisp (second edition)
// but with most of the flexibility removed in the interest of efficiency.

std::ostream &print(std::ostream &out, const list &root)
{   
    out << "(";
    for (const auto &child : root) {
        if (auto *arg = std::get_if<list>(&child))
            print(out, *arg);
        else if (auto *arg = std::get_if<atomic>(&child))
            out << *arg << " ";
    }
    return out << ")";
}

}; // sexpr
