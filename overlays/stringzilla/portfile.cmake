# Header-only library
set(VCPKG_BUILD_TYPE release)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ashvardanian/StringZilla
    REF "v${VERSION}"
    SHA512 bd55178fcd5fa61f93328a1f9b3eeedd71afa53aa0e8366aafa7355bb1365518e14616f213637d1af967c7aed41a363468f4023af61faf21a60b24b9cddddc3e
    HEAD_REF master
)

file(COPY "${SOURCE_PATH}/include" DESTINATION "${CURRENT_PACKAGES_DIR}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
