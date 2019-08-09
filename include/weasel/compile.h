#include "stream.h"
#include <unistd.h>
#include <sys/mman.h>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <sstream>

#pragma once

namespace sexpr {

class native_function {
public:
    native_function(const std::string &buffer, std::vector<object> &&immediates)
    : m_buffer{nullptr}
    , m_immediates{immediates} {
        // Attempt to allocate a contiguous page-aligned region of memory for
        // the function.
        
        const int pagesize = sysconf(_SC_PAGE_SIZE);
        if (pagesize == -1)
            throw std::runtime_error("Can't access page size.");
        
        int length = (buffer.size() + pagesize-1) / pagesize * pagesize;
        if (posix_memalign((void **)&m_buffer, pagesize, length))
            throw std::runtime_error("Cannot create compiled_fn.");
        memcpy(m_buffer, buffer.data(), buffer.size());
        
        // Mark the region as executable. This is required on modern hardware as
        // the heap is restricted from execution by default to prevent code
        // injection attacks.
        
        mprotect(m_buffer, length, PROT_READ | PROT_EXEC);
    }
    
    const object &immediate(uint32_t idx) const {
        return m_immediates[idx];
    }
    
    void operator ()() {
        std::vector<object> stack;
        (*reinterpret_cast<void (*)(std::vector<object> *, native_function *)>(m_buffer))(&stack, this);
    }
private:
    std::vector<object> m_immediates;
    char *m_buffer;
};

namespace {
// Builtin functions.

static void op_add(std::vector<object> *stack)
{
    auto it = stack->end();
    auto a = std::stol(*std::get_if<atom>(&*--it));
    auto b = std::stol(*std::get_if<atom>(&*--it));
    stack->pop_back();
    stack->back() = atom{std::to_string(a + b)};
}

static void op_mul(std::vector<object> *stack)
{
    auto it = stack->end();
    auto a = std::stol(*std::get_if<atom>(&*--it));
    auto b = std::stol(*std::get_if<atom>(&*--it));
    stack->pop_back();
    stack->back() = atom{std::to_string(a * b)};
}

static void op_print(std::vector<object> *stack) {
    print(std::cout, stack->back()) << std::endl;
}

static const std::map<std::string, uintptr_t> builtins = {
    { "+",     reinterpret_cast<uintptr_t>(op_add) },
    { "*",     reinterpret_cast<uintptr_t>(op_mul) },
    { "print", reinterpret_cast<uintptr_t>(op_print) }
};

// The imm<> type aids in serializing unsigned integers to streams in the LSB
// format that x64 expects for immediates.

template <typename T> struct imm { T val; };
template <typename T>
std::ostream &operator <<(std::ostream &out, const imm<T> &imm) {
    for (int i = 0; i < sizeof(T) * 8; i += 8)
        out.put((imm.val >> i) & 0xff);
    return out;
}

// We need a proxy function here since method functions are not guarenteed to
// have machine addresses, which does not help us when calling from assembly!

static void do_push_imm(std::vector<object> *stack, native_function *fn, uint32_t idx){
    stack->push_back(fn->immediate(idx));
};
};

native_function compile(const list &root)
{   
    // The x64 instructions that we will need when building the function are
    // defined here:

    const char *call_rax      = "\xff\xd0";
    const char *push_rdi      = "\x57";
    const char *pop_rdi       = "\x5f";
    const char *push_rsi      = "\x56";
    const char *pop_rsi       = "\x5e";
    const char *mov_rax_imm64 = "\x48\xb8";
    const char *mov_rdx_imm32 = "\xba";
    const char *sub_rsp_8     = "\x48\x83\xec\x08";
    const char *add_rsp_8     = "\x48\x83\xc4\x08";
    const char *ret           = "\xc3";
    
    struct frame {
        std::reference_wrapper<const list> parent;
        list::const_iterator it;
    };
    std::vector<struct frame> frames = {{root, root.begin()}};
    
    std::vector<object> immediates;
    std::ostringstream out;
    out << push_rsi;

    for (;;) {
        if (frames.back().it == frames.back().parent.get().end()) {
            auto op = builtins.find(frames.back().parent.get().op);
            if (op == builtins.end())
                throw std::runtime_error("compile: Unknown function.");
            
            out << push_rdi;
            out << push_rsi;
            out << mov_rax_imm64 << imm<uint64_t>{op->second};
            out << call_rax;
            out << pop_rsi;
            out << pop_rdi;

            frames.pop_back();
            if (frames.empty())
                break;
            frames.back().it++;
            continue;
        }
        
        auto *list = std::get_if<sexpr::list>(&*frames.back().it);
        if (list && !list->op.empty()) {
            frames.push_back(frame{*list, list->begin()});
            continue;
        }
        
        if (immediates.size() == std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("compile: Too many immediates.");
        immediates.push_back(*frames.back().it);
        out << push_rdi;
        out << push_rsi;
        out << mov_rdx_imm32 << imm<uint32_t>{(uint32_t)immediates.size()-1};
        out << mov_rax_imm64 << imm<uint64_t>{reinterpret_cast<uintptr_t>(&do_push_imm)};
        out << call_rax;
        out << pop_rsi;
        out << pop_rdi;
        frames.back().it++;
    }
    out << pop_rsi << ret;
    out.flush();
    
    return native_function{out.str(), std::move(immediates)};
}

};