idf_component_register(SRCS "sketch.cpp" INCLUDE_DIRS "../../main" REQUIRES arduino vl53l0x esp_pm app_update)
# The application consumes Arduino's C++ headers; do not inherit an older dialect.
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS YES)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LITTER_CONNECTED_STANDBY=${LC_CONNECTED})
