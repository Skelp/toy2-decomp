cmake_minimum_required(VERSION 3.20.0)

set(PROJECT_NAME "toy2decomp")

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC runtime library flags are selected by an abstraction
cmake_policy(SET CMP0091 NEW)

project(${PROJECT_NAME} C CXX RC)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(CMAKE_HOST_UNIX AND MSVC)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_CURRENT_SOURCE_DIR}/tools/vc6-compile")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/patcher)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/include)

set(CMAKE_MSVC_RUNTIME_LIBRARY "")

# C++ flags
set(CMAKE_CXX_FLAGS "/W3 /GX /D \"WIN32\" /D \"_WINDOWS\"")
set(CMAKE_CXX_FLAGS_DEBUG "/Gm /MLd /Zi /Od /D \"_DEBUG\"")
set(CMAKE_CXX_FLAGS_RELEASE "/O2 /ML /D \"NDEBUG\"")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/Zi /ML /O2 /D \"NDEBUG\"")
set(CMAKE_CXX_FLAGS_MINSIZEREL "/Os /ML /D \"NDEBUG\"")

# EXE linker flags
set(CMAKE_EXE_LINKER_FLAGS "/machine:I386 /NODEFAULTLIB:libcp.lib /FIXED:NO")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/incremental:yes /debug")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/incremental:no")
set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/incremental:no /debug")
set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "/incremental:no")

# DLL linker flags
set(CMAKE_SHARED_LINKER_FLAGS "/machine:I386")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "/incremental:yes /debug")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "/incremental:no")
set(CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "/incremental:no /debug")
set(CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL "/incremental:no")


