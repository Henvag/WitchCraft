cmake_minimum_required(VERSION 3.25)
project(WitchCraft LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(witchcraft_standin
	host/Link.cpp
	host/standin/main.cpp
)
target_include_directories(witchcraft_standin PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/protocol
	${CMAKE_CURRENT_SOURCE_DIR}/host
)
target_compile_definitions(witchcraft_standin PRIVATE
	NOMINMAX
	WIN32_LEAN_AND_MEAN
	UNICODE
	_UNICODE
)
if(MSVC)
	target_compile_options(witchcraft_standin PRIVATE /W4 /permissive-)
endif()

add_library(dinput8 SHARED
	host/proxy/dinput8.cpp
	host/Link.cpp
	host/Input.cpp
	host/ManualInput.cpp
	host/ManualGameInput.cpp
	host/Overlay.cpp
	host/World.cpp
	host/GameBridge.cpp
	host/Session.cpp
	host/NativeAdapter.cpp
	host/NativePilot.cpp
	host/NativeCollision.cpp
	host/Terrain.cpp
	host/Walls.cpp
	host/CollisionTiles.cpp
	host/TerrainVoxels.cpp
	host/DiagnosticSettle.cpp
	host/DiagnosticWalk.cpp
	host/DiagnosticMovement.cpp
	host/PlaySession.cpp
	host/DiagnosticWall.cpp
	host/DiagnosticAvatar.cpp
	host/proxy/dinput8.def
)
target_link_libraries(dinput8 PRIVATE d3d11 d3dcompiler dxgi)
target_include_directories(dinput8 PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/protocol
	${CMAKE_CURRENT_SOURCE_DIR}/host
)
target_compile_definitions(dinput8 PRIVATE
	NOMINMAX
	WIN32_LEAN_AND_MEAN
	UNICODE
	_UNICODE
)
set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")

if(MINGW)
    target_link_options(dinput8 PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(witchcraft_standin PRIVATE -static -static-libgcc -static-libstdc++)
endif()
include(CTest)
if(BUILD_TESTING)
    add_executable(witchcraft_session_tests tests/SessionTests.cpp host/Session.cpp)
    target_include_directories(witchcraft_session_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_session_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME session COMMAND witchcraft_session_tests)
    add_executable(witchcraft_bridge_tests tests/BridgeTests.cpp host/GameBridge.cpp)
    target_include_directories(witchcraft_bridge_tests PRIVATE host protocol)
    target_compile_definitions(witchcraft_bridge_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MINGW)
        target_link_options(witchcraft_bridge_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME bridge COMMAND witchcraft_bridge_tests)
    add_executable(witchcraft_native_layout_tests tests/NativeLayoutTests.cpp)
    target_include_directories(witchcraft_native_layout_tests PRIVATE host)
    if(MINGW)
        target_link_options(witchcraft_native_layout_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME native_layout COMMAND witchcraft_native_layout_tests)
    add_executable(witchcraft_terrain_tests tests/TerrainTests.cpp host/Terrain.cpp)
    target_include_directories(witchcraft_terrain_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_terrain_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME terrain COMMAND witchcraft_terrain_tests)
    add_executable(witchcraft_wall_tests tests/WallTests.cpp host/Walls.cpp)
    target_include_directories(witchcraft_wall_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_wall_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME walls COMMAND witchcraft_wall_tests)
    add_executable(witchcraft_collision_tiles_tests tests/CollisionTilesTests.cpp host/CollisionTiles.cpp host/TerrainVoxels.cpp)
    target_include_directories(witchcraft_collision_tiles_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_collision_tiles_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME collision_tiles COMMAND witchcraft_collision_tiles_tests)
    add_executable(witchcraft_diagnostic_settle_tests tests/DiagnosticSettleTests.cpp host/DiagnosticSettle.cpp)
    target_include_directories(witchcraft_diagnostic_settle_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_diagnostic_settle_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME diagnostic_settle COMMAND witchcraft_diagnostic_settle_tests)
    add_executable(witchcraft_diagnostic_walk_tests tests/DiagnosticWalkTests.cpp host/DiagnosticWalk.cpp host/DiagnosticSettle.cpp)
    target_include_directories(witchcraft_diagnostic_walk_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_diagnostic_walk_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME diagnostic_walk COMMAND witchcraft_diagnostic_walk_tests)
    add_executable(witchcraft_diagnostic_wall_tests tests/DiagnosticWallTests.cpp host/DiagnosticWall.cpp host/DiagnosticSettle.cpp host/Walls.cpp)
    target_include_directories(witchcraft_diagnostic_wall_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_diagnostic_wall_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME diagnostic_wall COMMAND witchcraft_diagnostic_wall_tests)
    add_executable(witchcraft_diagnostic_avatar_tests tests/DiagnosticAvatarTests.cpp host/DiagnosticAvatar.cpp host/DiagnosticSettle.cpp)
    target_include_directories(witchcraft_diagnostic_avatar_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_diagnostic_avatar_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME diagnostic_avatar COMMAND witchcraft_diagnostic_avatar_tests)
    add_executable(witchcraft_pilot_position_tests tests/PilotPositionTests.cpp)
    target_include_directories(witchcraft_pilot_position_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_pilot_position_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME pilot_position COMMAND witchcraft_pilot_position_tests)
    add_executable(witchcraft_manual_keys_tests tests/ManualKeysTests.cpp)
    target_include_directories(witchcraft_manual_keys_tests PRIVATE host)
    if(MINGW)
        target_link_options(witchcraft_manual_keys_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME manual_keys COMMAND witchcraft_manual_keys_tests)
    add_executable(witchcraft_manual_game_input_tests tests/ManualGameInputTests.cpp)
    target_include_directories(witchcraft_manual_game_input_tests PRIVATE host)
    target_compile_definitions(witchcraft_manual_game_input_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MINGW)
        target_link_options(witchcraft_manual_game_input_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME manual_game_input COMMAND witchcraft_manual_game_input_tests)
    add_executable(witchcraft_diagnostic_movement_tests tests/DiagnosticMovementTests.cpp host/DiagnosticMovement.cpp host/DiagnosticSettle.cpp)
    target_include_directories(witchcraft_diagnostic_movement_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_diagnostic_movement_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME diagnostic_movement COMMAND witchcraft_diagnostic_movement_tests)
    add_executable(witchcraft_diagnostic_jump_tests tests/DiagnosticJumpTests.cpp)
    target_include_directories(witchcraft_diagnostic_jump_tests PRIVATE host)
    if(MINGW)
        target_link_options(witchcraft_diagnostic_jump_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME diagnostic_jump COMMAND witchcraft_diagnostic_jump_tests)
    add_executable(witchcraft_play_tests tests/PlaySessionTests.cpp host/PlaySession.cpp host/DiagnosticSettle.cpp)
    target_include_directories(witchcraft_play_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_play_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME play_session COMMAND witchcraft_play_tests)
    add_executable(witchcraft_render_timing_tests tests/RenderTimingTests.cpp)
    target_include_directories(witchcraft_render_timing_tests PRIVATE host protocol)
    if(MINGW)
        target_link_options(witchcraft_render_timing_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME render_timing COMMAND witchcraft_render_timing_tests)
    add_executable(witchcraft_hud_blend_tests tests/HudBlendTests.cpp)
    target_include_directories(witchcraft_hud_blend_tests PRIVATE host)
    if(MINGW)
        target_link_options(witchcraft_hud_blend_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME hud_blend COMMAND witchcraft_hud_blend_tests)
    add_executable(witchcraft_hud_viewport_tests tests/HudViewportTests.cpp)
    target_include_directories(witchcraft_hud_viewport_tests PRIVATE host)
    if(MINGW)
        target_link_options(witchcraft_hud_viewport_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME hud_viewport COMMAND witchcraft_hud_viewport_tests)
    add_executable(witchcraft_native_probe tools/native_probe.cpp)
    target_compile_definitions(witchcraft_native_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MINGW)
        target_link_options(witchcraft_native_probe PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()
