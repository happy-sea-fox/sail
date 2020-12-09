macro(sail_find_dependencies)
    find_package(JPEG)

    if (NOT JPEG_FOUND)
        return()
    endif()

    set(sail_${SAIL_CODEC_NAME}_libs ${JPEG_LIBRARIES})
    set(sail_${SAIL_CODEC_NAME}_include_dirs ${JPEG_INCLUDE_DIR})
endmacro()

macro(sail_codec_post_add)
    # Check for JPEG ICC functions that were added in libjpeg-turbo-1.5.90
    #
    cmake_push_check_state(RESET)
        set(CMAKE_REQUIRED_INCLUDES ${sail_jpeg_include_dirs})
        set(CMAKE_REQUIRED_LIBRARIES ${sail_jpeg_libs})

        check_c_source_compiles(
            "
            #include <stdio.h>
            #include <jpeglib.h>

            int main(int argc, char *argv[]) {
                jpeg_read_icc_profile(NULL, NULL, NULL);
                jpeg_write_icc_profile(NULL, NULL, 0);
                return 0;
            }
        "
        HAVE_JPEG_ICCP
        )
    cmake_pop_check_state()

    if (HAVE_JPEG_ICCP)
        set(CODEC_INFO_FEATURE_ICCP ";ICCP")
    endif()

    if (HAVE_JPEG_ICCP)
        target_compile_definitions(${TARGET} PRIVATE HAVE_JPEG_ICCP)
    endif()
endmacro()
