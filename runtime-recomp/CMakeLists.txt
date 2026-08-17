cmake_minimum_required(VERSION 3.24)

project(DKRRuntimePreparation LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(DKR_RELEASE_VERSION "" CACHE STRING "Public DKR-R release version")
if(DKR_RELEASE_VERSION STREQUAL "")
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" DKR_RELEASE_VERSION
         LIMIT_COUNT 1)
endif()
if(DKR_RELEASE_VERSION STREQUAL "")
    set(DKR_RELEASE_VERSION "development")
endif()
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" DKR_RELEASE_VERSION_NUMERIC
       "${DKR_RELEASE_VERSION}")
if(DKR_RELEASE_VERSION_NUMERIC STREQUAL "")
    set(DKR_RELEASE_VERSION_NUMERIC "1.0.0")
endif()
string(REGEX MATCH "-rc([0-9]+)$" DKR_RELEASE_RC_MATCH
       "${DKR_RELEASE_VERSION}")
if(DKR_RELEASE_RC_MATCH)
    set(DKR_RELEASE_BUILD_NUMBER "${CMAKE_MATCH_1}")
else()
    set(DKR_RELEASE_BUILD_NUMBER "1")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(DKRPORT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "Root of the DKR Port repository")
option(DKR_RUNTIME_BUILD_GENERATED "Compile N64Recomp-generated DKR CPU functions" ON)
option(DKR_RUNTIME_BUILD_RT64 "Also configure and compile the pinned RT64 renderer" OFF)

get_filename_component(DKRPORT_ROOT "${DKRPORT_ROOT}" ABSOLUTE)
set(DKR_MODERN_RUNTIME_SOURCE "${DKRPORT_ROOT}/extern/n64-modern-runtime")
set(DKR_RECOMP_SOURCE "${DKR_MODERN_RUNTIME_SOURCE}/N64Recomp")
set(DKR_RT64_SOURCE "${DKRPORT_ROOT}/extern/rt64")
set(DKR_GENERATED_SOURCE "${CMAKE_CURRENT_LIST_DIR}/RecompiledFuncs")
set(DKR_GENERATED_RSP_SOURCE "${CMAKE_CURRENT_LIST_DIR}/RecompiledRSP")
set(DKR_GENERATED_FILE_COUNT 0)

if(NOT EXISTS "${DKR_MODERN_RUNTIME_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR
        "N64ModernRuntime has not been prepared at ${DKR_MODERN_RUNTIME_SOURCE}. "
        "Run Build-DKR-Runtime.cmd first.")
endif()
if(NOT EXISTS "${DKR_RECOMP_SOURCE}/include/recomp.h")
    message(FATAL_ERROR
        "The N64Recomp submodule pinned by N64ModernRuntime is missing at ${DKR_RECOMP_SOURCE}. "
        "Run Build-DKR-Runtime.cmd first.")
endif()

# N64ModernRuntime exports the ultramodern and librecomp targets used by
# production recompilation projects. It carries GPL-3.0 licensing; see the
# runtime preparation notice before distributing a linked executable.
add_subdirectory("${DKR_MODERN_RUNTIME_SOURCE}" "${CMAKE_BINARY_DIR}/n64-modern-runtime")

if(DKR_RUNTIME_BUILD_RT64)
    if(NOT EXISTS "${DKR_RT64_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR
            "RT64 has not been prepared at ${DKR_RT64_SOURCE}. "
            "Run Prepare-DKR-Runtime.cmd first.")
    endif()
    set(RT64_STATIC TRUE CACHE BOOL "Build RT64 as a static runtime dependency" FORCE)
    set(RT64_SDL_WINDOW_VULKAN TRUE CACHE BOOL "Allow SDL/Vulkan window integration" FORCE)
    add_compile_definitions(HLSL_CPU)
    add_subdirectory("${DKR_RT64_SOURCE}" "${CMAKE_BINARY_DIR}/rt64")
    if(MSVC AND TARGET rt64)
        # RT64 enables /MP internally. Ensure all compiler workers serialize
        # writes to the target PDB so clean RelWithDebInfo builds are reliable.
        target_compile_options(rt64 PRIVATE /FS)
    endif()
endif()

