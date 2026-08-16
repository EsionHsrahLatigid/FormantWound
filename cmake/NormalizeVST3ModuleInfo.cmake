if(NOT DEFINED MODULE_INFO)
    message(FATAL_ERROR "MODULE_INFO is required")
endif()

if(NOT EXISTS "${MODULE_INFO}")
    message(FATAL_ERROR "Missing VST3 moduleinfo.json: ${MODULE_INFO}")
endif()

file(READ "${MODULE_INFO}" module_info)

string(LENGTH "${module_info}" module_info_length)
set(normalized "")
set(index 0)
while(index LESS module_info_length)
    string(SUBSTRING "${module_info}" ${index} 1 current)
    set(skip_current FALSE)

    if(current STREQUAL ",")
        math(EXPR lookahead "${index} + 1")
        set(next_non_space "")
        while(lookahead LESS module_info_length AND next_non_space STREQUAL "")
            string(SUBSTRING "${module_info}" ${lookahead} 1 candidate)
            if(NOT candidate STREQUAL " " AND
               NOT candidate STREQUAL "\t" AND
               NOT candidate STREQUAL "\r" AND
               NOT candidate STREQUAL "\n")
                set(next_non_space "${candidate}")
            endif()
            math(EXPR lookahead "${lookahead} + 1")
        endwhile()

        if(next_non_space STREQUAL "}" OR next_non_space STREQUAL "]")
            set(skip_current TRUE)
        endif()
    endif()

    if(NOT skip_current)
        string(APPEND normalized "${current}")
    endif()
    math(EXPR index "${index} + 1")
endwhile()

set(module_info "${normalized}")

string(JSON module_name ERROR_VARIABLE json_error GET "${module_info}" Name)
if(json_error)
    message(FATAL_ERROR "Could not normalize VST3 moduleinfo.json: ${json_error}")
endif()

file(WRITE "${MODULE_INFO}" "${module_info}")
