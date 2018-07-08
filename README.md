# Ogre_glTF
Library to use glTF 2.0 resources with Ogre 2.1 licencied under the terms of the [MIT licence](https://opensource.org/licenses/MIT "MIT Licence on the OSI website")

![DEMO](./demo.gif)

![DEMO](./demo2.gif)


Right now this is not intended to serve as a "plugin" for Ogre, but to be a little support library that permit you to use standard glTF files in an Ogre application.

In the current state, it is not using Ogre's resource management in any way, but is accessing files directly. Once the code gets more stable, I'll work on hooking the loading from Ogre's resource group manager.

The goal is to be able to load the geometry, the PBR material and the animations of an object from glTF and use Ogre's classes as if you just got the object as a .mesh from Ogre's resource manager.

The curent code is limitted into loading the first declared mesh in a glTF file, or, if the glTF file has multiple "secenes", and has a "default scene" value, it will take the mesh attached to the fist node of the default scene.

I'm wanting to get this functionality working inside my game engine (Annwvyn, a VR application developement framework that uses Ogre), and specifically I want to be able to use the glTF official Blender exporter.

The user facing API hasn't been worked on quite well, the only thing that has been cared on is to follow a pImpl (compillation firewall) pattern to hide to your code the dependencies in this library.

This library is based on tinygltf. https://github.com/syoyo/tinygltf. tinygltf itsefl vendor in some other opensource projects, like stb_image and a json loading library.

## How to use

 - Build the library with CMake
 - Point your compiler to the built library, and the public headers (the /include directory of the repository)
 - In your code:
```cpp
//Initialize the library. I recomend using a unique_ptr from #include <memory>
auto gltf = std::make_unique<Ogre_glTF>();

//Create an "adapter" object that permit to access the glb/gltf file
auto adapter = gltf->loadFile("Path/To/GLTF/GLB/Resource");

//Get an Ogre v2 "Item" from the adapter
Ogre::Item* ObjectItem = adapter.getItem(smgr);

//Now you can add this item to an node, and do everything you may want with it
//to cleanup the library, you just let go of the "gltf" smart pointer
```
 
 

## Current state

**At the moment, the code assume that you are using one gltf file to represent one "mesh". This is not a scene loader and doesn't support multiple objects per file**

This is a projet under developement. Here's a short todolist beofre this thing will be in an "usable" state:

 - [x] Load mesh infrmation (index and vertex buffer, including vertex coordinates, normal vectors, texture coordinates) and create an Ogre::Mesh out of it via Ogre::MeshManager and Ogre::VaoManager
 - [x] Load Image information from glTF into Ogre::TextureManager
 - [x] Load PBR material definition form glTF and create coresponding Ogre::HlmsPbsDatablock for them. (Ogre call PBR "PBS", more or less)

 At this point, the library will be able to load static geometry into Ogre.

 If this works, we can start tackeling animation data:
 - [x] Load "skin" information from glTF and create corresponding Ogre::Skeleton for the mesh
 - [x] Loop through all the vertex <-> bone assignement to get a valid skeleton configuration
 - [x] Load animation information and create animations from them
 - [ ] ~~Load mesh "target" information and create Ogre "morph" target from them~~ (Ogre 2.1 doesn't support them yet)


## Known issues

 - There's a problem with loading normal map data with the Direct 3D 11 render system of Ogre [issue #2](https://github.com/Ybalrid/Ogre_glTF/issues/2)
 - There's several little issues with the texture loading. A small refactor would help. See the TODO comments.
 - Library is not "installable" from CMakeLists.txt yet. Users need to get the .dll / .so file accessible to their program, and point their compiler to look for headers the "include" directory
 - Can only load one mesh and it's associated material in a file. Will either load the first one, of the fist node of the default scene, depending if the default scene is set
 - Library only has been tested on an handfull of glTF files, so some corner cases may make it not work.

## Building

First of all, locally clone this repository. It uses git submodules to get it's dependencies, so you just need to do 

```
git clone --recursive https://github.com/Ybalrid/Ogre_glTF
```

This project uses CMake. The CMake directory is a simple copy of every cmake script shipped in the Ogre SDK, to make things simpler.

You should get and install Ogre 2.1 from source on your system, 

To build the project, you need to have Ogre 2.1 build and "installed" somewhere. Windows users may need to set the `OGRE_HOME` variable.

then, do the folliwng (linux) :

```bash
cd build
cmake ..                        #execute CMake while pointing at the parent directory
make                            #build the library and the demo program

#to be able to run the demo program as-is, do the following:
cp -r <path to HLMS> .          #add the Hlms shader code that comes with Ogre
cp <path to ogre plugins>/* .   #add the necessary plugins (RenderSystem_GL3+)
```
On a typical install from Ogre's source code on linux, theses path are `/usr/local/share/OGRE/Media/Hlms` and `/usr/local/lib/OGRE/*`

(windows) :

 - Use cmake-gui to generate a Visual Studio solutution inside the `build` using the same version that you built Ogre with. You probably need to set the `OGRE_HOME` variable.
 - Open the .sln (solution) file into Visual Studio. You'll get 2 projects : `Ogre_glTF` (the DLL) and `Ogre_glTF_TEST` (a test program)
 - To make the test program works, copy inside the "build" directory all the .dll (and .pdb if you want to debug) files from Ogre's debug and release binary directories
 - Copy the HLMS libary to the "build" directory
 
 The "test" program is really crude and badly written, it was to validate taht some of the feautres where workign during developement.
 
 ## Contributors
 
 This project is Open Source. If you can improve it in any way, Pull Requests are welcommed!
 
 Here's the list of the contributors that hepled out with this projet:
 
  - Stig Atle @stig-atle


## Notes on third party components

tinygltf is an header only library. It is included in this very repository via git submodules.
If you are about to clone this repository, you should use `git clone --recursive`

The library define inside one of it's files the implementation of `tinygltf` and `stb_image`. This shouldn't be an issue and your program using ogre_glTF shouldn't be affected by them in any way. Everything is hidden inside a [pimpl](http://en.cppreference.com/w/cpp/language/pimpl)

If you have issues related with them, please open an issue :)
