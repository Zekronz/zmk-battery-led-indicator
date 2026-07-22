zephyr_library()
zephyr_library_include_directories(${ZEPHYR_ZMK_MODULE_DIR}/app/include)

zephyr_library_sources(
    src/module.c
)