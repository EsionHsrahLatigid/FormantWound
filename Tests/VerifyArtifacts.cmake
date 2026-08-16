if(NOT DEFINED STAGE_DIR OR NOT DEFINED SLUG)
    message(FATAL_ERROR "STAGE_DIR and SLUG are required")
endif()

if(APPLE)
    set(standalone "${STAGE_DIR}/standalone/${SLUG}_standalone_plugin.app")
elseif(WIN32)
    set(standalone "${STAGE_DIR}/standalone/${SLUG}_standalone_plugin.exe")
else()
    set(standalone "${STAGE_DIR}/standalone/${SLUG}_standalone_plugin")
endif()
set(vst3 "${STAGE_DIR}/vst3/${SLUG}_vst3_plugin.vst3")
set(manifest "${STAGE_DIR}/ARTIFACTS.txt")
set(module_info "${vst3}/Contents/Resources/moduleinfo.json")

foreach(path IN ITEMS "${standalone}" "${vst3}" "${manifest}" "${module_info}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing staged artifact: ${path}")
    endif()
endforeach()

file(READ "${module_info}" module_info_contents)
string(FIND "${module_info_contents}" "\"Name\": \"FormantWound\"" module_name_pos)
if(module_name_pos EQUAL -1)
    message(FATAL_ERROR "VST3 moduleinfo.json lacks the FormantWound identity")
endif()

string(FIND "${module_info_contents}" "\"Vendor\": \"EsionHsrahLatigid\"" module_vendor_pos)
if(module_vendor_pos EQUAL -1)
    message(FATAL_ERROR "VST3 moduleinfo.json lacks the EsionHsrahLatigid vendor identity")
endif()

if(EXPECT_AU)
    set(standalone_plist "${standalone}/Contents/Info.plist")
    set(au "${STAGE_DIR}/au/${SLUG}_au_plugin.component")

    file(READ "${standalone_plist}" plist_contents)
    string(FIND "${plist_contents}" "NSMicrophoneUsageDescription" microphone_key)
    if(microphone_key EQUAL -1)
        message(FATAL_ERROR "Standalone Info.plist lacks NSMicrophoneUsageDescription")
    endif()

    if(NOT EXISTS "${au}")
        message(FATAL_ERROR "Missing staged AU: ${au}")
    endif()

    foreach(bundle IN ITEMS "${vst3}" "${standalone}" "${au}")
        execute_process(
            COMMAND codesign --verify --deep --strict "${bundle}"
            RESULT_VARIABLE codesign_result
            ERROR_VARIABLE codesign_error)
        if(NOT codesign_result EQUAL 0)
            message(FATAL_ERROR "Invalid signature for ${bundle}: ${codesign_error}")
        endif()
    endforeach()
endif()
