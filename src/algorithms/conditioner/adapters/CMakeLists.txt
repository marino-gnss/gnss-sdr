# GNSS-SDR is a Global Navigation Satellite System software-defined receiver.
# This file is part of GNSS-SDR.
#
# SPDX-FileCopyrightText: 2010-2020 C. Fernandez-Prades cfernandez(at)cttc.es
# SPDX-License-Identifier: BSD-3-Clause


set(COND_ADAPTER_SOURCES
    signal_conditioner.cc
    array_signal_conditioner.cc
)

set(COND_ADAPTER_HEADERS
    signal_conditioner.h
    array_signal_conditioner.h
)

list(SORT COND_ADAPTER_HEADERS)
list(SORT COND_ADAPTER_SOURCES)

if(USE_CMAKE_TARGET_SOURCES)
    add_library(conditioner_adapters STATIC)
    target_sources(conditioner_adapters
        PRIVATE
            ${COND_ADAPTER_SOURCES}
        PUBLIC
            ${COND_ADAPTER_HEADERS}
    )
else()
    source_group(Headers FILES ${COND_ADAPTER_HEADERS})
    add_library(conditioner_adapters
        ${COND_ADAPTER_SOURCES}
        ${COND_ADAPTER_HEADERS}
    )
endif()

target_link_libraries(conditioner_adapters
    PUBLIC
        Gnuradio::runtime
)

if(GNURADIO_VERSION VERSION_LESS 3.9)
    target_link_libraries(conditioner_adapters
        PUBLIC
            Boost::headers
    )
endif()

if(ENABLE_GLOG_AND_GFLAGS)
    target_link_libraries(conditioner_adapters PRIVATE Gflags::gflags Glog::glog)
    target_compile_definitions(conditioner_adapters PRIVATE -DUSE_GLOG_AND_GFLAGS=1)
else()
    target_link_libraries(conditioner_adapters PRIVATE absl::flags absl::log)
endif()

target_include_directories(conditioner_adapters
    PUBLIC
        ${GNSSSDR_SOURCE_DIR}/src/core/interfaces
)

if(ENABLE_CLANG_TIDY)
    if(CLANG_TIDY_EXE)
        set_target_properties(conditioner_adapters
            PROPERTIES
                CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
        )
    endif()
endif()

set_property(TARGET conditioner_adapters
    APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
