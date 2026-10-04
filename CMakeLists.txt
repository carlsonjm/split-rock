cmake_minimum_required(VERSION 3.22)

project(split_rock VERSION 0.0.1 LANGUAGES CXX)

# Milestone 0 is a small test program. It needs only Qt; KDE's libraries arrive
# with the widget and the service in Milestone 1.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick Test)

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
    src/guide/GuideLibrary.cpp
)
target_include_directories(splitrock_core PUBLIC src)
target_link_libraries(splitrock_core PUBLIC Qt6::Core)
target_compile_options(splitrock_core PRIVATE -Wall -Wextra)
target_compile_definitions(splitrock_core PUBLIC PROJECT_VERSION="${PROJECT_VERSION}")

qt_add_executable(split-rock-m0 src/main.cpp)
qt_add_resources(split-rock-m0 "sheet"
    PREFIX "/"
    FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Icon.qml
        qml/Icons.js
        qml/JackMark.qml
        qml/Pill.qml
        qml/Fold.qml
        qml/AnswerCard.qml
        qml/DoItCard.qml
        qml/SignInPanel.qml
        qml/AssistantList.qml
)
target_link_libraries(split-rock-m0 PRIVATE splitrock_core Qt6::Gui Qt6::Quick)

# The pretend assistant: speaks the protocol and follows a script, so every test
# runs with no account, no network and nothing on the computer to change.
qt_add_executable(split-rock-pretend-assistant src/pretend/main.cpp)
target_link_libraries(split-rock-pretend-assistant PRIVATE Qt6::Core)

enable_testing()
add_subdirectory(tests)
