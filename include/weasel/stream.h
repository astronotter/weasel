#include <string>
#include <variant>
#include <vector>
#include <functional>
#include <iostream>

#pragma once

namespace sexpr {

// Objects are split into two categories: atom and list. atoms are values
// such as numbers and strings, and lists are unordered sets of objects other
// objects.

using atom = std::string;
using object = std::variant<struct list, atom>;

struct list : std::vector<object> {
    atom op;
    list(atom &&op)
    : op{op} {}
};

object read(std::istream &is)
{
    list root{""};
    std::vector<std::reference_wrapper<list>> ctx = { root }; 
    
    std::string accum;
    auto tokenize = [&]() -> atom {
        atom res{std::move(accum)};
        accum.clear();
        return res;
    };
    
    auto ll = is.tellg();
    long ln = 1;
    while (is) {
        auto c = is.get();
        auto &top = ctx.back().get();
        
        if (c == '\n') {
            // Keep track of the current line number, as well as the position of
            // the last line for error reporting.
            
            ++ln;
            ll = is.tellg();
            
            auto token = tokenize();
            if (!token.empty())
                top.push_back(token);
        }
        else if (c == ',')
            top.push_back(tokenize());
        else if (c == '(') {
            auto &back = top.emplace_back(list{tokenize()});
            ctx.push_back(*std::get_if<list>(&back));
        }
        else if (c == ')') {
            auto token = tokenize();
            if (!token.empty())
                top.push_back(token);
            ctx.pop_back();  // May throw on underflow.
        }
        else
            accum.push_back(c);
    }
    return root.front();
}

std::ostream &print(std::ostream &out, const object &obj)
{
    if (auto *li = std::get_if<list>(&obj)) {
        out << li->op << "(";
        for (const auto &obj : *li)
            print(out, obj) << ",";
        return out << "\b)";
    }
    else if (auto *at = std::get_if<atom>(&obj)) {
        return out << *at;
    }
}

}; // sexpr
