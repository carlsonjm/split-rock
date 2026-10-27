cmake_minimum_required(VERSION 3.22)

project(split_rock VERSION 1.1.0 LANGUAGES CXX)

# Split Rock needs only Qt.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus Gui Network Quick Test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)

# The Helpers and what they write in, without a window, so the tests can drive
# all of it.
# The copies Core keeps of the desktop's settings, and what changed in them,
# which What changed reads (src/settings/SettingsHistory.h).
add_library(splitrock_settings STATIC src/settings/SettingsHistory.cpp)
target_include_directories(splitrock_settings PUBLIC src)
target_link_libraries(splitrock_settings PUBLIC Qt6::Core)
target_compile_options(splitrock_settings PRIVATE -Wall -Wextra)

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
    src/app/TidyUp.cpp
    src/app/AppInstall.cpp
    src/app/RestorePoints.cpp
    src/app/RecentChanges.cpp
    src/app/SettingRows.cpp
    src/app/HealthCheck.cpp
    src/app/CommunityApps.cpp
    src/guide/GuideLibrary.cpp
    src/app/Product.cpp
)
target_include_directories(splitrock_core PUBLIC src)
target_link_libraries(splitrock_core PUBLIC Qt6::Core splitrock_settings)
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
        qml/TidyCard.qml
        qml/InstallCard.qml
        qml/RestoreCard.qml
        qml/HealthCard.qml
        qml/ChangesCard.qml
        qml/CommunityCard.qml
        qml/ActionWindow.qml
        qml/CodexWindow.qml
        qml/WheelScroll.qml
)
target_link_libraries(split-rock PRIVATE splitrock_core splitrock_codex Qt6::DBus Qt6::Gui Qt6::Quick)

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
    src/core/Devices.cpp
    src/core/Health.cpp
    src/core/Storage.cpp
    src/core/Updates.cpp
    src/core/Windows.cpp
)
target_include_directories(splitrock_core_watch PUBLIC src)
target_link_libraries(splitrock_core_watch PUBLIC Qt6::Core Qt6::DBus splitrock_settings)
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

# Ask on this computer (docs/ASK.md): the front desk in front of the model on
# this computer, a program of its own beside Core, which never calls a model.
add_library(splitrock_ask STATIC
    src/ask/Ask.cpp
    src/ask/AskService.cpp
    src/ask/Engine.cpp
    src/ask/LocalModel.cpp
    src/desk/FrontDesk.cpp
    src/desk/Gate.cpp
    src/desk/Model.cpp
    src/guide/SkillShelf.cpp
)
target_include_directories(splitrock_ask PUBLIC src)
target_link_libraries(splitrock_ask PUBLIC splitrock_core splitrock_codex Qt6::Core Qt6::DBus Qt6::Network)
target_compile_options(splitrock_ask PRIVATE -Wall -Wextra)
qt_add_executable(split-rock-ask src/ask/main.cpp)
target_link_libraries(split-rock-ask PRIVATE splitrock_ask)
target_compile_definitions(split-rock-ask PRIVATE
    # Where Shuffle's own build of llama.cpp puts llama-server.
    SPLIT_ROCK_ENGINE_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/engine"
    SPLIT_ROCK_INSTALLED_MODELS="${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/models.json"
    # The program that opens Ask Codex.
    SPLIT_ROCK_INSTALLED_PROGRAM="${CMAKE_INSTALL_FULL_BINDIR}/split-rock")
install(TARGETS split-rock-ask RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/split-rock)
# The models it may download, beside the program in a build and installed
# beside it otherwise, with the licence of the model the person downloads.
configure_file(data/models.json ${CMAKE_CURRENT_BINARY_DIR}/models.json COPYONLY)
install(FILES data/models.json DESTINATION ${CMAKE_INSTALL_LIBDIR}/split-rock)
install(FILES data/licences/Apache-2.0.txt DESTINATION ${CMAKE_INSTALL_LIBDIR}/split-rock/licences)
install(DIRECTORY skills/plasma DESTINATION ${CMAKE_INSTALL_DATADIR}/split-rock/skills USE_SOURCE_PERMISSIONS)
set(SPLIT_ROCK_ASK_PROGRAM "${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/split-rock-ask")
configure_file(data/io.github.carlsonjm.splitrock.Ask.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.splitrock.Ask.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.splitrock.Ask.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)

# The broker (docs/BROKER.md): the models, for the Good Input apps that ask, a
# program of its own beside Core and Ask.
add_library(splitrock_broker STATIC
    src/broker/Broker.cpp
    src/broker/BrokerService.cpp
    src/broker/CodexTask.cpp
)
target_include_directories(splitrock_broker PUBLIC src)
target_link_libraries(splitrock_broker PUBLIC splitrock_ask splitrock_codex Qt6::Core Qt6::DBus Qt6::Network)
target_compile_options(splitrock_broker PRIVATE -Wall -Wextra)
qt_add_executable(split-rock-broker src/broker/main.cpp)
target_link_libraries(split-rock-broker PRIVATE splitrock_broker)
target_compile_definitions(split-rock-broker PRIVATE
    SPLIT_ROCK_ENGINE_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/engine"
    SPLIT_ROCK_INSTALLED_MODELS="${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/models.json")
install(TARGETS split-rock-broker RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/split-rock)
set(SPLIT_ROCK_BROKER_PROGRAM "${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/split-rock-broker")
configure_file(data/io.github.carlsonjm.splitrock.Broker.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.splitrock.Broker.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.splitrock.Broker.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)

# Codex, for the bigger questions (docs/ASK.md, Codex): its Agent Client
# Protocol client, and the conversation in its own window, in the fence Ask's
# engine runs in.
add_library(splitrock_codex STATIC
    src/acp/Client.cpp
    src/acp/Connection.cpp
    src/acp/Protocol.cpp
    src/ask/Fence.cpp
    src/codex/Agent.cpp
    src/codex/Chat.cpp
)
target_include_directories(splitrock_codex PUBLIC src)
target_link_libraries(splitrock_codex PUBLIC splitrock_core Qt6::Core)
target_compile_options(splitrock_codex PRIVATE -Wall -Wextra)
target_compile_definitions(splitrock_codex PUBLIC
    # Where Shuffle's own copy of Codex's adapter is installed.
    SPLIT_ROCK_CODEX_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/codex")

# A package build leaves the tests out.
option(BUILD_TESTING "Build the tests" ON)
option(SPLIT_ROCK_LIVE_TESTS "Build the checks that change the signed-in person's settings, run by hand" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
