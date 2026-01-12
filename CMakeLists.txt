# Battery History Feature CMakeLists.txt

# Include directories
zephyr_include_directories(include)

# DTS bindings
zephyr_include_directories(dts/bindings)

# Battery History Feature
if(CONFIG_ZMK_BATTERY_HISTORY)
    target_sources(app PRIVATE src/battery_history/battery_history.c)

    if(CONFIG_ZMK_BATTERY_HISTORY_SPLIT)
        target_sources(app PRIVATE src/behaviors/behavior_battery_history_request.c)
        target_sources(app PRIVATE src/battery_history/battery_history_split.c)
    endif()

    if(CONFIG_ZMK_BATTERY_HISTORY_STUDIO_RPC)
        target_sources(app PRIVATE src/battery_history/battery_history_handler.c)

        list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
        include(nanopb)
        set(NANOPB_GENERATE_CPP_APPEND_PATH TRUE)
        set(NANOPB_GENERATE_CPP_STANDALONE OFF)

        # Create library for battery history proto files
        zephyr_library_named(battery_history_proto)
        file(GLOB BATTERY_HISTORY_PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/proto/zmk/battery_history/*.proto)

        nanopb_generate_cpp(battery_history_proto_srcs battery_history_proto_hdrs
                            RELPATH ${CMAKE_CURRENT_SOURCE_DIR} ${BATTERY_HISTORY_PROTO_FILES})
        target_include_directories(battery_history_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
        target_sources(battery_history_proto PRIVATE ${battery_history_proto_srcs} ${battery_history_proto_hdrs})

        # app should depend on the generated proto files
        target_include_directories(app PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/proto)
        add_dependencies(app battery_history_proto)
    endif()
endif()
