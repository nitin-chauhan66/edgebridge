cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(edgebridge_s3_phase0)

target_include_directories(app PRIVATE include)

target_sources(app PRIVATE
    src/main.c
    src/eb_crc.c
    src/eb_config.c
    src/eb_ring_queue.c
    src/eb_rs485.c
    src/eb_can.c
    src/eb_scanner.c
    src/eb_profile.c
    src/eb_storage.c
    src/eb_mqtt.c
    src/eb_ble_prov.c
    src/eb_web_analyzer.c
    src/eb_ota.c
)
