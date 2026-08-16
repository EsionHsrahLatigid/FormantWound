cmake_minimum_required(VERSION 3.22)

project(FormantWound VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FORMANTWOUND_BUILD_PLUGIN "Build the JUCE plug-in targets" ON)
option(FORMANTWOUND_BUILD_TESTS "Build tests" ON)
set(FORMANTWOUND_ARTIFACT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/artifacts" CACHE PATH
    "Directory for final VST3, AU, and Standalone products")
set(EHL_JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE source checkout; avoids network when set")

add_library(formantwound_dsp STATIC
    Source/dsp/FormantWoundCore.cpp)

target_include_directories(formantwound_dsp PUBLIC Source)
target_compile_features(formantwound_dsp PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(formantwound_dsp PRIVATE /W4)
else()
    target_compile_options(formantwound_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(FORMANTWOUND_BUILD_TESTS)
    enable_testing()
    add_executable(FormantWoundTests Tests/TestMain.cpp)
    target_link_libraries(FormantWoundTests PRIVATE formantwound_dsp)
    add_test(NAME FormantWoundDSPTests COMMAND FormantWoundTests)
endif()

if(FORMANTWOUND_BUILD_PLUGIN)
    include(FetchContent)

    if(EHL_JUCE_SOURCE_DIR)
        if(NOT EXISTS "${EHL_JUCE_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "EHL_JUCE_SOURCE_DIR does not contain JUCE: ${EHL_JUCE_SOURCE_DIR}")
        endif()
        add_subdirectory("${EHL_JUCE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
        add_subdirectory(JUCE)
    else()
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.15
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    add_subdirectory(modules/juce-ehl-design-module)

    set(FORMANTWOUND_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND FORMANTWOUND_FORMATS AU)
    endif()

    set(EHL_COPY_PLUGIN_AFTER_BUILD_DEFAULT OFF)
    if(APPLE AND NOT DEFINED ENV{CI})
        set(EHL_COPY_PLUGIN_AFTER_BUILD_DEFAULT ON)
    endif()
    option(EHL_COPY_PLUGIN_AFTER_BUILD
        "Copy built plug-in formats to the current user's standard plug-in folders"
        ${EHL_COPY_PLUGIN_AFTER_BUILD_DEFAULT})

    set(EHL_PLUGIN_COPY_ARGS
        COPY_PLUGIN_AFTER_BUILD ${EHL_COPY_PLUGIN_AFTER_BUILD})
    if(APPLE)
        list(APPEND EHL_PLUGIN_COPY_ARGS
            VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3"
            AU_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
    endif()

    juce_add_plugin(FormantWound
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "EsionHsrahLatigid"
        BUNDLE_ID "jp.ehl.formantwound"
        PRODUCT_NAME "FormantWound"
        DESCRIPTION "LPC source-filter envelope damage and corrupted excitation effect"
        PLUGIN_MANUFACTURER_CODE EHL_
        PLUGIN_CODE FmWt
        AU_SANDBOX_SAFE TRUE
        FORMATS ${FORMANTWOUND_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "FormantWound requires microphone access to process live audio input."
        ${EHL_PLUGIN_COPY_ARGS})

    target_sources(FormantWound PRIVATE
        Source/ParameterIDs.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h)

    target_compile_definitions(FormantWound PUBLIC
        JUCE_REPORT_APP_USAGE=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0)

    target_link_libraries(FormantWound
        PRIVATE
            formantwound_dsp
            EHL::JuceDesign
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    target_sources(FormantWound_Standalone PRIVATE Source/StandaloneApp.cpp)
    target_compile_definitions(FormantWound_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

    set(formantwound_stage_platform "linux-x64")
    if(APPLE)
        set(formantwound_stage_platform "macos-arm64")
    elseif(WIN32)
        set(formantwound_stage_platform "windows-x64")
    endif()
    set(formantwound_juce_products "${CMAKE_CURRENT_BINARY_DIR}/FormantWound_artefacts/$<CONFIG>")
    set(formantwound_staged_products "${FORMANTWOUND_ARTIFACT_DIR}/plugin-release/${formantwound_stage_platform}")
    set(formantwound_artifact_dependencies FormantWound_VST3 FormantWound_Standalone)
    set(formantwound_artifact_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory "${formantwound_staged_products}/vst3"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${formantwound_staged_products}/standalone"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${formantwound_staged_products}/au")

    if(APPLE)
        list(APPEND formantwound_artifact_commands
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${formantwound_staged_products}/vst3/formantwound_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${formantwound_staged_products}/standalone/formantwound_standalone_plugin.app"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${formantwound_staged_products}/au/formantwound_au_plugin.component"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${formantwound_juce_products}/VST3/FormantWound.vst3" "${formantwound_staged_products}/vst3/formantwound_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${formantwound_juce_products}/Standalone/FormantWound.app" "${formantwound_staged_products}/standalone/formantwound_standalone_plugin.app")
    elseif(WIN32)
        list(APPEND formantwound_artifact_commands
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${formantwound_staged_products}/vst3/formantwound_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${formantwound_juce_products}/VST3/FormantWound.vst3" "${formantwound_staged_products}/vst3/formantwound_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy
                    "${formantwound_juce_products}/Standalone/FormantWound.exe" "${formantwound_staged_products}/standalone/formantwound_standalone_plugin.exe")
    else()
        list(APPEND formantwound_artifact_commands
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${formantwound_staged_products}/vst3/formantwound_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${formantwound_juce_products}/VST3/FormantWound.vst3" "${formantwound_staged_products}/vst3/formantwound_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy
                    "${formantwound_juce_products}/Standalone/FormantWound" "${formantwound_staged_products}/standalone/formantwound_standalone_plugin")
    endif()

    if(TARGET FormantWound_AU)
        list(APPEND formantwound_artifact_dependencies FormantWound_AU)
        list(APPEND formantwound_artifact_commands
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${formantwound_juce_products}/AU/FormantWound.component" "${formantwound_staged_products}/au/formantwound_au_plugin.component")
    endif()

    if(APPLE)
        list(APPEND formantwound_artifact_commands
            COMMAND codesign --force --deep --sign -
                    "${formantwound_staged_products}/vst3/formantwound_vst3_plugin.vst3"
            COMMAND codesign --force --deep --sign -
                    "${formantwound_staged_products}/standalone/formantwound_standalone_plugin.app"
            COMMAND codesign --force --deep --sign -
                    "${formantwound_staged_products}/au/formantwound_au_plugin.component")
    endif()

    list(APPEND formantwound_artifact_commands
        COMMAND ${CMAKE_COMMAND}
            -DPRODUCT_NAME=FormantWound
            -DSLUG=formantwound
            -DBUNDLE_ID=jp.ehl.formantwound
            -DPLUGIN_CODE=FmWt
            -DSTAGE_DIR=${formantwound_staged_products}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteArtifactsManifest.cmake")

    add_custom_target(ehl_stage_products
        ${formantwound_artifact_commands}
        DEPENDS ${formantwound_artifact_dependencies}
        COMMENT "Staging FormantWound products under ${FORMANTWOUND_ARTIFACT_DIR}/plugin-release")

    add_custom_target(FormantWound_Artifacts DEPENDS ehl_stage_products)

    if(FORMANTWOUND_BUILD_TESTS)
        add_executable(FormantWoundIntegrationTests Tests/PluginIntegrationTest.cpp)
        target_include_directories(FormantWoundIntegrationTests PRIVATE Source)
        target_link_libraries(FormantWoundIntegrationTests PRIVATE FormantWound EHL::JuceDesign juce::juce_audio_utils)
        add_dependencies(FormantWoundIntegrationTests FormantWound_Artifacts)

        juce_add_console_app(FormantWoundPluginLoadTest
            PRODUCT_NAME "FormantWound Plugin Load Test")
        target_sources(FormantWoundPluginLoadTest PRIVATE Tests/PluginLoadTest.cpp)
        target_include_directories(FormantWoundPluginLoadTest PRIVATE Source)
        target_link_libraries(FormantWoundPluginLoadTest PRIVATE
            juce::juce_audio_processors_headless
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
        target_compile_definitions(FormantWoundPluginLoadTest PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0)
        add_dependencies(FormantWoundPluginLoadTest FormantWound_VST3)

        add_test(NAME FormantWoundPluginIntegrationTests COMMAND FormantWoundIntegrationTests)
        add_test(NAME FormantWoundHostVST3
            COMMAND FormantWoundPluginLoadTest
                    "${formantwound_juce_products}/VST3/FormantWound.vst3"
                    "FormantWound")
        add_test(
            NAME FormantWoundArtifactChecks
            COMMAND ${CMAKE_COMMAND}
                    -DSTAGE_DIR=${formantwound_staged_products}
                    -DSLUG=formantwound
                    -DEXPECT_AU=$<BOOL:${APPLE}>
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/VerifyArtifacts.cmake)
    endif()
endif()
