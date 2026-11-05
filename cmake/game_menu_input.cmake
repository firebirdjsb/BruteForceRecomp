include("${PROJECT_SOURCE_DIR}/cmake/upscaler_state.cmake")
bf_add_upscaler_state()
add_library(bf_host_ui STATIC src/ui/host_ui.cpp src/ui/ui_draw.cpp)
target_link_libraries(bf_host_ui PUBLIC bf_scene_boundary bf_aspect_state bf_upscaler_state)
add_executable(bf_edge_aa_test tests/edge_aa_test.cpp src/graphics/edge_aa.cpp)
target_include_directories(bf_edge_aa_test PRIVATE src)
add_test(NAME edge_antialiasing COMMAND bf_edge_aa_test)
add_executable(bf_host_ui_test tests/host_ui_test.cpp)
target_link_libraries(bf_host_ui_test PRIVATE bf_host_ui)
target_include_directories(bf_host_ui_test PRIVATE src)
add_test(NAME native_menu_ui COMMAND bf_host_ui_test)
add_executable(bf_ui_prompt_frames_test tests/ui_prompt_frames_test.cpp)
target_link_libraries(bf_ui_prompt_frames_test PRIVATE bf_host_ui)
target_include_directories(bf_ui_prompt_frames_test PRIVATE src)
add_test(NAME ui_prompt_source_frames COMMAND bf_ui_prompt_frames_test)
add_executable(bf_tutorial_ui_test tests/game_menu_tutorial_test.cpp src/game_menu_input.cpp)
target_include_directories(bf_tutorial_ui_test PRIVATE src "${XBOXRECOMP_DIR}/src" "${XBOXRECOMP_DIR}/src/kernel")
target_link_libraries(bf_tutorial_ui_test PRIVATE bf_host_ui bf_text_events)
add_test(NAME native_tutorial_ui COMMAND bf_tutorial_ui_test)
add_executable(bf_gameplay_glyph_test tests/gameplay_glyph_test.cpp src/ui/gameplay_glyphs.cpp src/ui/prompt_text.cpp)
target_include_directories(bf_gameplay_glyph_test PRIVATE src "${XBOXRECOMP_DIR}/src" "${XBOXRECOMP_DIR}/src/kernel")
target_link_libraries(bf_gameplay_glyph_test PRIVATE bf_host_ui)
add_test(NAME gameplay_glyph_labels COMMAND bf_gameplay_glyph_test)
list(APPEND BF_GENERATED "${PROJECT_SOURCE_DIR}/src/game_menu_input.cpp")
include("${PROJECT_SOURCE_DIR}/cmake/inline_glyphs.cmake")
set(menu_matches 0)
foreach(file IN LISTS BF_GENERATED)
    file(READ "${file}" text)
    string(FIND "${text}" "void sub_00100910(void)\n{" found)
    if(NOT found EQUAL -1)
        string(REPLACE "\r\n" "\n" text "${text}")
        string(SHA256 digest "${text}")
        if(NOT digest STREQUAL "b37c4130ff4ced46b6cf0f0f1528edc03770ebd161ebb45538bd0f9736b1cd54")
            message(FATAL_ERROR "Unaudited original menu draw; mouse integration refused.")
        endif()
        string(REPLACE "void sub_00100910(void)\n{" "void bf_original_menu_draw(void)\n{" text "${text}")
        set(copy "${CMAKE_BINARY_DIR}/runtime-fixes/game-menu.c")
        file(WRITE "${copy}.in" "${text}")
        configure_file("${copy}.in" "${copy}" COPYONLY)
        list(REMOVE_ITEM BF_GENERATED "${file}")
        list(APPEND BF_GENERATED "${copy}")
        math(EXPR menu_matches "${menu_matches}+1")
    endif()
endforeach()
if(NOT menu_matches EQUAL 1)
    message(FATAL_ERROR "Expected exactly one original menu draw.")
endif()

