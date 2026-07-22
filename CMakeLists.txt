zephyr_library()

zephyr_library_sources(
    src/module.c
)

zephyr_library_include_directories(${ZMK_MODULE_DIR}/app/include)