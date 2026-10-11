# WitchCraft Minecraft readback patch

Targets the installed SkyCraft 0.1.2 JAR (SHA-256
EA429C73DCEC49D39B6A0BB9B17AD08A6B0290A39A96CCF6F6C07D581CF992D9)
and Minecraft 26.3 / Java 25. Protocol v11 and unrelated mod entries stay intact.
FrameExporter.java is from chasmlol/SkyCraft commit
bfcaf178524b92c2cdeb88e4ce0f13ef9ded6f32, with one dispatch added.
Upstream: https://github.com/chasmlol/SkyCraft
Its MIT license is preserved in LICENSE-SkyCraft.

PersistentOverlayReadback uses three coherent, persistently mapped OpenGL pixel
pack buffers. It polls GPU fences without waiting and publishes the newest ready
image. When all slots are pending it skips capture, keeping the render thread
available. Pixel-pack bindings/state are restored. Allocation is repeated only
after dimensions change. Unsupported backends/OpenGL versions use the original
exporter; OpenGL 3.3 contexts are supported when the direct-state-access,
buffer-storage and texture-sub-image extensions are present. A runtime failure
disables the new path and logs fallback. Launch Java
with -Dwitchcraft.legacyReadback=true for the original exporter.

Run build.ps1 with the original JAR, or -BaseMod pointing at the backed-up original
after installation. It compiles against the local Prism libraries and verifies
every unrelated original JAR entry remains byte-identical. The result is
dist/minecraft/skycraft-0.1.2.jar. Do not install a second copy beside the original;
back up and replace the instance's skycraft-0.1.2.jar while Minecraft is stopped.

PersistentReadbackSmoke.java tested the same persistent-map/fence/texture-copy
operations in independent hidden OpenGL 4.5 and 3.3 contexts on the GTX 1080 Ti: twelve
changing RGBA frames matched byte-for-byte. This validates driver support, not
integration performance. Native CTest: 19 passing suites, including resized HUD
orientation, pixel mapping and blending. Live Minecraft activation and fresh-frame
rate remain to be checked after installation.
