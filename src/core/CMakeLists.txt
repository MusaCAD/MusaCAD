add_library(musacad_core STATIC
    version.cpp
    geometry_store.cpp
    native_kernel_2d.cpp
    spatial_grid.cpp
    entity_bounds.cpp
    osnap.cpp
    scene_snapshot.cpp
    geometry_engine.cpp)

add_library(musacad::core ALIAS musacad_core)

target_include_directories(musacad_core PUBLIC
    "${CMAKE_SOURCE_DIR}/include")

target_compile_definitions(musacad_core PRIVATE
    MUSACAD_VERSION="${PROJECT_VERSION}")

# std::jthread / std::condition_variable_any need the platform thread library.
find_package(Threads REQUIRED)
target_link_libraries(musacad_core PUBLIC Threads::Threads)

# musacad_core builds with warnings promoted to errors.
musacad_set_warnings(musacad_core AS_ERRORS)
musacad_enable_sanitizers(musacad_core)
