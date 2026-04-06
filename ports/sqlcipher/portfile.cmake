vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO sqlcipher/sqlcipher
    REF "v${VERSION}"
    SHA512 023b2fc7248fe38b758ef93dd8436677ff0f5d08b1061e7eab0adb9e38ad92d523e0ab69016ee69bd35c1fd53c10f61e99b01f7a2987a1f1d492e1f7216a0a9c
    HEAD_REF master
)

if(VCPKG_TARGET_IS_WINDOWS)
    # Keep the upstream Windows flow, which relies on Makefile.msc.
    find_program(NMAKE nmake REQUIRED)

    file(GLOB TCLSH_CMD
        ${CURRENT_INSTALLED_DIR}/tools/tcl/bin/tclsh*${VCPKG_HOST_EXECUTABLE_SUFFIX}
    )
    file(TO_NATIVE_PATH "${TCLSH_CMD}" TCLSH_CMD)

    string(REGEX REPLACE ^.*tclsh "" TCLVERSION ${TCLSH_CMD})
    string(REGEX REPLACE [A-Za-z]*${VCPKG_HOST_EXECUTABLE_SUFFIX}$ "" TCLVERSION ${TCLVERSION})

    list(APPEND NMAKE_OPTIONS
        TCLSH_CMD="${TCLSH_CMD}"
        TCLVERSION=${TCLVERSION}
        EXT_FEATURE_FLAGS=-DSQLITE_TEMP_STORE=2\ -DSQLITE_HAS_CODEC
    )

    set(ENV{INCLUDE} "${CURRENT_INSTALLED_DIR}/include;$ENV{INCLUDE}")

    message(STATUS "Pre-building ${TARGET_TRIPLET}")
    vcpkg_execute_required_process(
        COMMAND ${NMAKE} -f Makefile.msc /A /NOLOGO clean sqlite3.c
        ${NMAKE_OPTIONS}
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME pre-build-${TARGET_TRIPLET}
    )
    message(STATUS "Pre-building ${TARGET_TRIPLET} done")
else()
    find_program(MAKE make REQUIRED)

    file(GLOB TCLSH_CANDIDATES
        ${CURRENT_HOST_INSTALLED_DIR}/tools/tcl/bin/tclsh*${VCPKG_HOST_EXECUTABLE_SUFFIX}
        ${CURRENT_INSTALLED_DIR}/tools/tcl/bin/tclsh*${VCPKG_HOST_EXECUTABLE_SUFFIX}
    )
    if(TCLSH_CANDIDATES)
        list(GET TCLSH_CANDIDATES 0 TCLSH_CMD)
    else()
        find_program(TCLSH_CMD tclsh REQUIRED)
    endif()

    get_filename_component(TCLSH_DIR "${TCLSH_CMD}" DIRECTORY)
    if(CMAKE_HOST_WIN32)
        set(PATH_SEP ";")
    else()
        set(PATH_SEP ":")
    endif()

    vcpkg_backup_env_variables(VARS CPPFLAGS LDFLAGS LIBS PATH)
    set(ENV{CPPFLAGS} "-I${CURRENT_INSTALLED_DIR}/include")
    set(ENV{LDFLAGS} "-L${CURRENT_INSTALLED_DIR}/lib")
    set(ENV{LIBS} "-lcrypto -lssl")
    set(ENV{PATH} "${TCLSH_DIR}${PATH_SEP}$ENV{PATH}")

    message(STATUS "Pre-building ${TARGET_TRIPLET}")
    vcpkg_execute_required_process(
        COMMAND "${SOURCE_PATH}/configure" --with-crypto-lib=openssl --disable-shared --enable-tempstore=yes
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME configure-${TARGET_TRIPLET}
    )
    vcpkg_execute_required_process(
        COMMAND ${MAKE} sqlite3.c
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME pre-build-${TARGET_TRIPLET}
    )
    message(STATUS "Pre-building ${TARGET_TRIPLET} done")

    vcpkg_restore_env_variables(VARS CPPFLAGS LDFLAGS LIBS PATH)
endif()

file(COPY ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt DESTINATION "${SOURCE_PATH}")

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        geopoly WITH_GEOPOLY
        json1 WITH_JSON1
        fts5 WITH_FTS5
    INVERTED_FEATURES
        tool SQLITE3_SKIP_TOOLS
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DSQLCIPHER_VERSION=${VERSION}
    OPTIONS_DEBUG
        -DSQLITE3_SKIP_TOOLS=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME ${PORT} CONFIG_PATH share/${PORT})

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

if(NOT SQLITE3_SKIP_TOOLS AND EXISTS "${CURRENT_PACKAGES_DIR}/tools/${PORT}/sqlcipher-bin${VCPKG_HOST_EXECUTABLE_SUFFIX}")
    file(RENAME "${CURRENT_PACKAGES_DIR}/tools/${PORT}/sqlcipher-bin${VCPKG_HOST_EXECUTABLE_SUFFIX}" "${CURRENT_PACKAGES_DIR}/tools/${PORT}/sqlcipher${VCPKG_HOST_EXECUTABLE_SUFFIX}")
endif()

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/sqlcipher-config.in.cmake"
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/sqlcipher-config.cmake"
    @ONLY
)

file(INSTALL "${SOURCE_PATH}/LICENSE.md" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)

vcpkg_copy_pdbs()
vcpkg_copy_tool_dependencies("${CURRENT_PACKAGES_DIR}/tools/${PORT}")
vcpkg_fixup_pkgconfig()
