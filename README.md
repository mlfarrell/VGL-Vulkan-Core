# VGL Vulkan Core
This is an open source distribution of the core that runs the Verto Studio Graphics Library (VGL) engine.
The design goals are firstmost to support the general purpose OpenGL-like engine that powers Verto Studio 3D, a cross-platform 3D modeling application.
Secondly, with the first goal in mind, the engine should offer reasonable high performance.  MoktenVK is a required target for this system, so certain Vulkan features will not be exposed/utilized.

Because this core is utilized by a real-world product, it may be less elegant than typical Vulkan tutorial code
or similair open source software.  Nonetheless, the motivations for providing this code are to aid in learning how to do
somewhat complicated tasks using Vulkan and how to provide general rendering APIs to service a non-game application such as 
a 3D modeling tool.

## Dependencies

You will need to have [SDL2](https://www.libsdl.org/download-2.0.php), [stb_image](https://github.com/nothings/stb/blob/master/stb_image.h), [linearalg](https://github.com/sgorsten/linalg) and the [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/).  
At the time of this release, the vesions I'm using are SDL2 2.0.9, stb_image 2.19, linearalg 2.1, and LunarG 1.1.73.0

Inside LunarG contains shaderc, which must be built as a shared DLL on windows.  Because that is a giant pain in the ass, I've included the interface lib and DLL for shaderc in this project.

## How to build example program

As of now, there's no separate .lib project for this core (yet).  I usually embed its source files inside the larger engine that builds it.  
An example program that shows basic core usage is contained within the example folder.

Once you have all your dependencies, place all the headers from (vulkansdk/include/vulkan, shaderc, linearalg, and SDL2) into example/include and the corresponding libs inside example/lib
Then, open the Visual Studio 2017 solution and build the project.

## License

This software is licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)
