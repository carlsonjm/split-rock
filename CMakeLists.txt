cmake_minimum_required(VERSION 3.22)

project(split_rock VERSION 0.0.1 LANGUAGES CXX)

# Milestone 0 is a small test program. It needs only Qt; KDE's libraries arrive
# with the widget and the service in Milestone 1, except Layer Shell Qt, which
# lets the card be a surface of the desktop's own (src/desktop/Shell.h). Without
# it the card is an ordinary window, and cannot hold Spread's centre.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus Gui Quick Test)
find_package(LayerShellQt 6.7 QUIET)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# The protocol layer, the assistants and the conversation, without a window, so
# the tests can drive all of it.
add_library(splitrock_core STATIC
    src/acp/Protocol.cpp
    src/acp/Connection.cpp
    src/acp/Client.cpp
    src/app/Assistants.cpp
    src/app/AnswerText.cpp
    src/app/ChangeCard.cpp
    src/app/Conversation.cpp
    src/app/Product.cpp
)
target_include_directories(splitrock_core PUBLIC src)
target_link_libraries(splitrock_core PUBLIC Qt6::Core)
target_compile_options(splitrock_core PRIVATE -Wall -Wextra)
target_compile_definitions(splitrock_core PUBLIC PROJECT_VERSION="${PROJECT_VERSION}")

# Where the card stands, and Kadunce's companion guest protocol.
add_library(splitrock_desktop STATIC
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

qt_add_executable(split-rock-m0 src/main.cpp)
qt_add_resources(split-rock-m0 "sheet"
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
        qml/SignInPanel.qml
        qml/AssistantList.qml
        qml/Workspace.qml
        qml/FullWindow.qml
        qml/StartingPanel.qml
        qml/ProblemPanel.qml
)
target_link_libraries(split-rock-m0 PRIVATE splitrock_core splitrock_desktop Qt6::Gui Qt6::Quick)

# The pretend assistant: speaks the protocol and follows a script, so every test
# runs with no account, no network and nothing on the computer to change.
qt_add_executable(split-rock-pretend-assistant src/pretend/main.cpp)
target_link_libraries(split-rock-pretend-assistant PRIVATE Qt6::Core)

enable_testing()
add_subdirectory(tests)