if(DKR_RUNTIME_BUILD_GENERATED)
    file(GLOB_RECURSE DKR_GENERATED_C CONFIGURE_DEPENDS "${DKR_GENERATED_SOURCE}/*.c")
    file(GLOB_RECURSE DKR_GENERATED_CXX CONFIGURE_DEPENDS "${DKR_GENERATED_SOURCE}/*.cpp")
    set(DKR_GENERATED_FILES ${DKR_GENERATED_C} ${DKR_GENERATED_CXX})
    list(LENGTH DKR_GENERATED_FILES DKR_GENERATED_FILE_COUNT)

    if(DKR_GENERATED_FILE_COUNT EQUAL 0)
        message(FATAL_ERROR
            "No N64Recomp output was found under ${DKR_GENERATED_SOURCE}. "
            "Run Prepare-DKR-Runtime.cmd without -SkipRecompile first.")
    endif()

    add_library(DKRRecompiledFuncs STATIC ${DKR_GENERATED_FILES})
    target_include_directories(DKRRecompiledFuncs PRIVATE
        "${DKRPORT_ROOT}/include"
        "${DKR_RECOMP_SOURCE}/include"
        "${DKR_MODERN_RUNTIME_SOURCE}/N64Recomp/include"
        "${DKR_MODERN_RUNTIME_SOURCE}/ultramodern/include"
        "${DKR_MODERN_RUNTIME_SOURCE}/librecomp/include"
        "${DKR_GENERATED_SOURCE}"
    )
    if(MSVC)
        target_compile_options(DKRRecompiledFuncs PRIVATE
            /FS
            /permissive-
            /wd4068  # Unknown non-MSVC pragmas emitted for floating-point behaviour.
            /wd4101  # Generated register temporaries can be unused after simplification.
            /wd4189
            /wd4244
            /wd4267
            /wd4702
        )
    else()
        target_compile_options(DKRRecompiledFuncs PRIVATE
            -fno-strict-aliasing
            -Wno-unused-but-set-variable
            -Wno-unused-variable
            -Wno-implicit-function-declaration
        )
    endif()
endif()

