#pragma once

#include "pch.hpp"

class App {
public:
    explicit App();

    void run();

private:
    struct PImpl;
    struct PImplDeleter { static void operator()(PImpl*) noexcept; };

    std::unique_ptr<PImpl, PImplDeleter> pImpl;
};