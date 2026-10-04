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
    "QT_QPA_PLATFORM=offscreen"
    "DBUS_SESSION_BUS_ADDRESS=disabled:"
    "SPLIT_ROCK_TEST_HOME=${TEST_HOME}"
)

function(split_rock_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE splitrock_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        PRETEND_PROGRAM="$<TARGET_FILE:split-rock-pretend-assistant>"
        SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/pretend-scripts"
        FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_dependencies(${name} split-rock-pretend-assistant)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${TEST_ENVIRONMENT}" TIMEOUT 120)
endfunction()

split_rock_test(test_answer_text)
split_rock_test(test_change_card)
split_rock_test(test_assistants)
split_rock_test(test_protocol)
split_rock_test(test_conversation)

split_rock_test(test_sheet)
target_link_libraries(test_sheet PRIVATE Qt6::Gui Qt6::Quick)
qt_add_resources(test_sheet "sheet_for_test"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES
        ${PROJECT_SOURCE_DIR}/qml/Main.qml
        ${PROJECT_SOURCE_DIR}/qml/Theme.qml
        ${PROJECT_SOURCE_DIR}/qml/Icon.qml
        ${PROJECT_SOURCE_DIR}/qml/Icons.js
        ${PROJECT_SOURCE_DIR}/qml/JackMark.qml
        ${PROJECT_SOURCE_DIR}/qml/Pill.qml
        ${PROJECT_SOURCE_DIR}/qml/Fold.qml
        ${PROJECT_SOURCE_DIR}/qml/AnswerCard.qml
        ${PROJECT_SOURCE_DIR}/qml/DoItCard.qml
        ${PROJECT_SOURCE_DIR}/qml/SignInPanel.qml
        ${PROJECT_SOURCE_DIR}/qml/AssistantList.qml
)
