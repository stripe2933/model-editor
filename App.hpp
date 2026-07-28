#pragma once

#include "pch.hpp"

class App {
public:
    explicit App();

    void loadAsset(std::filesystem::path path);
    void openAssetWithDialog();

    void run();

private:
    struct PImpl;
    struct PImplDeleter { static void operator()(PImpl*) noexcept; };

    std::unique_ptr<PImpl, PImplDeleter> pImpl;
};