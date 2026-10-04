# Every test runs against the pretend assistant only, with a home folder of its
# own inside the build, so no test can reach a real person's settings, files or
# session, or an account or the network.
set(TEST_HOME "${CMAKE_CURRENT_BINARY_DIR}/home")
file(MAKE_DIRECTORY "${TEST_HOME}/runtime")
file(CHMOD "${TEST_HOME}/runtime" DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
set(TEST_ENVIRONMENT
    "HOME=${TEST_HOME}"
    "XDG_CONFIG_HOME=${TEST_HOME}/.config"
    "XDG_DATA_HOME=${TEST_HOME}/.local/share"
    "XDG_STATE_HOME=${TEST_HOME}/.local/state"
    "XDG_CACHE_HOME=${TEST_HOME}/.cache"
    "XDG_RUNTIME_DIR=${TEST_HOME}/runtime"
    # One named 1280 by 800 screen, as a tablet's, so Kadunce's replies can
    # name it.
    "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/offscreen-screen.json"
    "DBUS_SESSION_BUS_ADDRESS=disabled:"
    "SPLIT_ROCK_TEST_HOME=${TEST_HOME}"
)

function(split_rock_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE splitrock_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        PRETEND_PROGRAM="$<TARGET_FILE:split-rock-pretend-assistant>"
        SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/pretend-scripts"
        FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        GUIDE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/guide-fixtures"
        PROJECT_DIR="${PROJECT_SOURCE_DIR}")
    add_dependencies(${name} split-rock-pretend-assistant)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${TEST_ENVIRONMENT}" TIMEOUT 120)
endfunction()

split_rock_test(test_answer_text)
split_rock_test(test_change_card)
split_rock_test(test_assistants)
split_rock_test(test_protocol)
split_rock_test(test_conversation)
split_rock_test(test_notebook)
split_rock_test(test_guide)

split_rock_test(test_kadunce)
target_link_libraries(test_kadunce PRIVATE splitrock_desktop)

split_rock_test(test_sheet FakeKadunce.h)
target_link_libraries(test_sheet PRIVATE splitrock_desktop Qt6::Gui Qt6::Quick)
qt_add_resources(test_sheet "sheet_for_test"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES
        ${PROJECT_SOURCE_DIR}/qml/Main.qml
        ${PROJECT_SOURCE_DIR}/qml/Theme.qml
        ${PROJECT_SOURCE_DIR}/qml/Icon.qml
        ${PROJECT_SOURCE_DIR}/qml/Icons.js
        ${PROJECT_SOURCE_DIR}/qml/Mark.qml
        ${PROJECT_SOURCE_DIR}/assets/mark.svg
        ${PROJECT_SOURCE_DIR}/qml/Pill.qml
        ${PROJECT_SOURCE_DIR}/qml/Fold.qml
        ${PROJECT_SOURCE_DIR}/qml/AnswerCard.qml
        ${PROJECT_SOURCE_DIR}/qml/DoItCard.qml
        ${PROJECT_SOURCE_DIR}/qml/SignInPanel.qml
        ${PROJECT_SOURCE_DIR}/qml/AssistantList.qml
        ${PROJECT_SOURCE_DIR}/qml/Workspace.qml
        ${PROJECT_SOURCE_DIR}/qml/FullWindow.qml
        ${PROJECT_SOURCE_DIR}/qml/StartingPanel.qml
        ${PROJECT_SOURCE_DIR}/qml/ProblemPanel.qml
        ${PROJECT_SOURCE_DIR}/qml/RememberLine.qml
)

# The conversation's bus interface, driven by a client that loads none of Split
# Rock's code, against the real program and the pretend assistant, on a bus of
# the test's own and in a home of its own.
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_executable(test_conversation_bus bus/test_conversation_bus.cpp)
target_link_libraries(test_conversation_bus PRIVATE Qt6::DBus Qt6::Test)
target_compile_definitions(test_conversation_bus PRIVATE
    SPLIT_ROCK_PROGRAM="$<TARGET_FILE:split-rock>"
    SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/pretend-scripts")
add_dependencies(test_conversation_bus split-rock split-rock-pretend-assistant)
add_test(NAME test_conversation_bus COMMAND ${DBUS_RUN_SESSION}
    --config-file=${CMAKE_CURRENT_SOURCE_DIR}/bus/private-bus.conf -- $<TARGET_FILE:test_conversation_bus>)
set_tests_properties(test_conversation_bus PROPERTIES
    ENVIRONMENT "SPLIT_ROCK_PRIVATE_BUS=1;QT_QPA_PLATFORM=offscreen"
    SKIP_RETURN_CODE 77
    TIMEOUT 180)

