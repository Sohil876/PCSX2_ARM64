#--------------------------
# Find platform tools
#--------------------------
find_package(Git)
find_package(Threads REQUIRED)

# macOS workaround (avoid Mono.framework PNG)
set(FIND_FRAMEWORK_BACKUP ${CMAKE_FIND_FRAMEWORK})
set(CMAKE_FIND_FRAMEWORK NEVER)

# Platform-specific dependencies
if (WIN32)
    add_subdirectory(3rdparty/D3D12MemAlloc EXCLUDE_FROM_ALL)
    add_subdirectory(3rdparty/winpixeventruntime EXCLUDE_FROM_ALL)
    add_subdirectory(3rdparty/winwil EXCLUDE_FROM_ALL)
    set(FFMPEG_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/3rdparty/ffmpeg/include")
    find_package(Vtune)
elseif (ANDROID)
    foreach(dep IN ITEMS zlib zstd lz4 libwebp SDL3 harfbuzz freetype oboe plutosvg1)
        if(NOT TARGET ${dep})
            add_subdirectory(3rdparty/${dep} EXCLUDE_FROM_ALL)
        endif()
    endforeach()
    find_package(EGL REQUIRED)
    set(CUBEB_API ON)
else()
    find_package(CURL REQUIRED)
    find_package(PCAP REQUIRED)
    find_package(Vtune)

    # Prefer system ffmpeg, fallback to bundled includes
    find_package(FFMPEG COMPONENTS avcodec avformat avutil swresample swscale)
    if(NOT FFMPEG_FOUND)
        message(WARNING "FFmpeg not found, using bundled headers.")
        set(FFMPEG_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/3rdparty/ffmpeg/include")
    endif()

    include(CheckLib)

    if(UNIX AND NOT APPLE)
        if(LINUX)
            check_lib(LIBUDEV libudev libudev.h)
        endif()
        if(X11_API)
            find_package(X11 REQUIRED)
            if(NOT X11_Xrandr_FOUND)
                message(FATAL_ERROR "XRandR extension is required")
            endif()
        endif()
        if(WAYLAND_API)
            find_package(ECM REQUIRED NO_MODULE)
            list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")
            find_package(Wayland REQUIRED Egl)
        endif()
        if(USE_BACKTRACE)
            find_package(Libbacktrace REQUIRED)
        endif()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(DBUS REQUIRED dbus-1)
    endif()
endif()

set(CMAKE_FIND_FRAMEWORK ${FIND_FRAMEWORK_BACKUP})

#--------------------------
# 3rdparty submodules (guarded!)
#--------------------------

foreach(dep IN ITEMS fast_float rapidyaml lzma libchdr soundtouch simpleini imgui cpuinfo libzip rcheevos rapidjson discord-rpc freesurround)
    if(NOT TARGET ${dep})
        add_subdirectory(3rdparty/${dep} EXCLUDE_FROM_ALL)
    endif()
endforeach()

# Only try to disable warnings if the target exists
function(disable_warnings_if_exists target)
    if(TARGET ${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE "/W0")
        else()
            target_compile_options(${target} PRIVATE "-w")
        endif()
    endif()
endfunction()

foreach(target IN ITEMS libchdr cpuinfo cubeb speex)
    disable_warnings_if_exists(${target})
endforeach()

# Optional OpenGL and Vulkan
if(USE_OPENGL AND NOT TARGET glad)
    add_subdirectory(3rdparty/glad EXCLUDE_FROM_ALL)
endif()
if(USE_VULKAN AND NOT TARGET vulkan)
    add_subdirectory(3rdparty/vulkan EXCLUDE_FROM_ALL)
endif()

# Audio
if(NOT TARGET cubeb)
    add_subdirectory(3rdparty/cubeb EXCLUDE_FROM_ALL)
endif()

# Qt (example, adjust as needed)
# find_package(Qt6 6.7.3 COMPONENTS CoreTools Core GuiTools Gui WidgetsTools Widgets LinguistTools REQUIRED)

if(WIN32 AND NOT TARGET rainterface)
    add_subdirectory(3rdparty/rainterface EXCLUDE_FROM_ALL)
endif()

if(NOT TARGET demangler)
    add_subdirectory(3rdparty/demangler EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET ccc)
    add_subdirectory(3rdparty/ccc EXCLUDE_FROM_ALL)
endif()

# Architecture-specific
if(_M_X86 AND NOT TARGET zydis)
    add_subdirectory(3rdparty/zydis EXCLUDE_FROM_ALL)
elseif(_M_ARM64 AND NOT TARGET vixl)
    add_subdirectory(3rdparty/vixl EXCLUDE_FROM_ALL)
endif()

# fmt (always last for custom flags)
if(NOT TARGET fmt)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFMT_USE_EXCEPTIONS=0 -DFMT_USE_RTTI=0")
    add_subdirectory(3rdparty/fmt EXCLUDE_FROM_ALL)
endif()

if(MSVC)
    add_definitions("-D_CRT_NONSTDC_NO_WARNINGS" "-D_CRT_SECURE_NO_WARNINGS" "-DCRT_SECURE_NO_DEPRECATE")
endif()
