idf_component_register(SRCS "main.c" "llm.c"
                    INCLUDE_DIRS ""  LDFRAGMENTS "../linker.lf")



# Workaround for Internal Compiler Error in GCC during esp-dsp compilation
# https://github.com/espressif/esp-idf/issues/11696#issuecomment-1596208414
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-if-conversion)

idf_component_get_property(esp_dsp_lib espressif__esp-dsp COMPONENT_LIB)
if(esp_dsp_lib)
    target_compile_options(${esp_dsp_lib} PRIVATE -fno-if-conversion -fno-ira-share-save-slots -fno-ira-share-spill-slots)
endif()


# Create a SPIFFS image from the contents of the 'spiffs_image' directory
# that fits the partition named 'data'. FLASH_IN_PROJECT indicates that
# the generated image should be flashed when the entire project is flashed to
# the target with 'idf.py -p PORT flash'.
spiffs_create_partition_image(data ../data FLASH_IN_PROJECT)
