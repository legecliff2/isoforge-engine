# render_api: IRenderDevice, resources, command lists (§2.1, §5.1). Depends on base only.
engine_add_module(render_api
    SOURCES     src/device.cpp
    PUBLIC_DEPS base)
