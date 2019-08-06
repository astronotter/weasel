#include <string>
#include <variant>
#include <vector>
#include <map>
#include <functional>
#include <iterator>
#include <istream>
#include <cstdint>
#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>

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

object read(std::istream &is)
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
    return root.front();
}

// Create a character stream representation of a Lisp object. Loosely based on
// the specification provided in chapter 22 of the Common Lisp (second edition)
// but with most of the flexibility removed in the interest of efficiency.

std::ostream &print(std::ostream &out, const object &obj)
{
    if (auto *li = std::get_if<list>(&obj)) {
        out << "(";
        for (const auto &obj : *li)
            print(out, obj) << " ";
        return out << ")";
    }
    else if (auto *at = std::get_if<atomic>(&obj)) {
        return out << *at;
    }
}

class compiled_fn {
public:
    compiled_fn(const uint8_t *data, size_t size) {
        const int pagesize = sysconf(_SC_PAGE_SIZE);
        if (pagesize == -1)
            throw std::runtime_error("Can't access page size.");
        
        int length = (size + pagesize-1) / pagesize * pagesize;
        if (posix_memalign((void **)&m_buffer, pagesize, length))
            throw std::runtime_error("Cannot create compiled_fn.");
        memcpy(m_buffer, data, size);
        mprotect(m_buffer, length, PROT_READ | PROT_EXEC);
    }
    
    uint64_t operator ()() {
        return ((uint64_t(*)(void))m_buffer)();
    }
private:
    char *m_buffer;
};

uint64_t op_add(uint64_t a, uint64_t b) { return a + b; }
uint64_t op_mul(uint64_t a, uint64_t b) { return a * b; }
uint64_t op_print(uint64_t a) { printf("%llu\n", a); return a; }

compiled_fn compile(const sexpr::list &root)
{
    struct frame {
        std::reference_wrapper<const sexpr::list> parent;
        sexpr::list::const_iterator it;
        sexpr::atomic op;
    };
    std::vector<struct frame> ctx;
    struct op {
        int nargs;
        uintptr_t addr;
    };
    std::map<std::string, op> ops = {
        { "+",     { 2, reinterpret_cast<uintptr_t>(op_add) } },
        { "*",     { 2, reinterpret_cast<uintptr_t>(op_mul) } },
        { "print", { 1, reinterpret_cast<uintptr_t>(op_print) } }
    };
    
    std::vector<uint8_t> buffer;
    int stackofs = 0;
    auto out = std::back_inserter(buffer);
    ctx.push_back(frame{root, root.begin()+1,
        *std::get_if<sexpr::atomic>(&root.front())});
    for (;;) {
        if (ctx.back().it == ctx.back().parent.get().end()) {
            auto op = ops[ctx.back().op];
            if (op.nargs > 0)
                *out++ = 0x5f; // pop rdi
            if (op.nargs > 1)
                *out++ = 0x5e; // pop rsi
            stackofs -= op.nargs;
            if (stackofs % 2 == 0) {
                *out++ = 0x48; // sub rsp,8
                *out++ = 0x83;
                *out++ = 0xec;
                *out++ = 0x08;
            }
            *out++ = 0x48; // mov rax,imm64
            *out++ = 0xb8;
            *out++ = op.addr & 0xff;
            *out++ = (op.addr >> 8) & 0xff;
            *out++ = (op.addr >> 16) & 0xff;
            *out++ = (op.addr >> 24) & 0xff;
            *out++ = (op.addr >> 32) & 0xff;
            *out++ = (op.addr >> 40) & 0xff;
            *out++ = (op.addr >> 48) & 0xff;
            *out++ = (op.addr >> 56) & 0xff;
            *out++ = 0xff;
            *out++ = 0xd0; // call rax
            if (stackofs % 2 == 0) {
                *out++ = 0x48; // add rsp,8
                *out++ = 0x83;
                *out++ = 0xc4;
                *out++ = 0x08;
            }
            *out++ = 0x50; // push rax
            stackofs++;
            ctx.pop_back();
            if (ctx.empty())
                break;
        }
        else if (auto *list = std::get_if<sexpr::list>(&*ctx.back().it)) {
            ctx.push_back(frame{*list, list->begin()+1,
                *std::get_if<sexpr::atomic>(&list->front())});
            continue;
        }
        else if (auto *atom = std::get_if<sexpr::atomic>(&*ctx.back().it)) {
            uint32_t imm32 = std::stoi(*atom);
            *out++ = 0x68; // push imm32
            *out++ = imm32 & 0xff;
            *out++ = (imm32 >> 8) & 0xff;
            *out++ = (imm32 >> 16) & 0xff;
            *out++ = (imm32 >> 24) & 0xff;
            stackofs++;
        }
        ctx.back().it++;
    }
    *out++ = 0x58; // pop rax
    *out++ = 0xc3; // ret
    
    return compiled_fn{buffer.data(), buffer.size()};
}

}; // sexpr
