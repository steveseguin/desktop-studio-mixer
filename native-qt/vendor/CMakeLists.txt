add_library(studio-rnnoise STATIC rnnoise/src/denoise.c rnnoise/src/rnn.c rnnoise/src/rnn_data.c
    rnnoise/src/rnn_reader.c rnnoise/src/pitch.c rnnoise/src/kiss_fft.c rnnoise/src/celt_lpc.c)
target_compile_definitions(studio-rnnoise PRIVATE COMPILE_OPUS RNNOISE_BUILD
    _celt_autocorr=studio_rn_autocorr _celt_lpc=studio_rn_lpc celt_iir=studio_rn_iir
    pitch_downsample=studio_rn_downsample pitch_search=studio_rn_search remove_doubling=studio_rn_remove_doubling)
target_include_directories(studio-rnnoise PUBLIC rnnoise/include)

add_library(studio-camera-queue STATIC virtualcam/shared-memory-queue.c virtualcam/tiny-nv12-scale.c)
target_include_directories(studio-camera-queue PUBLIC virtualcam)
add_library(desktop-studio-camera SHARED
    virtualcam/camera.def virtualcam/desktop-studio-camera.cpp virtualcam/virtualcam-filter.cpp virtualcam/placeholder.cpp virtualcam/sleepto.c
    virtualcam/dshow/source/output-filter.cpp virtualcam/dshow/source/dshow-base.cpp
    virtualcam/dshow/source/dshow-enum.cpp virtualcam/dshow/source/dshow-formats.cpp
    virtualcam/dshow/source/dshow-media-type.cpp virtualcam/dshow/source/log.cpp)
target_include_directories(desktop-studio-camera PRIVATE virtualcam virtualcam/dshow)
target_compile_definitions(desktop-studio-camera PRIVATE _UP_WINDOWS=1 _CRT_SECURE_NO_WARNINGS)
target_link_libraries(desktop-studio-camera PRIVATE studio-camera-queue strmiids winmm ole32 oleaut32 uuid advapi32 shell32 setupapi cfgmgr32)

if(MSVC)
 target_compile_options(studio-camera-queue PRIVATE /we4013)
 target_compile_options(studio-rnnoise PRIVATE /we4013)
endif()
