#pragma once
#include "signature.hpp"

#include <asmjit/core/jitruntime.h>
#include <asmjit/x86/x86assembler.h>

using namespace asmjit::x86;

namespace utils::hook
{
    class assembler : public Assembler
    {
    public:
        using Assembler::Assembler;
        using Assembler::call;
        using Assembler::jmp;

        void pushad();
        void popad();

        void prepare_stack_for_call();
        void restore_stack_after_call();

        template <typename T>
        void call_aligned(T&& target)
        {
            this->prepare_stack_for_call();
            this->call(std::forward<T>(target));
            this->restore_stack_after_call();
        }

        asmjit::Error call(void* target);
        asmjit::Error jmp(void* target);
    };

    class detour
    {
    public:
        detour() = default;
        detour(void* place, void* target);
        detour(size_t place, void* target);
        ~detour();

        detour(detour&& other) noexcept
        {
            this->operator=(std::move(other));
        }

        detour& operator=(detour&& other) noexcept
        {
            if (this != &other)
            {
                this->~detour();

                this->place_ = other.place_;
                this->original_ = other.original_;

                other.place_ = nullptr;
                other.original_ = nullptr;
            }

            return *this;
        }

        detour(const detour&) = delete;
        detour& operator=(const detour&) = delete;

        void enable() const;
        void disable() const;

        void create(void* place, void* target);
        void create(size_t place, void* target);
        void clear();

        template <typename T>
        T* get() const
        {
            return static_cast<T*>(this->get_original());
        }

        template <typename T = void, typename... Args>
        T invoke(Args ... args)
        {
            return static_cast<T(*)(Args ...)>(this->get_original())(args...);
        }

        [[nodiscard]] void* get_original() const;

    private:
        void* place_{};
        void* original_{};
    };

    bool iat(const nt::library& library, const std::string& target_library, const std::string& process, void* stub);

    void nop(void* place, size_t length);
    void nop(size_t place, size_t length);

    void call(void* pointer, void* data);
    void call(size_t pointer, void* data);
    void call(size_t pointer, size_t data);

    void jump(void* pointer, void* data);
    void jump(size_t pointer, void* data);
    void jump(size_t pointer, size_t data);

    void* assemble(const std::function<void(assembler&)>& asm_function);

    void inject(void* pointer, const void* data);
    void inject(size_t pointer, const void* data);

    template <typename T>
    static void set(void* place, T value)
    {
        DWORD old_protect;
        VirtualProtect(place, sizeof(T), PAGE_EXECUTE_READWRITE, &old_protect);

        *static_cast<T*>(place) = value;

        VirtualProtect(place, sizeof(T), old_protect, &old_protect);
        FlushInstructionCache(GetCurrentProcess(), place, sizeof(T));
    }

    template <typename T>
    static void set(const size_t place, T value)
    {
        return set<T>(reinterpret_cast<void*>(place), value);
    }

    template <typename T, typename... Args>
    static T invoke(size_t func, Args ... args)
    {
        return reinterpret_cast<T(*)(Args ...)>(func)(args...);
    }

    template <typename T, typename... Args>
    static T invoke(void* func, Args ... args)
    {
        return static_cast<T(*)(Args ...)>(func)(args...);
    }
}