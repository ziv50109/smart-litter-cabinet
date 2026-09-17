idf_component_register(SRCS "sketch.cpp" INCLUDE_DIRS "../../main" REQUIRES arduino vl53l0x esp_pm app_update)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LITTER_CONNECTED_STANDBY=${LC_CONNECTED})
