# game/sandbox: reference executable — isometric checkerboard demo (§6 repo layout).
add_executable(sandbox src/main.cpp)
target_link_libraries(sandbox PRIVATE eng::platform eng::iso2d eng::render_api eng::render_sdlgpu)
set_target_properties(sandbox PROPERTIES CXX_EXTENSIONS OFF)
_engine_warnings(sandbox)