function(bf_ui_read target filename)
    get_target_property(sources ${target} SOURCES)
    get_target_property(directory ${target} SOURCE_DIR)
    foreach(item IN LISTS sources)
        get_filename_component(absolute "${item}" ABSOLUTE BASE_DIR "${directory}")
        get_filename_component(name "${absolute}" NAME)
        if(name STREQUAL filename)
            file(READ "${absolute}" source)
            string(REPLACE "\r\n" "\n" source "${source}")
            set(BF_UI_SOURCE "${source}" PARENT_SCOPE)
            set(BF_UI_ORIGINAL "${absolute}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "UI integration could not find ${target}/${filename}")
endfunction()
function(bf_ui_write target filename)
    set(copy "${CMAKE_BINARY_DIR}/runtime-fixes/${filename}")
    file(WRITE "${copy}.in" "#include \"ui/host_ui.h\"\n${BF_UI_SOURCE}")
    configure_file("${copy}.in" "${copy}" COPYONLY)
    get_target_property(sources ${target} SOURCES)
    get_target_property(directory ${target} SOURCE_DIR)
    set(replaced)
    foreach(item IN LISTS sources)
        get_filename_component(absolute "${item}" ABSOLUTE BASE_DIR "${directory}")
        if(absolute STREQUAL BF_UI_ORIGINAL)
            list(APPEND replaced "${copy}")
        else()
            list(APPEND replaced "${absolute}")
        endif()
    endforeach()
    set_property(TARGET ${target} PROPERTY SOURCES "${replaced}")
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(${target} PUBLIC bf_host_ui)
endfunction()

bf_ui_read(xbox_input game-key-mapping.c)
string(REPLACE "const int capture_text = bf_text_active();" "const int capture_text = bf_text_active();\n    const int capture_ui = bf_ui_open();" BF_UI_SOURCE "${BF_UI_SOURCE}")
string(REPLACE "down[vk] = (!capture_text" "down[vk] = !capture_ui && (!capture_text" BF_UI_SOURCE "${BF_UI_SOURCE}")
set(poll_before [=[    const char *v = getenv("RECOMP_KEYBOARD");
    if (!v || !*v || *v == '0')
        return 0;
    xbox_InputKeyboardState(out);
    return 1;]=])
set(poll_after [=[    static uint32_t native_packet;
    const char *v = getenv("RECOMP_KEYBOARD");
    const int mapped = v && *v && *v != '0';
    int native;
    if (mapped) xbox_InputKeyboardState(out);
    else { memset(out,0,sizeof(*out)); out->dwPacketNumber=++native_packet; }
    native=bf_ui_pad(&out->Gamepad.wButtons,out->Gamepad.bAnalogButtons);
    return mapped || native || bf_ui_capture();]=])
string(REPLACE "${poll_before}" "${poll_after}" BF_UI_SOURCE "${BF_UI_SOURCE}")
bf_ui_write(xbox_input game-ui-keys.c)

bf_ui_read(xbox_input xinput_device.c)
string(SHA256 digest "${BF_UI_SOURCE}")
if(NOT digest STREQUAL "dba2708b822e5269748558e0d55898082d7478197324b52a56bd9224409ec05f")
    message(FATAL_ERROR "Unaudited physical input source.")
endif()
set(activity [=[    if (dwPort == 0) {
        static uint32_t previous_packet;
        if (pState->dwPacketNumber != previous_packet) {
            const XBOX_GAMEPAD *g=&pState->Gamepad;
            int active=g->wButtons || abs(g->sThumbLX)>8000 || abs(g->sThumbLY)>8000 || abs(g->sThumbRX)>8000 || abs(g->sThumbRY)>8000;
            for (int i=0;i<8;i++) active |= g->bAnalogButtons[i]>30;
            if(active) bf_ui_controller_activity();
            previous_packet=pState->dwPacketNumber;
        }
        keyboard_merge(pState);
        if (bf_ui_open()) memset(&pState->Gamepad,0,sizeof(pState->Gamepad));
    }]=])
string(REPLACE "    if (dwPort == 0)\n        keyboard_merge(pState);" "${activity}" BF_UI_SOURCE "${BF_UI_SOURCE}")
bf_ui_write(xbox_input game-ui-gamepad.c)

if(WIN32)
    bf_ui_read(xbox_input game-text-mkb.c)
    string(REPLACE "if (bf_text_active())" "if (bf_text_active() || bf_ui_capture())" BF_UI_SOURCE "${BF_UI_SOURCE}")
    bf_ui_write(xbox_input game-ui-mkb.c)
    bf_ui_read(xbox_video game-text-window.c)
    foreach(anchor IN ITEMS "s_present_ids[next]=++s_present_count;" "gpu_present_source(s_present_ids[idx],s_present_times[idx]);")
        string(FIND "${BF_UI_SOURCE}" "${anchor}" found)
        if(found LESS 0)
            message(FATAL_ERROR "Expected locked image/glyph frame handoff.")
        endif()
    endforeach()
    string(REPLACE "s_present_ids[next]=++s_present_count;" "s_present_ids[next]=++s_present_count;\n    bf_ui_prompts_publish(s_present_ids[next]);" BF_UI_SOURCE "${BF_UI_SOURCE}")
    string(REPLACE "gpu_present_source(s_present_ids[idx],s_present_times[idx]);" "gpu_present_source(s_present_ids[idx],s_present_times[idx]);\n            bf_ui_prompts_select(s_present_ids[idx]);" BF_UI_SOURCE "${BF_UI_SOURCE}")
    string(REPLACE "    case WM_CHAR:" "    case WM_CHAR:\n        if(bf_ui_open())return 0;" BF_UI_SOURCE "${BF_UI_SOURCE}")
    string(REPLACE "if (m == WM_KEYDOWN && bf_text_key((uint32_t)w)) return 0;"
        "if (m == WM_KEYDOWN && bf_ui_key((unsigned)w,(l & (1u<<30)) != 0)) return 0;\n        if (m == WM_KEYDOWN && !bf_text_active() && bf_ui_menu_key((unsigned)w,(l & (1u<<30)) != 0)) return 0;\n        if (m == WM_KEYDOWN && bf_text_key((uint32_t)w)) return 0;" BF_UI_SOURCE "${BF_UI_SOURCE}")
    string(REPLACE "    case WM_LBUTTONDOWN: case WM_LBUTTONUP:"
        "    case WM_MOUSEMOVE: {\n        RECT r; GetClientRect(h,&r);\n        bf_ui_mouse((short)LOWORD(l),(short)HIWORD(l),r.right,r.bottom,0);\n        return 0;\n    }\n    case WM_LBUTTONDOWN: case WM_LBUTTONUP:" BF_UI_SOURCE "${BF_UI_SOURCE}")
    string(REPLACE "            int down = 0;" "            if (bf_ui_capture()) {\n                RECT r;GetClientRect(h,&r);\n                if(m==WM_LBUTTONDOWN || m==WM_RBUTTONDOWN)bf_ui_mouse((short)LOWORD(l),(short)HIWORD(l),r.right,r.bottom,m==WM_LBUTTONDOWN?1:2);\n                return 0;\n            }\n            int down = 0;" BF_UI_SOURCE "${BF_UI_SOURCE}")
    string(REPLACE "        bf_text_reset();\n        xbox_InputKeysClear();" "        bf_ui_focus_lost();\n        bf_text_reset();\n        xbox_InputKeysClear();" BF_UI_SOURCE "${BF_UI_SOURCE}")
    string(REPLACE "            paced = fb_show(hwnd, hdc, &bi);" "            bf_ui_paint(s_rgb,s_fb_width,s_fb_height);\n            paced = fb_show(hwnd, hdc, &bi);" BF_UI_SOURCE "${BF_UI_SOURCE}")
    set(size_request [=[        unsigned requested_w,requested_h;
        if(bf_ui_take_size(&requested_w,&requested_h)) {
            MONITORINFO info;info.cbSize=sizeof(info);
            if(GetMonitorInfoA(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&info)) {
                double fit_x=(info.rcWork.right-info.rcWork.left-32.0)/requested_w;
                double fit_y=(info.rcWork.bottom-info.rcWork.top-64.0)/requested_h;
                double fit=fit_x<fit_y?fit_x:fit_y;if(fit>1)fit=1;
                requested_w=(unsigned)(requested_w*fit);requested_h=(unsigned)(requested_h*fit);
            }
            RECT desired={0,0,(LONG)requested_w,(LONG)requested_h};
            AdjustWindowRectEx(&desired,(DWORD)GetWindowLongPtr(hwnd,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtr(hwnd,GWL_EXSTYLE));
            SetWindowPos(hwnd,NULL,0,0,desired.right-desired.left,desired.bottom-desired.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
        if (s_present_idx >= 0 && s_rgb) {]=])
    string(REPLACE "        if (s_present_idx >= 0 && s_rgb) {" "${size_request}" BF_UI_SOURCE "${BF_UI_SOURCE}")
    bf_ui_write(xbox_video game-ui-window.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    bf_ui_read(xbox_input game-text-sdl-keys.c)
    string(REPLACE "    case SDL_TEXTINPUT:" "    case SDL_TEXTINPUT:\n        if(bf_ui_open())return;" BF_UI_SOURCE "${BF_UI_SOURCE}")
    string(REPLACE "    case SDL_KEYDOWN:" "    case SDL_KEYDOWN:\n        vk=xbox_InputSdlKeyToVk(ev->key.keysym.sym);\n        if(bf_ui_key(vk,ev->key.repeat))return;\n        if(!bf_text_active() && bf_ui_menu_key(vk,ev->key.repeat))return;" BF_UI_SOURCE "${BF_UI_SOURCE}")
    string(REPLACE "bf_text_reset(); xbox_InputKeysClear();" "bf_ui_focus_lost(); bf_text_reset(); xbox_InputKeysClear();" BF_UI_SOURCE "${BF_UI_SOURCE}")
    bf_ui_write(xbox_input game-ui-sdl-keys.c)
    bf_ui_read(xbox_video game-text-sdl-window.c)
    foreach(anchor IN ITEMS "s_ready_seq++;" "seq = s_ready_seq;")
        string(FIND "${BF_UI_SOURCE}" "${anchor}" found)
        if(found LESS 0)
            message(FATAL_ERROR "Expected locked SDL image/glyph frame handoff.")
        endif()
    endforeach()
    string(REPLACE "s_ready_seq++;" "s_ready_seq++;\n        bf_ui_prompts_publish(s_ready_seq);" BF_UI_SOURCE "${BF_UI_SOURCE}")
    string(REPLACE "seq = s_ready_seq;" "seq = s_ready_seq;\n        bf_ui_prompts_select(seq);" BF_UI_SOURCE "${BF_UI_SOURCE}")
    string(REPLACE "        uint64_t seq, now;" "        uint64_t seq, now;\n        unsigned requested_w,requested_h;\n        if(bf_ui_take_size(&requested_w,&requested_h)){\n            SDL_Rect bounds;if(!SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(win),&bounds)){\n                float fx=(bounds.w-32.0f)/requested_w,fy=(bounds.h-64.0f)/requested_h,fit=fx<fy?fx:fy;\n                if(fit<1){requested_w=(unsigned)(requested_w*fit);requested_h=(unsigned)(requested_h*fit);}\n            }\n            SDL_SetWindowSize(win,requested_w,requested_h);\n        }" BF_UI_SOURCE "${BF_UI_SOURCE}")
    # SDL_RenderSetLogicalSize already scales window pointer events into the
    # 640x480 logical canvas; scaling by the physical size again is incorrect.
    string(REPLACE "                xbox_InputSdlEvent(&ev);" "                if(ev.type==SDL_MOUSEMOTION || ev.type==SDL_MOUSEBUTTONDOWN) {\n                    bf_ui_mouse(ev.type==SDL_MOUSEMOTION?ev.motion.x:ev.button.x,ev.type==SDL_MOUSEMOTION?ev.motion.y:ev.button.y,640,480,ev.type==SDL_MOUSEMOTION?0:ev.button.button==SDL_BUTTON_LEFT?1:2);\n                }\n                xbox_InputSdlEvent(&ev);" BF_UI_SOURCE "${BF_UI_SOURCE}")
    set(before "        if (px) {\n            if (!tex || bpp != tex_bpp) {")
    set(after [=[        static uint32_t ui_base[640*480],ui_work[640*480];
        static int had_ui;
        if (px) {
            for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++) {
                const uint8_t *row=px+y*pitch;
                if(bpp==4) ui_base[y*640+x]=((const uint32_t *)row)[x];
                else {uint32_t v=((const uint16_t *)row)[x];ui_base[y*640+x]=0xff000000u|((v&0xf800)<<8)|((v&0x7e0)<<5)|((v&31)<<3);}
            }
        }
        if (px || bf_ui_open() || had_ui) {
            had_ui=bf_ui_open();memcpy(ui_work,ui_base,sizeof ui_work);
            bf_ui_paint(ui_work,640,480);px=(const uint8_t *)ui_work;bpp=4;pitch=640*4;
            if (!tex || bpp != tex_bpp) {]=])
    string(REPLACE "${before}" "${after}" BF_UI_SOURCE "${BF_UI_SOURCE}")
    bf_ui_write(xbox_video game-ui-sdl-window.c)
endif()

bf_ui_read(xbox_apu apu_core.c)
string(SHA256 digest "${BF_UI_SOURCE}")
if(NOT digest STREQUAL "c15d0b1a599f220d662d85f3a45f141cd128acecb383fba6fa03ce9479331b46")
    message(FATAL_ERROR "Unaudited APU mixer output source.")
endif()
set(gain [=[    {
        static float previous_gain=1.0f;
        const float gain=bf_ui_audio_gain();
        if(gain!=1.0f || previous_gain!=1.0f) {
            for(int i=0;i<MIXER_FRAME_SAMPLES;i++) {
                const float g=previous_gain+(gain-previous_gain)*(float)(i+1)/MIXER_FRAME_SAMPLES;
                period[i][0]=(int16_t)(period[i][0]*g);period[i][1]=(int16_t)(period[i][1]*g);
            }
        }
        previous_gain=gain;
    }
    /* A period dropped while pacing is stalled never reaches the device,]=])
string(REPLACE "    /* A period dropped while pacing is stalled never reaches the device," "${gain}" BF_UI_SOURCE "${BF_UI_SOURCE}")
bf_ui_write(xbox_apu game-ui-audio.c)