# Toy2 Sources
set(TOY2_SOURCES
    src/SinCosLUT.inc
    src/LibraryMSVC.h

    src/Logger.cpp
    src/Logger.h
    src/LoggerCannotCreateDeviceContextError.inc
    src/LoggerMissingOverlayDestinationError.inc
    src/LoggerNoZBufferHardwareError.inc
    src/LoggerSurfaceLostError.inc

    src/EventManager.cpp

    src/FileUtils.h

    src/Nullsub.cpp
    src/Nullsub.h

    src/Common.h
    src/Numerics.h
    src/Numerics.cpp

    src/DrawingDevice.h
    src/DrawingDevice.cpp

    src/ModeSelect.cpp
    src/ModeSelect.h

    src/Random.h
    src/Random.cpp

    src/SoftwareRenderer.cpp
    src/SoftwareRendererInternal.h
    src/SoftwareDeviceRaster.cpp
    src/SoftwareBackdrop.cpp
    src/SoftwareBackBuffer.cpp
    src/SoftwareDeviceQueue.cpp
    src/SoftwareRenderer.h
    src/SoftwareRendererDefaultPalette.inc

    src/SaveManager.cpp
    src/SaveManager.h

    src/InputTypes.h
    src/InputManager.h
    src/InputManager.cpp
    src/InputManagerKeyboardGlyphs.inc
    src/InputManagerKeyGlyphMappings.inc

    src/CharacterLoader.h
    src/CharacterLoader.cpp

    src/RawLoader.cpp
    src/RawLoader.h

    # Toy2
    src/Toy2/LevelSelect.cpp
    src/Toy2/LevelSelect.h
    src/Toy2/LevelLogic.h
    src/Toy2/Particles.h
    src/Toy2/TokenDialogue.h
    src/Toy2/CreditsText.inc
    src/Toy2/LaserAttackInstructions.inc
    src/Toy2/CameraInstructions.inc
    src/Toy2/PoleInstructions.inc
    src/Toy2/VisorInstructions.inc
    src/Toy2/StompInstructions.inc
    src/Toy2/LightPuzzleInstructions.inc
    src/Toy2/TarmacTroubleHint.inc
    src/Toy2/AndysHouse.cpp
    src/Toy2/AndysNeighborhood.cpp
    src/Toy2/BombsAway.cpp
    src/Toy2/ConstructionYard.cpp
    src/Toy2/AlleysAndGullies.cpp
    src/Toy2/AlleysAndGulliesHint.inc
    src/Toy2/AlsToyBarnHint.inc
    src/Toy2/SlimeTime.cpp
    src/Toy2/AlsToyBarn.cpp
    src/Toy2/HayBaleRideInstructions.inc
    src/Toy2/AlsSpaceLand.cpp
    src/Toy2/BarnEncounter.cpp
    src/Toy2/ElevatorHop.cpp
    src/Toy2/AlsPenthouseWaterButtonInstructions.inc
    src/Toy2/AlsPenthouseTrainSwitchInstructions.inc
    src/Toy2/AlsPenthouse.cpp
    src/Toy2/EvilEmperorZurg.cpp
    src/Toy2/AirportInfiltration.cpp
    src/Toy2/TarmacTrouble.cpp
    src/Toy2/FinalShowdown.cpp
    src/Toy2/Cfg.cpp
    src/Toy2/Ini.cpp
    src/Toy2/MoveableObject.cpp
    src/Toy2/Screens.cpp
    src/Toy2/Toy2.cpp
    src/Toy2/Toy2.h
    src/Toy2/MoveableObjectInternal.h
    src/Toy2/PoleRecord.h
    src/Toy2/IniKeywords.inc
    src/D3DApp/d3dapp.cpp
    src/D3DApp/d3dapp.h
    src/D3DApp/d3dappi.h
    src/D3DApp/d3dcalls.cpp
    src/D3DApp/ddcalls.cpp
    src/D3DApp/misc.cpp
    src/D3DApp/texture.cpp
    src/Toy2/Direct6.cpp
    src/Toy2/Direct6.h
    src/Toy2/Win95.cpp
    src/Toy2/Win95.h
    src/Toy2/Buzz.cpp
    src/Toy2/Buzz.h
    src/Toy2/BuzzAnimationEvents.inc
    src/Toy2/BuzzAnimationStates.inc
    src/Toy2/Actor.cpp
    src/Toy2/Actor.h
    src/Toy2/ActorAnimationFrameData.inc
    src/Toy2/BuzzLightPreset.h
    src/Toy2/Lighting.h
    src/Toy2/Levels.cpp
    src/Toy2/Levels.h
    src/Toy2/Object.cpp
    src/Toy2/Object.h
    src/Toy2/Collision.h
    src/Toy2/CollisionInternal.h
    src/Toy2/Collision.cpp
    src/Toy2/Collectables.cpp
    src/Toy2/Collectables.h
    src/Toy2/HiddenCollectible.h
    src/Toy2/KiteTail.cpp
    src/Toy2/KiteTail.h
    src/Toy2/MainMenu.cpp
    src/Toy2/MainMenu.h
    src/Toy2/Animation.h
    src/Toy2/Animation.cpp
    src/Toy2/Camera.h
    src/Toy2/Camera.cpp
    src/Toy2/Weather.cpp
    src/Toy2/Weather.h
    src/Toy2/TextureConversion.cpp

    # Nu3D
    src/Nu3D/Creature.h
    src/Nu3D/Link.h
    src/Nu3D/Link.cpp
    src/Nu3D/Material.h
    src/Nu3D/Material.cpp
    src/Nu3D/Nu3D.h
    src/Nu3D/Nu3D.cpp
    src/Nu3D/Profiler.cpp
    src/Nu3D/BmpDataNode.h
    src/Nu3D/BmpDataNode.cpp
    src/Nu3D/Patch.h
    src/Nu3D/Patch.cpp
    src/Nu3D/Portal.h
    src/Nu3D/Portal.cpp
    src/Nu3D/Scene.h
    src/Nu3D/Scene.cpp
    src/Nu3D/Primitive.h
    src/Nu3D/Primitive.cpp
    src/Nu3D/Sprite.h
    src/Nu3D/Viewport.h
    src/Nu3D/Viewport.cpp
    src/Nu3D/Font.h
    src/Nu3D/Font.cpp
    src/Nu3D/FMV.cpp
    src/Nu3D/HObjLoad.cpp
    src/Nu3D/DrawSurface.h
    src/Nu3D/DrawSurface.cpp
    src/Nu3D/Light.cpp
    src/Nu3D/Light.h
    src/Nu3D/Camera.cpp
    src/Nu3D/Camera.h
    src/Nu3D/SoftwareProjectionPoint.h
    src/Nu3D/Particles.h
    src/Nu3D/Particles.cpp
    src/Nu3D/ParticlePresets.inc
    src/Nu3D/ParticleTypes.inc
    src/Nu3D/Math.cpp
    src/Nu3D/Math.h
    src/Nu3D/NuMtx.cpp
    src/Nu3D/NuPlane.cpp
    src/Nu3D/NuVec.cpp
    src/Nu3D/ObjCore.cpp
    src/Nu3D/ObjLoad.cpp
    src/Nu3D/ObjLoad.h
    src/Nu3D/World.cpp

    # Renderer
    src/Renderer/Renderer.h
    src/Renderer/Renderer.cpp
    src/Renderer/DevDraw.cpp
    src/Renderer/TexturedQuad.h
    src/Renderer/RenderType.h
    src/Renderer/Sprite.h
    src/Renderer/Sprite.cpp
    src/Renderer/Vertices.h
    src/Renderer/Vertices.cpp
    src/Renderer/Shadows.cpp
    src/Renderer/Shadows.h
    src/Renderer/SpriteSheets.cpp
    src/Renderer/SpriteSheets.h
    src/Renderer/Glue.cpp
    src/Renderer/Glue.h

    # NGNLoader
    src/NGNLoader/NGNLoader.cpp
    src/NGNLoader/NGNLoader.h
    src/NGNLoader/NGNTypes.h

    # AudioManager
    src/AudioManager/AudioManager.cpp
    src/AudioManager/AudioManager.h
    src/AudioManager/DsVolTable.inc
    src/AudioManager/OneShotSoundPresets.inc
    src/AudioManager/PrimarySoundNames.inc
    src/AudioManager/SecondarySoundNames.inc
    src/AudioManager/SoundSequences.inc
)

