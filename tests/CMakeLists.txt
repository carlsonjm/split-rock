# Every test runs with a home folder of its own inside the build, so no test
# can reach a real person's settings, files or session, or an account or the
# network.
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
    # One named 1280 by 800 screen, as a tablet's.
    "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/offscreen-screen.json"
    "DBUS_SESSION_BUS_ADDRESS=disabled:"
    "SPLIT_ROCK_TEST_HOME=${TEST_HOME}"
)

function(split_rock_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE splitrock_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        GUIDE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/guide-fixtures"
        PROJECT_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${TEST_ENVIRONMENT}" TIMEOUT 120)
endfunction()

split_rock_test(test_answer_text)
split_rock_test(test_change)
split_rock_test(test_update)
split_rock_test(test_chores)
split_rock_test(test_notebook)
split_rock_test(test_guide)

# Core, the front desk: its judgement, when it speaks, its files and its bus
# interface, with looks and a clock of the test's own.
# On a bus of its own, so the bus interface is tested too.
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE splitrock_core_watch Qt6::DBus Qt6::Test)
add_test(NAME test_core COMMAND ${DBUS_RUN_SESSION}
    --config-file=${CMAKE_CURRENT_SOURCE_DIR}/bus/private-bus.conf -- $<TARGET_FILE:test_core>)
set(TEST_CORE_ENVIRONMENT ${TEST_ENVIRONMENT})
list(FILTER TEST_CORE_ENVIRONMENT EXCLUDE REGEX "^DBUS_SESSION_BUS_ADDRESS=")
set_tests_properties(test_core PROPERTIES ENVIRONMENT "${TEST_CORE_ENVIRONMENT}" TIMEOUT 60)

# The Helpers' window and its cards.
split_rock_test(test_sheet)
target_link_libraries(test_sheet PRIVATE Qt6::Gui Qt6::Quick)
qt_add_resources(test_sheet "sheet_for_test"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES
        ${PROJECT_SOURCE_DIR}/qml/ActionWindow.qml
        ${PROJECT_SOURCE_DIR}/qml/Theme.qml
        ${PROJECT_SOURCE_DIR}/qml/Icon.qml
        ${PROJECT_SOURCE_DIR}/qml/Icons.js
        ${PROJECT_SOURCE_DIR}/assets/mark.svg
        ${PROJECT_SOURCE_DIR}/qml/Pill.qml
        ${PROJECT_SOURCE_DIR}/qml/Fold.qml
        ${PROJECT_SOURCE_DIR}/qml/UpdateCard.qml
        ${PROJECT_SOURCE_DIR}/qml/CardFrame.qml
        ${PROJECT_SOURCE_DIR}/qml/CardHead.qml
        ${PROJECT_SOURCE_DIR}/qml/CardNote.qml
        ${PROJECT_SOURCE_DIR}/qml/CardFact.qml
        ${PROJECT_SOURCE_DIR}/qml/CardProgress.qml
        ${PROJECT_SOURCE_DIR}/qml/CardRow.qml
        ${PROJECT_SOURCE_DIR}/qml/SpaceCard.qml
        ${PROJECT_SOURCE_DIR}/qml/InstallCard.qml
        ${PROJECT_SOURCE_DIR}/qml/RestoreCard.qml
        ${PROJECT_SOURCE_DIR}/qml/HealthCard.qml
        ${PROJECT_SOURCE_DIR}/qml/ChangesCard.qml
        ${PROJECT_SOURCE_DIR}/qml/CommunityCard.qml
        ${PROJECT_SOURCE_DIR}/qml/WheelScroll.qml
)
