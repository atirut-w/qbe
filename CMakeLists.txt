cmake_minimum_required(VERSION 3.0)
project(QBE)
enable_testing()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/config.h
    COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && make config.h
)

add_executable(qbe
    config.h

    main.c
    util.c
    parse.c
    abi.c
    cfg.c
    mem.c
    ssa.c
    alias.c
    load.c
    copy.c
    fold.c
    simpl.c
    live.c
    spill.c
    rega.c
    emit.c

    amd64/targ.c
    amd64/sysv.c
    amd64/isel.c
    amd64/emit.c

    arm64/targ.c
    arm64/abi.c
    arm64/isel.c
    arm64/emit.c

    rv64/targ.c
    rv64/abi.c
    rv64/isel.c
    rv64/emit.c
)
target_compile_options(qbe PRIVATE -std=c99 -Wall -Wextra -Wpedantic)
install(TARGETS qbe RUNTIME DESTINATION bin)

add_test(
    NAME check
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/test.sh all
)
add_test(
    NAME check-arm64
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/test.sh all
)
add_test(
    NAME check-rv64
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/test.sh all
)

set_property(TEST check-arm64 PROPERTY ENVIRONMENT "TARGET=arm64")
set_property(TEST check-rv64 PROPERTY ENVIRONMENT "TARGET=rv64")