if(DKR_RUNTIME_BUILD_GENERATED)
    file(GLOB DKR_GENERATED_RSP CONFIGURE_DEPENDS "${DKR_GENERATED_RSP_SOURCE}/*.cpp")
    if(NOT DKR_GENERATED_RSP)
        message(FATAL_ERROR
            "No RSPRecomp output was found under ${DKR_GENERATED_RSP_SOURCE}. "
            "Run Generate-DKR-RSP.cmd first.")
    endif()
    set(DKR_GAME_SOURCES
        src/game/audio_equalizer.cpp
        src/game/dkr_save_codec.cpp
        src/game/game_main.cpp
        src/game/game_registration.cpp
        src/game/null_renderer.cpp
        src/game/presentation_identity.cpp
        src/game/renderer_snapshot.cpp
        src/game/runtime_enhancements.cpp
        src/game/runtime_audio_controls.cpp
        src/game/runtime_input.cpp
        src/game/runtime_magic_codes.cpp
        src/game/runtime_platform.cpp
        src/game/runtime_quick_restart.cpp
        src/game/save_manager.cpp
        src/game/runtime_stubs.cpp
        src/game/runtime_telemetry.cpp
        src/game/virtual_pak.cpp
        ${DKR_GENERATED_RSP}
    )
    if(APPLE)
        list(APPEND DKR_GAME_SOURCES src/game/runtime_metal_layer.mm)
    endif()
    if(DKR_RUNTIME_BUILD_RT64)
        list(APPEND DKR_GAME_SOURCES
            src/game/f3ddkr_rt64.cpp
            src/game/generated/jumpman_font.c
            src/game/generated/racing_banana_font.c
            src/game/rt64_renderer.cpp
            src/game/runtime_crt_overlay.cpp
            src/game/runtime_rice_texture_import.cpp
            src/game/runtime_texture_packs.cpp
            src/game/runtime_ui.cpp
            "${DKR_RT64_SOURCE}/src/contrib/imgui/backends/imgui_impl_sdlrenderer2.cpp"
        )
    endif()
    if(WIN32)
        list(APPEND DKR_GAME_SOURCES
            resources/dkr-r.rc
        )
    endif()
    add_executable(DKRPortGame ${DKR_GAME_SOURCES})
    target_include_directories(DKRPortGame PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
        "${DKR_RECOMP_SOURCE}/include"
        "${DKR_MODERN_RUNTIME_SOURCE}/N64Recomp/include"
        "${DKR_MODERN_RUNTIME_SOURCE}/ultramodern/include"
        "${DKR_MODERN_RUNTIME_SOURCE}/librecomp/include"
        "${DKR_GENERATED_SOURCE}"
        "${DKR_GENERATED_RSP_SOURCE}"
    )
    target_compile_definitions(DKRPortGame PRIVATE
        DKR_RUNTIME_HAS_RT64=$<BOOL:${DKR_RUNTIME_BUILD_RT64}>
        DKR_RELEASE_VERSION="${DKR_RELEASE_VERSION}"
        NOMINMAX
    )
    if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        # librecomp's generated RSP vector implementation uses SSE4.1
        # intrinsics on x86-64. GCC and Clang require the target feature to be
        # enabled explicitly, while MSVC exposes these intrinsics by default.
        target_compile_options(DKRPortGame PRIVATE -msse4.1)
    endif()
    target_link_libraries(DKRPortGame PRIVATE ultramodern librecomp DKRRecompiledFuncs)
    if(DKR_RUNTIME_BUILD_RT64 AND TARGET rt64)
        target_include_directories(DKRPortGame PRIVATE
            "$<TARGET_PROPERTY:rt64,INCLUDE_DIRECTORIES>"
            "${DKR_RT64_SOURCE}/src"
            "${DKR_RT64_SOURCE}/src/include"
            "${DKR_RT64_SOURCE}/src/contrib"
            "${DKR_RT64_SOURCE}/src/contrib/hlslpp/include"
            "${DKR_RT64_SOURCE}/src/contrib/plume"
            "${DKR_RT64_SOURCE}/src/contrib/imgui"
            "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-core/src/api"
            "${DKR_RT64_SOURCE}/src/contrib/nativefiledialog-extended/src/include"
            "${CMAKE_BINARY_DIR}/src"
        )
        target_link_libraries(DKRPortGame PRIVATE rt64)
        if(APPLE)
            target_include_directories(DKRPortGame PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/metal-cpp"
            )
        endif()
        if(WIN32)
            target_include_directories(DKRPortGame PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/include"
            )
            target_link_directories(DKRPortGame PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64"
            )
            target_link_libraries(DKRPortGame PRIVATE SDL2)
            add_custom_command(TARGET DKRPortGame POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxcompiler.dll"
                    "$<TARGET_FILE_DIR:DKRPortGame>/dxcompiler.dll"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxil.dll"
                    "$<TARGET_FILE_DIR:DKRPortGame>/dxil.dll"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
                    "$<TARGET_FILE_DIR:DKRPortGame>/SDL2.dll"
                VERBATIM
            )
        else()
            find_package(SDL2 REQUIRED)
            target_include_directories(DKRPortGame PRIVATE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(DKRPortGame PRIVATE ${SDL2_LIBRARIES})
        endif()
        add_custom_command(TARGET DKRPortGame POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/ui/Icons"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_LIST_DIR}/../assets/ui/Icons/DKR-R8.bmp"
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/ui/Icons/DKR-R8.bmp"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/filters"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_LIST_DIR}/../assets/filters"
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/filters"
            COMMENT "Staging the DKR-R launcher and graphics assets"
            VERBATIM
        )
    endif()
    if(WIN32)
        target_link_libraries(DKRPortGame PRIVATE user32 dbghelp)
    endif()
    set_target_properties(DKRPortGame PROPERTIES
        OUTPUT_NAME "DKR-R"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    if(APPLE)
        # CMake supplies the platform Info.plist and places the executable in
        # Contents/MacOS. RT64 selects its native Metal backend on Apple
        # platforms; the release packager adds artwork and notices before
        # applying an ad-hoc signature for local testing.
        set_target_properties(DKRPortGame PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "DKR-R"
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.thatguymcd.dkr-r"
            MACOSX_BUNDLE_INFO_STRING "Diddy Kong Racing - Recompiled"
            MACOSX_BUNDLE_LONG_VERSION_STRING "${DKR_RELEASE_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${DKR_RELEASE_VERSION_NUMERIC}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${DKR_RELEASE_BUILD_NUMBER}"
            MACOSX_BUNDLE_COPYRIGHT "Copyright 2026 ThatGuyMcd and DKR-R contributors"
        )
    endif()
    if(WIN32)
        set_target_properties(DKRPortGame PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(DKRPortGame PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}"
        )
    endforeach()
endif()

add_executable(DKRRuntimeProbe src/runtime_probe.cpp)
target_include_directories(DKRRuntimeProbe PRIVATE
    "${DKR_RECOMP_SOURCE}/include"
    "${DKR_MODERN_RUNTIME_SOURCE}/N64Recomp/include"
    "${DKR_MODERN_RUNTIME_SOURCE}/ultramodern/include"
    "${DKR_MODERN_RUNTIME_SOURCE}/librecomp/include"
)
target_compile_definitions(DKRRuntimeProbe PRIVATE
    DKR_RUNTIME_GENERATED_FILE_COUNT=${DKR_GENERATED_FILE_COUNT}
    DKR_RUNTIME_HAS_RT64=$<BOOL:${DKR_RUNTIME_BUILD_RT64}>
)
target_link_libraries(DKRRuntimeProbe PRIVATE ultramodern librecomp)
if(TARGET DKRRecompiledFuncs)
    target_link_libraries(DKRRuntimeProbe PRIVATE DKRRecompiledFuncs)
endif()
if(DKR_RUNTIME_BUILD_RT64 AND TARGET rt64)
    target_link_libraries(DKRRuntimeProbe PRIVATE rt64)
    if(WIN32)
        # rt64 is static and its SDL dependency is not exported to consumers.
        # Keep the validation probe linkable for aggregate/release builds just
        # like the production game target above.
        target_link_directories(DKRRuntimeProbe PRIVATE
            "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64"
        )
        target_link_libraries(DKRRuntimeProbe PRIVATE SDL2)
    endif()
endif()

set_target_properties(DKRRuntimeProbe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(DKRRuntimeProbe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}"
    )
endforeach()

add_executable(DKRRomHash src/rom_hash.cpp)
target_include_directories(DKRRomHash PRIVATE
    "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty"
)
set_target_properties(DKRRomHash PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(DKRRomHash PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}"
    )
endforeach()

if(BUILD_TESTING)
    add_executable(DKRPresentationPolicyTests
        tests/presentation_policy_tests.cpp
    )
    target_include_directories(DKRPresentationPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRPresentationPolicy COMMAND DKRPresentationPolicyTests)

    add_executable(DKRModernCameraPolicyTests
        tests/modern_camera_policy_tests.cpp
    )
    target_include_directories(DKRModernCameraPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRModernCameraPolicy COMMAND DKRModernCameraPolicyTests)

    add_executable(DKRWidescreenPolicyTests
        tests/widescreen_policy_tests.cpp
    )
    target_include_directories(DKRWidescreenPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRWidescreenPolicy COMMAND DKRWidescreenPolicyTests)

    add_executable(DKRViPresentationPolicyTests
        tests/vi_presentation_policy_tests.cpp
    )
    target_include_directories(DKRViPresentationPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRViPresentationPolicy COMMAND DKRViPresentationPolicyTests)

    add_executable(DKRMagicCodePolicyTests
        tests/magic_code_policy_tests.cpp
    )
    target_include_directories(DKRMagicCodePolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRMagicCodePolicy COMMAND DKRMagicCodePolicyTests)

    add_executable(DKRQuickRestartPolicyTests
        tests/quick_restart_policy_tests.cpp
    )
    target_include_directories(DKRQuickRestartPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRQuickRestartPolicy COMMAND DKRQuickRestartPolicyTests)

    add_executable(DKRCharacterSelectAnimationPolicyTests
        tests/character_select_animation_policy_tests.cpp
    )
    target_include_directories(DKRCharacterSelectAnimationPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRCharacterSelectAnimationPolicy
             COMMAND DKRCharacterSelectAnimationPolicyTests)

    add_executable(DKRCharacterSelectMusicPolicyTests
        tests/character_select_music_policy_tests.cpp
    )
    target_include_directories(DKRCharacterSelectMusicPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRCharacterSelectMusicPolicy
             COMMAND DKRCharacterSelectMusicPolicyTests)

    add_executable(DKRMotionSteeringPolicyTests
        tests/motion_steering_policy_tests.cpp
    )
    target_include_directories(DKRMotionSteeringPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRMotionSteeringPolicy COMMAND DKRMotionSteeringPolicyTests)

    add_executable(DKRSaveManagerTests
        tests/save_manager_tests.cpp
        src/game/dkr_save_codec.cpp
        src/game/save_manager.cpp
    )
    target_include_directories(DKRSaveManagerTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRSaveManager COMMAND DKRSaveManagerTests)

    add_executable(DKRSaveCodecTests
        tests/dkr_save_codec_tests.cpp
        src/game/dkr_save_codec.cpp
    )
    target_include_directories(DKRSaveCodecTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRSaveCodec COMMAND DKRSaveCodecTests)

    add_executable(DKRAudioEqualizerTests
        tests/audio_equalizer_tests.cpp
        src/game/audio_equalizer.cpp
    )
    target_include_directories(DKRAudioEqualizerTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRAudioEqualizer COMMAND DKRAudioEqualizerTests)

    add_executable(DKRAudioMixPolicyTests
        tests/audio_mix_policy_tests.cpp
    )
    target_include_directories(DKRAudioMixPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRAudioMixPolicy COMMAND DKRAudioMixPolicyTests)


    add_executable(DKRRendererSnapshotTests
        tests/renderer_snapshot_tests.cpp
        src/game/renderer_snapshot.cpp
    )
    target_include_directories(DKRRendererSnapshotTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRRendererSnapshot COMMAND DKRRendererSnapshotTests)

    add_executable(DKRPresentationIdentityTests
        tests/presentation_identity_tests.cpp
    )
    target_include_directories(DKRPresentationIdentityTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRPresentationIdentity COMMAND DKRPresentationIdentityTests)

    add_executable(DKRInterpolationStatePolicyTests
        tests/interpolation_state_policy_tests.cpp
    )
    target_include_directories(DKRInterpolationStatePolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRInterpolationStatePolicy
          COMMAND DKRInterpolationStatePolicyTests)

      add_executable(DKRRiceTexturePackPolicyTests
          tests/rice_texture_pack_policy_tests.cpp
      )
      target_include_directories(DKRRiceTexturePackPolicyTests PRIVATE
          "${CMAKE_CURRENT_LIST_DIR}/src/game"
      )
      add_test(NAME DKRRiceTexturePackPolicy
          COMMAND DKRRiceTexturePackPolicyTests)

      add_executable(DKRIntroTailPolicyTests
          tests/intro_tail_policy_tests.cpp
      )
      target_include_directories(DKRIntroTailPolicyTests PRIVATE
          "${CMAKE_CURRENT_LIST_DIR}/src/game"
      )
      add_test(NAME DKRIntroTailPolicy COMMAND DKRIntroTailPolicyTests)

    # These executables intentionally use assert() as their zero-dependency
    # test primitive. RelWithDebInfo normally defines NDEBUG, which used to turn
    # every assertion into a no-op in the exact configuration shipped to users.
    set(DKR_ASSERT_TEST_TARGETS
        DKRPresentationPolicyTests
        DKRModernCameraPolicyTests
        DKRWidescreenPolicyTests
        DKRViPresentationPolicyTests
        DKRMagicCodePolicyTests
        DKRQuickRestartPolicyTests
        DKRCharacterSelectAnimationPolicyTests
        DKRCharacterSelectMusicPolicyTests
        DKRMotionSteeringPolicyTests
        DKRSaveManagerTests
        DKRSaveCodecTests
        DKRAudioEqualizerTests
        DKRAudioMixPolicyTests
        DKRRendererSnapshotTests
        DKRPresentationIdentityTests
        DKRInterpolationStatePolicyTests
        DKRRiceTexturePackPolicyTests
        DKRIntroTailPolicyTests
    )
    foreach(DKR_ASSERT_TEST_TARGET IN LISTS DKR_ASSERT_TEST_TARGETS)
        if(MSVC)
            target_compile_options(${DKR_ASSERT_TEST_TARGET} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${DKR_ASSERT_TEST_TARGET} PRIVATE -UNDEBUG)
        endif()
    endforeach()
endif()
