add_library(tm_core STATIC
    arp_lookup.cpp
    arp_lookup.h
    be_io.h
    cp_client.cpp
    cp_client.h
    deci3_codec.cpp
    dfmp_codec.cpp
    drfp_codec.cpp
    drfp_codec.h
    dbgp_codec.cpp
    dbgp_codec.h
    lv1_guard.cpp
    lv1_guard.h
    nor_image.cpp
    nor_image.h
    ps3mapi_client.cpp
    ps3mapi_client.h
    tsmp_codec.cpp
    sft_settings.cpp
    sft_schema.cpp
    sft_schema.h
    sft_settings.h
    target_type.h
    tcp_connection.cpp
    tcp_connection.h
    wol.cpp
    wol.h
    dbgshl_cmd.h
    tty_stream.h
)

set_target_properties(tm_core PROPERTIES AUTOMOC ON)

target_include_directories(tm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(tm_core
    PUBLIC
        Qt6::Core
        Qt6::Network
)

opentm_target_speedup(tm_core NETWORK)