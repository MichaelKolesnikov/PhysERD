#pragma once
#include <memory>

#define PIMPL \
private: \
    struct Impl; \
    friend struct Impl; \
    std::unique_ptr<Impl> pimpl;
