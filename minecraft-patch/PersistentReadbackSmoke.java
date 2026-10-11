import java.nio.ByteBuffer;
import org.lwjgl.opengl.GL;
import org.lwjgl.system.MemoryUtil;
import static org.lwjgl.opengl.GL45C.*;
import static org.lwjgl.sdl.SDLInit.*;
import static org.lwjgl.sdl.SDLVideo.*;

// Hidden, independent GL context: exercise the driver's persistent readback
// support without interacting with either running game's graphics context.
public final class PersistentReadbackSmoke {
    public static void main(String[] args) {
        if(!SDL_Init(SDL_INIT_VIDEO)) throw new AssertionError("SDL init failed");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        long window=SDL_CreateWindow("WitchCraft readback check",32,32,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        if(window==0) throw new AssertionError("hidden GL window failed");
        long context=SDL_GL_CreateContext(window);
        if(context==0||!SDL_GL_MakeCurrent(window,context)) throw new AssertionError("GL context failed");
        GL.createCapabilities();
        var caps=GL.getCapabilities();
        if(!(caps.OpenGL45||(caps.GL_ARB_direct_state_access&&caps.GL_ARB_buffer_storage&&caps.GL_ARB_get_texture_sub_image))) throw new AssertionError("Readback extensions unavailable");
        int texture=glCreateTextures(GL_TEXTURE_2D),buffer=glCreateBuffers();
        ByteBuffer original=MemoryUtil.memAlloc(64);
        try {
            glTextureStorage2D(texture,1,GL_RGBA8,4,4);
            int flags=GL_MAP_READ_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT;
            glNamedBufferStorage(buffer,64L,flags);
            ByteBuffer mapped=glMapNamedBufferRange(buffer,0,64,flags);
            if(mapped==null) throw new AssertionError("persistent map failed");
            // Reuse the same mapping across multiple GPU writes; vary every
            // pixel so stale frames, channel swaps and flipped rows are caught.
            for(int frame=0;frame<12;++frame) {
                for(int i=0;i<64;++i) original.put(i,(byte)(i*3+frame*17));
                glTextureSubImage2D(texture,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,original);
                glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);
                glGetTextureSubImage(texture,0,0,0,0,4,4,1,GL_RGBA,GL_UNSIGNED_BYTE,64,0L);
                long fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();
                long deadline=System.nanoTime()+2_000_000_000L;
                int status;
                do {status=glClientWaitSync(fence,0,0);Thread.onSpinWait();}
                while(status==GL_TIMEOUT_EXPIRED&&System.nanoTime()<deadline);
                glDeleteSync(fence);
                if(status!=GL_ALREADY_SIGNALED&&status!=GL_CONDITION_SATISFIED) throw new AssertionError("fence failed/timeout");
                for(int i=0;i<64;++i) if(mapped.get(i)!=original.get(i)) throw new AssertionError("pixel mismatch "+frame+":"+i);
                if(glGetError()!=GL_NO_ERROR) throw new AssertionError("GL error");
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glUnmapNamedBuffer(buffer);
            System.out.println("PASS: persistent coherent GPU readback, 12 frames, exact RGBA pixels; "+glGetString(GL_RENDERER));
        } finally {
            glDeleteBuffers(buffer);glDeleteTextures(texture);MemoryUtil.memFree(original);
            SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();
        }
    }
}
