target_sources(anoptic_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/resource_universe.c
        ${CMAKE_CURRENT_SOURCE_DIR}/cooker.c
        ${CMAKE_CURRENT_SOURCE_DIR}/executor.c
        ${CMAKE_CURRENT_SOURCE_DIR}/pack.c
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime.c
        ${CMAKE_CURRENT_SOURCE_DIR}/import/anogltf.c
        ${CMAKE_CURRENT_SOURCE_DIR}/import/gltf.c
        ${CMAKE_CURRENT_SOURCE_DIR}/import/image_codec.c
        ${CMAKE_CURRENT_SOURCE_DIR}/artifact/hash.c
)
