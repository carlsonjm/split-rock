cmake_minimum_required(VERSION 3.22)

project(split_rock VERSION 0.0.1 LANGUAGES CXX)

# Milestone 0 is a small test program. It needs only Qt; KDE's libraries arrive
# with the widget and the service in Milestone 1, except Layer Shell Qt, which
# lets the card be a surface of the desktop's own (src/desktop/Shell.h). Without
# it the card is an ordinary window, and cannot hold Spread's centre.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus Gui Network Quick Test)
find_package(LayerShellQt 6.7 QUIET)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)

# The protocol layer, the assistants and the conversation, without a window, so
# the tests can drive all of it.
add_library(splitrock_core STATIC
    src/acp/Protocol.cpp
    src/acp/Connection.cpp
    src/acp/Client.cpp
    src/app/Appearance.cpp
    src/app/Assistants.cpp
    src/app/AnswerText.cpp
    src/app/Change.cpp
    src/app/ChangeCard.cpp
    src/app/ChangeRunner.cpp
    src/app/Conversation.cpp
    src/app/Fence.cpp
    src/app/LocalModel.cpp
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
    src/guide/SkillShelf.cpp
    src/app/Product.cpp
)
target_include_directories(splitrock_core PUBLIC src)
target_link_libraries(splitrock_core PUBLIC Qt6::Core Qt6::Network)
target_compile_options(splitrock_core PRIVATE -Wall -Wextra)
target_compile_definitions(splitrock_core PUBLIC PROJECT_VERSION="${PROJECT_VERSION}"
    # Where Shuffle's own build of llama.cpp puts llama-server.
    SPLIT_ROCK_ENGINE_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/engine")

# Where the card stands, and Kadunce's companion guest protocol.
add_library(splitrock_desktop STATIC
    src/desktop/ConversationService.cpp
    src/desktop/Entry.h
    src/desktop/Kadunce.cpp
    src/desktop/Shell.cpp
)
target_link_libraries(splitrock_desktop PUBLIC splitrock_core Qt6::DBus Qt6::Gui Qt6::Quick)
target_compile_options(splitrock_desktop PRIVATE -Wall -Wextra)
if(LayerShellQt_FOUND)
    target_compile_definitions(splitrock_desktop PRIVATE SPLIT_ROCK_LAYER_SHELL)
    target_link_libraries(splitrock_desktop PRIVATE LayerShellQt::Interface)
else()
    message(STATUS "Layer Shell Qt not found: the card is an ordinary window and cannot hold Spread's centre")
endif()

qt_add_executable(split-rock src/main.cpp)
qt_add_resources(split-rock "sheet"
    PREFIX "/"
    FILES
        assets/mark.svg
        qml/Main.qml
        qml/Theme.qml
        qml/Icon.qml
        qml/Icons.js
        qml/Mark.qml
        qml/Pill.qml
        qml/Fold.qml
        qml/AnswerCard.qml
        qml/DoItCard.qml
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
        qml/UpkeepShelf.qml
        qml/SignInPanel.qml
        qml/AssistantList.qml
        qml/Workspace.qml
        qml/FullWindow.qml
        qml/ActionWindow.qml
        qml/StartingPanel.qml
        qml/DownloadPanel.qml
        qml/ProblemPanel.qml
        qml/RememberLine.qml
        qml/WheelScroll.qml
)
target_link_libraries(split-rock PRIVATE splitrock_core splitrock_desktop Qt6::Gui Qt6::Quick)

# The developer install (install.sh), for every account on this computer: the
# program, its desktop file, its icon, the session bus's service file, the
# guide and the tour, and the assistants' adapters when install.sh has fetched
# them. The pretend assistant is never installed. Milestone 3 replaces this
# with a package.
include(GNUInstallDirs)
set(SPLIT_ROCK_ADAPTERS_DIR "" CACHE PATH "Fetched assistant adapters to install beside the program")
set(SPLIT_ROCK_INSTALLED_ADAPTERS "${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/assistants")
target_compile_definitions(split-rock PRIVATE SPLIT_ROCK_INSTALLED_ADAPTERS="${SPLIT_ROCK_INSTALLED_ADAPTERS}")

set(SPLIT_ROCK_PROGRAM "${CMAKE_INSTALL_FULL_BINDIR}/split-rock")
configure_file(data/io.github.carlsonjm.SplitRock.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.SplitRock.desktop @ONLY)
configure_file(data/io.github.carlsonjm.splitrock.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.splitrock.service @ONLY)
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
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/data/io.github.carlsonjm.splitrock.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(DIRECTORY guide/plasma DESTINATION ${CMAKE_INSTALL_DATADIR}/split-rock/guide)
install(DIRECTORY tour/plasma DESTINATION ${CMAKE_INSTALL_DATADIR}/split-rock/tour)
install(DIRECTORY skills/plasma DESTINATION ${CMAKE_INSTALL_DATADIR}/split-rock/skills)
if(SPLIT_ROCK_ADAPTERS_DIR)
    install(DIRECTORY ${SPLIT_ROCK_ADAPTERS_DIR}/ DESTINATION ${CMAKE_INSTALL_LIBDIR}/split-rock/assistants
        USE_SOURCE_PERMISSIONS
        PATTERN ".npm-cache" EXCLUDE)
endif()

# Genie on this computer: the front desk in front of the model on this
# computer, an assistant that speaks the protocol like the others.
add_library(splitrock_desk STATIC
    src/desk/FrontDesk.cpp
    src/desk/Gate.cpp
    src/desk/Model.cpp
)
target_link_libraries(splitrock_desk PUBLIC splitrock_core Qt6::Network)
target_compile_options(splitrock_desk PRIVATE -Wall -Wextra)
qt_add_executable(split-rock-desk src/desk/main.cpp)
target_link_libraries(split-rock-desk PRIVATE splitrock_desk)
install(TARGETS split-rock-desk RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/split-rock)
# The models it may download, beside the program in a build and beside the
# front desk once installed.
configure_file(data/models.json ${CMAKE_CURRENT_BINARY_DIR}/models.json COPYONLY)
install(FILES data/models.json DESTINATION ${CMAKE_INSTALL_LIBDIR}/split-rock)
# The licence of the models models.json offers, which the person downloads.
install(FILES data/licences/Apache-2.0.txt DESTINATION ${CMAKE_INSTALL_LIBDIR}/split-rock/licences)
target_compile_definitions(split-rock PRIVATE
    SPLIT_ROCK_INSTALLED_DESK="${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/split-rock-desk"
    SPLIT_ROCK_INSTALLED_MODELS="${CMAKE_INSTALL_FULL_LIBDIR}/split-rock/models.json")

# Core, the front desk: a background program with no window that looks at the
# computer and tells the desktop's surfaces (docs/CORE.md). It needs only Qt's
# core and the bus, and none of the conversation or the model.
add_library(splitrock_core_watch STATIC
    src/core/Attention.cpp
    src/core/Core.cpp
    src/core/CoreService.cpp
    src/core/Storage.cpp
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

# The pretend assistant: speaks the protocol and follows a script, so every test
# runs with no account, no network and nothing on the computer to change.
qt_add_executable(split-rock-pretend-assistant src/pretend/main.cpp)
target_link_libraries(split-rock-pretend-assistant PRIVATE Qt6::Core)

# A package build leaves the tests out.
option(BUILD_TESTING "Build the tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
