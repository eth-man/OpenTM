option(OPENTM_PCH         "Use precompiled headers" ON)
option(OPENTM_CCACHE      "Use ccache/sccache when available" ON)
option(OPENTM_FAST_LINKER "Use mold/lld/gold when available" ON)
option(OPENTM_UNITY       "Unity (jumbo) build" OFF)

set(OPENTM_SPEED_NOTES "" CACHE INTERNAL "")
function(_opentm_note text)
    set(OPENTM_SPEED_NOTES "${OPENTM_SPEED_NOTES};${text}" CACHE INTERNAL "")
endfunction()

set(CMAKE_OPTIMIZE_DEPENDENCIES ON)

if(OPENTM_CCACHE)
    find_program(OPENTM_CACHE_PROGRAM NAMES ccache sccache)
    if(OPENTM_CACHE_PROGRAM)
        execute_process(COMMAND "${OPENTM_CACHE_PROGRAM}" --version RESULT_VARIABLE _cache_rc OUTPUT_VARIABLE _cache_out ERROR_VARIABLE _cache_out)
        if(NOT _cache_rc EQUAL 0 OR NOT _cache_out MATCHES "[cs]cache")
            _opentm_note("compiler cache: ignoring ${OPENTM_CACHE_PROGRAM} - it did not identify itself as ccache/sccache")
            set(OPENTM_CACHE_PROGRAM "OPENTM_CACHE_PROGRAM-NOTFOUND")
        endif()
    endif()
    if(OPENTM_CACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${OPENTM_CACHE_PROGRAM}")
        if(MSVC)
            set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
                "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
        endif()
        get_filename_component(_name "${OPENTM_CACHE_PROGRAM}" NAME_WE)
        _opentm_note("compiler cache: ${_name}")
    elseif(NOT OPENTM_SPEED_NOTES MATCHES "ignoring")
        _opentm_note("compiler cache: none found (install ccache or sccache)")
    endif()
endif()

if(MSVC)
    add_link_options($<$<CONFIG:Debug>:/DEBUG:FASTLINK>)
elseif(OPENTM_FAST_LINKER)
    foreach(_ld mold lld gold)
        find_program(OPENTM_LD_${_ld} ${_ld})
        if(OPENTM_LD_${_ld})
            include(CheckCXXSourceCompiles)
            set(CMAKE_REQUIRED_LINK_OPTIONS -fuse-ld=${_ld})
            check_cxx_source_compiles("int main(){}" OPENTM_LD_WORKS_${_ld})
            unset(CMAKE_REQUIRED_LINK_OPTIONS)
            if(OPENTM_LD_WORKS_${_ld})
                add_link_options(-fuse-ld=${_ld})
                _opentm_note("linker: ${_ld}")
                set(_opentm_ld_found TRUE)
                break()
            endif()
        endif()
    endforeach()
    if(NOT _opentm_ld_found)
        _opentm_note("linker: system default (install mold for faster links)")
    endif()

    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-gsplit-dwarf OPENTM_HAS_SPLIT_DWARF)
    if(OPENTM_HAS_SPLIT_DWARF)
        add_compile_options($<$<CONFIG:Debug,RelWithDebInfo>:-gsplit-dwarf>)
    endif()
endif()

if(OPENTM_UNITY)
    set(CMAKE_UNITY_BUILD ON)
    set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
    _opentm_note("unity build: on (batch 16)")
endif()

function(opentm_target_speedup target)
    if(NOT OPENTM_PCH)
        return()
    endif()
    cmake_parse_arguments(ARG "NETWORK;WIDGETS;GUI;CATCH2" "" "" ${ARGN})

    set(_pch
        <cstdint>
        <cstring>
        <string>
        <vector>
        <QtCore/QByteArray>
        <QtCore/QHash>
        <QtCore/QList>
        <QtCore/QObject>
        <QtCore/QString>
        <QtCore/QStringList>
    )
    if(ARG_NETWORK)
        list(APPEND _pch <QtNetwork/QHostAddress> <QtNetwork/QTcpSocket>)
    endif()
    if(ARG_GUI)
        list(APPEND _pch <QtGui/QIcon>)
    endif()
    if(ARG_WIDGETS)
        list(APPEND _pch
            <QtGui/QIcon>
            <QtWidgets/QBoxLayout>
            <QtWidgets/QDialog>
            <QtWidgets/QLabel>
            <QtWidgets/QLineEdit>
            <QtWidgets/QPushButton>
            <QtWidgets/QWidget>
        )
    endif()
    if(ARG_CATCH2)
        list(APPEND _pch <catch2/catch_test_macros.hpp>)
    endif()

    target_precompile_headers(${target} PRIVATE ${_pch})
endfunction()

function(opentm_report_build_speed)
    message(STATUS "OpenTM build speed:")
    message(STATUS "  precompiled headers: ${OPENTM_PCH}")
    foreach(_n IN LISTS OPENTM_SPEED_NOTES)
        if(_n)
            message(STATUS "  ${_n}")
        endif()
    endforeach()
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        message(STATUS "  note: pass --parallel to cmake --build; MSBuild ""otherwise builds one project at a time")
    endif()
endfunction()
