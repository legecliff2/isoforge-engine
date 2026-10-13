# render_sdlgpu: SDL_GPU backend (D-003, §2.1). render_api + platform + SDL3.
engine_add_module(render_sdlgpu
    SOURCES     src/sdlgpu_device.cpp
    PUBLIC_DEPS render_api
    PRIVATE_DEPS platform)
target_link_libraries(eng_render_sdlgpu PRIVATE SDL3::SDL3-static)
