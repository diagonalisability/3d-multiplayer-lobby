# 3D Multiplayer Lobby
Clients connect to a server and send position updates so that they can see each other move. This is a work-in-progress. [See a demo!](https://davidjacewicz.com/project-thumbnails/3d-multiplayer-lobby.mp4)

## Dependencies:
- A C++17 compiler, like GCC or Clang
- [GLFW 3](https://www.glfw.org)
- [GLM](https://github.com/g-truc/glm)
- Linux Epoll (most recent Linux kernels probably have this)
- [`stb_image.h`](https://github.com/nothings/stb/blob/master/stb_image.h)
- [`tinyobjloader`](https://github.com/tinyobjloader/tinyobjloader)
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator): either modify the Makefile or set the environment variable `VULKAN_MEMORY_ALLOCATOR_INCLUDE_PATH` to the path to the `include` directory of this project

## Build
```
make
```

## Run server
```
make run-server
```

## Run client(s)
```
make run
```

There's no neat way to change any settings yet, like the server IP address. The default is for the client(s) to connect to localhost. You should be able to connect arbitrarily many clients to the same server.
