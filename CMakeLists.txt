cmake_minimum_required(VERSION 3.22)

project(split_rock VERSION 1.1.0 LANGUAGES CXX)

# Split Rock needs only Qt.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus Gui Quick Test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)

# The Helpers and what they write in, without a window, so the tests can drive
# all of it.
add_library(splitrock_core STATIC
    src/app/Admin.cpp
    src/app/Appearance.cpp
    src/app/AnswerText.cpp
    src/app/Change.cpp
    src/app/ChangeRunner.cpp
    src/app/Helpers.cpp
    src/app/NeverWritten.cpp
    src/app/Notebook.cpp
    src/app/SystemUpdate.cpp
    src/app/Chore.cpp
    src/app/SpaceCleanup.cpp
    src/app/AppInstall.cpp
    src/app/RestorePoints.cpp
    src/app/RecentChanges.cpp
    src/app/HealthCheck.cpp
    src/app/CommunityApps.cpp
    src/guide/GuideLibrary.cpp
    src/app/Product.cpp
)
target_include_directories(splitrock_core PUBLIC src)
target_link_libraries(splitrock_core PUBLIC Qt6::Core)
target_compile_options(splitrock_core PRIVATE -Wall -Wextra)
target_compile_definitions(splitrock_core PUBLIC PROJECT_VERSION="${PROJECT_VERSION}"
    # The only program Split Rock runs as the administrator.
    SPLIT_ROCK_ADMIN_HELPER="${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/split-rock-admin")
install(PROGRAMS src/admin/split-rock-admin DESTINATION ${CMAKE_INSTALL_LIBDIR}/split-rock)

qt_add_executable(split-rock src/main.cpp src/desktop/Entry.h)
qt_add_resources(split-rock "sheet"
    PREFIX "/"
    FILES
        assets/mark.svg
        qml/Theme.qml
        qml/Icon.qml
        qml/Icons.js
        qml/Pill.qml
        qml/Fold.qml
        qml/UpdateCard.qml
        qml/CardFrame.qml
        qml/CardHead.qml
        qml/CardNote.qml
        qml/CardFact.qml
        qml/CardProgress.qml
        qml/CardRow.qml
        qml/SpaceCard.qml
        qml/InstallCard.qml
        qml/RestoreCard.qml
        qml/HealthCard.qml
        qml/ChangesCard.qml
        qml/CommunityCard.qml
        qml/ActionWindow.qml
        qml/WheelScroll.qml
)
target_link_libraries(split-rock PRIVATE splitrock_core Qt6::DBus Qt6::Gui Qt6::Quick)

# The developer install (install.sh), for every account on this computer: the
# program, its desktop file, its icon, and the guide and the tour. Milestone 3
# replaces this with a package.
set(SPLIT_ROCK_PROGRAM "${CMAKE_INSTALL_FULL_BINDIR}/split-rock")
configure_file(data/io.github.carlsonjm.SplitRock.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.SplitRock.desktop @ONLY)
# The icon is the mark on its tile, drawn from the mark's one file.
file(READ assets/mark.svg SPLIT_ROCK_MARK)
string(REGEX MATCHALL "<path[^>]*/>" SPLIT_ROCK_MARK_PATHS "${SPLIT_ROCK_MARK}")
# The tile sets the mark's ink, so the paths' own paint is dropped.
string(REGEX REPLACE " (fill|stroke|fill-rule)=\"[^\"]*\"" "" SPLIT_ROCK_MARK_PATHS "${SPLIT_ROCK_MARK_PATHS}")
string(JOIN "\n    " SPLIT_ROCK_MARK_PATHS ${SPLIT_ROCK_MARK_PATHS})
configure_file(data/io.github.carlsonjm.SplitRock.svg.in
    ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.SplitRock.svg @ONLY)

install(TARGETS split-rock RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.SplitRock.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.SplitRock.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(DIRECTORY guide/plasma DESTINATION ${CMAKE_INSTALL_DATADIR}/split-rock/guide)
install(DIRECTORY tour/plasma DESTINATION ${CMAKE_INSTALL_DATADIR}/split-rock/tour)

# Core, the front desk: a background program with no window that looks at the
# computer and tells the desktop's surfaces (docs/CORE.md). It needs only Qt's
# core and the bus.
add_library(splitrock_core_watch STATIC
    src/core/Attention.cpp
    src/core/Core.cpp
    src/core/CoreService.cpp
    src/core/Health.cpp
    src/core/Storage.cpp
    src/core/Updates.cpp
    src/core/Windows.cpp
)
target_include_directories(splitrock_core_watch PUBLIC src)
target_link_libraries(splitrock_core_watch PUBLIC Qt6::Core Qt6::DBus)
target_compile_options(splitrock_core_watch PRIVATE -Wall -Wextra)
qt_add_executable(split-rock-core src/core/main.cpp)
target_link_libraries(split-rock-core PRIVATE splitrock_core_watch)
target_compile_definitions(split-rock-core PRIVATE PROJECT_VERSION="${PROJECT_VERSION}"
    SPLIT_ROCK_INSTALLED_PROGRAM="${SPLIT_ROCK_PROGRAM}")
install(TARGETS split-rock-core RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/split-rock)
set(SPLIT_ROCK_CORE_PROGRAM "${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/split-rock-core")
configure_file(data/io.github.carlsonjm.splitrock.Core.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.splitrock.Core.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.splitrock.Core.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
# Where systemd runs the person's services, Core runs as one tied to the
# signed-in desktop, however it was started, so signing out stops it.
configure_file(data/split-rock-core.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/data/split-rock-core.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/data/split-rock-core.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
# Core starts at sign-in, so it looks on its schedule whether or not anything
# has asked it yet.
configure_file(data/io.github.carlsonjm.splitrock.Core-autostart.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.splitrock.Core-autostart.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.splitrock.Core-autostart.desktop
    DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart RENAME io.github.carlsonjm.splitrock.Core.desktop)

# A package build leaves the tests out.
option(BUILD_TESTING "Build the tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
