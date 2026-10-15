#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <stdexcept>
#endif

class ExceptionObserver {
  public:
    ExceptionObserver() {
#ifdef _WIN32
        initial = count.load();
        handler = AddVectoredExceptionHandler(1, observe);
        if (!handler)
            throw std::runtime_error("Cannot observe first-chance exceptions");
#endif
    }
    ~ExceptionObserver() {
#ifdef _WIN32
        RemoveVectoredExceptionHandler(handler);
#endif
    }
    bool quiet() const {
#ifdef _WIN32
        return count.load() == initial;
#else
        return true;
#endif
    }
    ExceptionObserver(const ExceptionObserver &) = delete;
    ExceptionObserver &operator=(const ExceptionObserver &) = delete;

  private:
#ifdef _WIN32
    inline static std::atomic<unsigned> count{0};
    unsigned initial = 0;
    void *handler = nullptr;
    static LONG CALLBACK observe(EXCEPTION_POINTERS *event) {
        if (event->ExceptionRecord->ExceptionCode == 0xe06d7363)
            ++count;
        return EXCEPTION_CONTINUE_SEARCH;
    }
#endif
};