set(TOY2_RETAIL_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/original/toy2.exe"
    CACHE FILEPATH "Retail executable that supplies the game resources"
)
set(TOY2_GENERATED_RESOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/resources")
set(TOY2_GENERATED_RESOURCE "${TOY2_GENERATED_RESOURCE_DIR}/toy2.res")
add_custom_target(toy2_resources
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TOY2_GENERATED_RESOURCE_DIR}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_resources.py"
        --input "${TOY2_RETAIL_EXECUTABLE}"
        --output "${TOY2_GENERATED_RESOURCE}"
    BYPRODUCTS "${TOY2_GENERATED_RESOURCE}"
    DEPENDS
        tools/extract_resources.py
        tools/decomp_binary.py
    COMMENT "Extract the retail resources"
    VERBATIM
)
set_source_files_properties("${TOY2_GENERATED_RESOURCE}" PROPERTIES
    EXTERNAL_OBJECT TRUE
    GENERATED TRUE
)

# Build the game
add_executable(toy2decomp WIN32 
    ${TOY2_SOURCES}

    "${TOY2_GENERATED_RESOURCE}"
)
add_dependencies(toy2decomp toy2_resources)

# Build the dynamically loaded testing dll(s). MODULE avoids declaring an
# import library output: patcher exports no symbols, so VC6 does not create one.
add_library(patcher MODULE
    patcher/main.cpp

    patcher/Loader.cpp
    patcher/Loader.h

    patcher/GamePatches.cpp
    patcher/GamePatches.h

    patcher/Utilities.h

    ${TOY2_SOURCES}
)

target_compile_definitions(patcher PRIVATE APPLY_FIXES)

target_link_directories(patcher PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/external/libs
    ${CMAKE_CURRENT_SOURCE_DIR}/external/libs/directx6
)
target_link_libraries(patcher PRIVATE minhook_x86 ddraw dsound dxguid winmm dinput)

target_link_directories(toy2decomp PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/external/libs/directx6
)
target_link_libraries(toy2decomp PRIVATE ddraw dsound dxguid winmm dinput)
set_target_properties(toy2decomp PROPERTIES OUTPUT_NAME "toy2")
