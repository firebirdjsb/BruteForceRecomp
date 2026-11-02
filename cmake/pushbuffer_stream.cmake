set(original "${XBOXRECOMP_DIR}/src/kernel/nv2a_pb_scan.c")
file(READ "${original}" source)
string(REPLACE "\r\n" "\n" source "${source}")
string(SHA256 digest "${source}")
if(NOT digest STREQUAL "a365519c4c96b5e75a9a2e57eed74ecd0521129eae42fba3ce24c5176fccec90")
    message(FATAL_ERROR "Unaudited pushbuffer decoder; stream repair refused.")
endif()
string(FIND "${source}" "static uint32_t pb_walk_segment(" first)
string(FIND "${source}" "void nv2a_pb_scan_segment(" last)
if(first LESS 0 OR last LESS first)
    message(FATAL_ERROR "Expected original segment decoder boundaries.")
endif()
string(SUBSTRING "${source}" 0 ${first} prefix)
string(SUBSTRING "${source}" ${last} -1 suffix)
set(source "${prefix}\n#include \"pushbuffer_segment.inc\"\n${suffix}")
# The XDK software handlers can replace/clear the hardware CALL return state.
# Use that shared register rather than retaining a stale private CALL flag.
string(REPLACE "at = s_get;"
    "s_ret = *xbox_Nv2aRegPtr(0x324C) & 0xFFFFFFFCu;\n        s_in_call = (*xbox_Nv2aRegPtr(0x324C) & 1u) != 0;\n        at = s_get;" source "${source}")
string(REPLACE "s_ret = s_get;\n            s_in_call = 1;"
    "s_ret = s_get;\n            s_in_call = 1;\n            *xbox_Nv2aRegPtr(0x324C) = s_ret | 1u;" source "${source}")
string(REPLACE "s_get = s_ret;\n                s_in_call = 0;"
    "s_get = s_ret;\n                s_in_call = 0;\n                *xbox_Nv2aRegPtr(0x324C) = s_ret;" source "${source}")
# Pause only after consuming the parameter that arms FLIP_STALL. The DMA
# state (including a pending packet and CALL register) survives the wait.
string(REPLACE "        (*words)++;\n    }"
    "        (*words)++;\n        { extern int nv2a_pb_exec_flip_poll(void);\n          if (nv2a_pb_exec_flip_poll()) break; }\n    }" source "${source}")
string(REPLACE "    while (s_get != put) {"
    "    while (s_get != put) {\n        extern int nv2a_pb_exec_flip_poll(void);\n        if (nv2a_pb_exec_flip_poll()) break;" source "${source}")
set(BF_PB_SCANNER "${CMAKE_BINARY_DIR}/runtime-fixes/nv2a_pb_scan.c")
file(WRITE "${BF_PB_SCANNER}.in" "${source}")
configure_file("${BF_PB_SCANNER}.in" "${BF_PB_SCANNER}" COPYONLY)
get_target_property(sources xbox_kernel SOURCES)
list(REMOVE_ITEM sources "${original}")
list(APPEND sources "${BF_PB_SCANNER}")
set_property(TARGET xbox_kernel PROPERTY SOURCES "${sources}")
