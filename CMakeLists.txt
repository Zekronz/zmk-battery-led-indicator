zephyr_library()

zephyr_library_sources(
    src/module.c
)

if(DEFINED ZMK_MODULE_DIR)
    zephyr_library_include_directories(${ZMK_MODULE_DIR}/app/include)
elseif(DEFINED ZEPHYR_ZMK_MODULE_DIR)
    zephyr_library_include_directories(${ZEPHYR_ZMK_MODULE_DIR}/app/include)
else()
    # Fallback if building directly within the main ZMK app context
    zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../zmk/app/include)
endif()