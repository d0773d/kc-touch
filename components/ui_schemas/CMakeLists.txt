set(YAMUI_SCHEMA_DIR "${CMAKE_CURRENT_LIST_DIR}/schemas")

file(GLOB_RECURSE YAMUI_SCHEMA_INPUTS
    "${YAMUI_SCHEMA_DIR}/*.yml"
    "${YAMUI_SCHEMA_DIR}/*.yaml"
)

if(NOT YAMUI_SCHEMA_INPUTS)
    message(FATAL_ERROR "No YamUI schema sources found under ${YAMUI_SCHEMA_DIR}")
endif()

get_filename_component(YAMUI_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
set(YAMUI_BUNDLE_PY "${YAMUI_REPO_ROOT}/tools/yamui_bundle.py")
set(YAMUI_GENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/generated")
set(YAMUI_BUNDLE_FILE "${YAMUI_GENERATED_DIR}/yamui_bundle.yml")
set(YAMUI_MANIFEST_FILE "${YAMUI_GENERATED_DIR}/yamui_manifest.json")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    if(NOT EXISTS ${YAMUI_BUNDLE_FILE})
        file(MAKE_DIRECTORY ${YAMUI_GENERATED_DIR})
        execute_process(
            COMMAND ${Python3_EXECUTABLE} ${YAMUI_BUNDLE_PY}
                    --input ${YAMUI_SCHEMA_DIR}
                    --output-yaml ${YAMUI_BUNDLE_FILE}
                    --output-manifest ${YAMUI_MANIFEST_FILE}
                    --root ${YAMUI_REPO_ROOT}
            RESULT_VARIABLE YAMUI_BUNDLE_RESULT
        )
        if(NOT YAMUI_BUNDLE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to generate initial YamUI bundle")
        endif()
    endif()

    file(GLOB_RECURSE YAMUI_SCHEMA_INPUTS
        "${YAMUI_SCHEMA_DIR}/*.yml"
        "${YAMUI_SCHEMA_DIR}/*.yaml"
    )

    add_custom_command(
        OUTPUT ${YAMUI_BUNDLE_FILE} ${YAMUI_MANIFEST_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${YAMUI_GENERATED_DIR}
        COMMAND ${Python3_EXECUTABLE} ${YAMUI_BUNDLE_PY}
                --input ${YAMUI_SCHEMA_DIR}
                --output-yaml ${YAMUI_BUNDLE_FILE}
                --output-manifest ${YAMUI_MANIFEST_FILE}
                --root ${YAMUI_REPO_ROOT}
        DEPENDS ${YAMUI_BUNDLE_PY} ${YAMUI_SCHEMA_INPUTS}
        COMMENT "Bundling YamUI schemas"
        VERBATIM
    )

    add_custom_target(yamui_bundle ALL
        DEPENDS ${YAMUI_BUNDLE_FILE} ${YAMUI_MANIFEST_FILE}
    )
endif()

idf_component_register(
    SRCS "src/ui_schemas.c"
    INCLUDE_DIRS "include"
    EMBED_FILES "generated/yamui_bundle.yml"
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_dependencies(${COMPONENT_LIB} yamui_bundle)
endif()
