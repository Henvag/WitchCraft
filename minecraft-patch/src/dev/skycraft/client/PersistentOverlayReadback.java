package dev.skycraft.client;

import com.mojang.renderpearl.backend.opengl.GlTexture;
import dev.skycraft.SkyCraft;
import dev.skycraft.link.Proto;
import dev.skycraft.link.SkyLink;
import java.lang.foreign.MemorySegment;
import java.nio.ByteBuffer;
import net.minecraft.client.Minecraft;
import org.lwjgl.opengl.GL;
import static org.lwjgl.opengl.GL45C.*;

/** Persistent OpenGL readback for WitchCraft; the original exporter is the fallback. */
final class PersistentOverlayReadback {
    private static final Slot[] slots = {new Slot(),new Slot(),new Slot()};
    private static boolean disabled,logged;
    private static int width,height;
    private static long nextId=1,captured,published,skipped,logAt=System.nanoTime();
    private static final class Slot {
        int buffer;
        ByteBuffer mapped;
        long fence,id;
        boolean ready;
    }

    static boolean capture(Minecraft minecraft) {
        if(disabled || Boolean.getBoolean("witchcraft.legacyReadback")) return false;
        var target=minecraft.gameRenderer.mainRenderTarget();
        if(!(target.getColorTexture() instanceof GlTexture texture)) return false;
        var caps=GL.getCapabilities();
        if(!(caps.OpenGL45||(caps.GL_ARB_direct_state_access&&caps.GL_ARB_buffer_storage&&caps.GL_ARB_get_texture_sub_image))) {
            disabled=true;SkyCraft.LOG.info("WitchCraft: persistent readback extensions unavailable; using original exporter");return false;
        }
        int w=target.width,h=target.height;
        if(w<=0||h<=0||w>Proto.MAX_OVERLAY_W||h>Proto.MAX_OVERLAY_H) return false;
        try {
            if(w!=width||h!=height) {
                close();
                long bytes=(long)w*h*4;
                int flags=GL_MAP_READ_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT;
                for(Slot s:slots) {
                    s.buffer=glCreateBuffers();
                    glNamedBufferStorage(s.buffer,bytes,flags);
                    s.mapped=glMapNamedBufferRange(s.buffer,0,bytes,flags);
                    if(s.mapped==null) throw new IllegalStateException("persistent readback map failed");
                }
                width=w;height=h;
            }
            Slot newest=null;
            for(Slot s:slots) {
                if(s.fence!=0) {
                    int status=glClientWaitSync(s.fence,0,0); // Never wait on the render thread.
                    if(status==GL_WAIT_FAILED) throw new IllegalStateException("readback fence failed");
                    if(status==GL_ALREADY_SIGNALED||status==GL_CONDITION_SATISFIED) {
                        glDeleteSync(s.fence);s.fence=0;s.ready=true;
                    }
                }
                if(s.ready&&(newest==null||s.id>newest.id)) newest=s;
            }
            if(newest!=null) {
                MemorySegment shm=SkyLink.segment();
                if(shm!=null) {
                    MemorySegment.copy(MemorySegment.ofBuffer(newest.mapped),0,shm,SkyLink.overlayBackSlotOffset(),(long)width*height*4);
                    SkyLink.publishOverlay(width,height,true,newest.id);++published;
                }
                for(Slot s:slots) if(s.ready&&s.id<=newest.id) s.ready=false;
            }
            Slot free=null;
            for(Slot s:slots) if(s.fence==0&&!s.ready) {free=s;break;}
            if(free!=null) {
                // Restore all pixel-pack state touched here, so Minecraft's
                // command encoder sees exactly the bindings it had on entry.
                int binding=glGetInteger(GL_PIXEL_PACK_BUFFER_BINDING);
                int alignment=glGetInteger(GL_PACK_ALIGNMENT),row=glGetInteger(GL_PACK_ROW_LENGTH);
                int skipRows=glGetInteger(GL_PACK_SKIP_ROWS),skipPixels=glGetInteger(GL_PACK_SKIP_PIXELS);
                int imageHeight=glGetInteger(GL_PACK_IMAGE_HEIGHT),skipImages=glGetInteger(GL_PACK_SKIP_IMAGES);
                try {
                    glBindBuffer(GL_PIXEL_PACK_BUFFER,free.buffer);
                    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);
                    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
                    glPixelStorei(GL_PACK_IMAGE_HEIGHT,0);glPixelStorei(GL_PACK_SKIP_IMAGES,0);
                    glGetTextureSubImage(texture.glId(),0,0,0,0,width,height,1,GL_RGBA,GL_UNSIGNED_BYTE,width*height*4,0L);
                    free.fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
                    if(free.fence==0) throw new IllegalStateException("readback fence creation failed");
                    free.id=nextId++;++captured;
                    glFlush();
                } finally {
                    glPixelStorei(GL_PACK_ALIGNMENT,alignment);glPixelStorei(GL_PACK_ROW_LENGTH,row);
                    glPixelStorei(GL_PACK_SKIP_ROWS,skipRows);glPixelStorei(GL_PACK_SKIP_PIXELS,skipPixels);
                    glPixelStorei(GL_PACK_IMAGE_HEIGHT,imageHeight);glPixelStorei(GL_PACK_SKIP_IMAGES,skipImages);
                    glBindBuffer(GL_PIXEL_PACK_BUFFER,binding);
                }
            } else ++skipped;
            if(!logged) {SkyCraft.LOG.info("WitchCraft: persistent OpenGL HUD readback active, {}x{}",width,height);logged=true;}
            long now=System.nanoTime();
            if(now-logAt>=5_000_000_000L) {
                SkyCraft.LOG.info("WitchCraft readback: captured={} published={} skipped={} seconds={}",captured,published,skipped,(now-logAt)/1e9);
                captured=published=skipped=0;logAt=now;
            }
            return true;
        } catch(RuntimeException failure) {
            disabled=true;close();
            SkyCraft.LOG.error("WitchCraft: persistent readback disabled; using original exporter",failure);
            return false;
        }
    }
    private static void close() {
        for(Slot s:slots) {
            if(s.fence!=0) glDeleteSync(s.fence);
            if(s.mapped!=null&&s.buffer!=0) glUnmapNamedBuffer(s.buffer);
            if(s.buffer!=0) glDeleteBuffers(s.buffer);
            s.buffer=0;s.mapped=null;s.fence=0;s.ready=false;
        }
        width=height=0;
    }
}
