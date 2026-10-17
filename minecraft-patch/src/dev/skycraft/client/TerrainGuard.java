package dev.skycraft.client;

import dev.skycraft.SkyCraft;
import dev.skycraft.link.SkyLink;
import dev.skycraft.world.SkyCollision;
import dev.skycraft.world.SkyTri;
import dev.skycraft.world.TriCollider;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.phys.AABB;

/** Recovery for incomplete sampled terrain; vanilla remains the normal movement controller. */
public final class TerrainGuard {
    private static LocalPlayer owner;
    private static int sequence, world;
    private static Vec3 previous, safe, held;
    private static long lastReport;
    private static int fallTicks;
    private static volatile long generation;
    private TerrainGuard() {}

    public static void tick(Minecraft minecraft, SkyLink.SkyState sky, boolean arriving) {
        LocalPlayer player = minecraft.player;
        if (player == null || arriving || !sky.inGame() || (sky.flags & 16) == 0) {
            if(owner!=null) ++generation;
            owner = null; previous = safe = held = null; return;
        }
        if (owner != player || sequence != sky.teleportSeq || world != sky.worldId) {
            owner = player; sequence = sky.teleportSeq; world = sky.worldId;
            ++generation;
            previous = player.position(); safe = held = null;
        }
        if ((sky.flags & 8) != 0) {
            if (held == null) held = previous != null ? previous : player.position();
            place(player, held);
            previous = held;
            return;
        }
        held = null;
        Vec3 pos = player.position();
        boolean flying = player.getAbilities().flying;
        // Gliding still lands on sampled ground, but a long glide over a
        // valley must never be pulled back to the last supported cliff edge.
        boolean gliding = player.isFallFlying();
        boolean wet = player.isInWater() || player.isSwimming();
        if(flying||wet||gliding) safe=null;
        if (!flying && !wet && previous != null) {
            // Query from the preceding tick so a fast downward step cannot put
            // the measured surface above the collision search before recovery.
            double ground = groundBelow(pos.x, previous.y, pos.z);
            if (GroundRecovery.crossedSurface(previous.y, pos.y, ground)) {
                pos = new Vec3(pos.x, ground + .002, pos.z);
                place(player, pos); sync(minecraft, player, pos);
                report("corrected a crossed sampled surface");
            } else if (safe != null && !gliding) {
                boolean blockBelow = false;
                for (int depth = 0; depth <= 4; depth++) {
                    BlockPos block = BlockPos.containing(pos.x, pos.y - depth, pos.z);
                    if (!player.level().getBlockState(block).getCollisionShape(player.level(), block).isEmpty()) {
                        blockBelow = true; break;
                    }
                }
                if (GroundRecovery.missingSupport(safe.y, pos.y,
                        Math.hypot(pos.x-safe.x, pos.z-safe.z), ground, flying, wet, blockBelow)) {
                    pos = safe;
                    place(player, pos); sync(minecraft, player, pos);
                    report("returned to last supported position while terrain was missing");
                }
            }
        }
        // Last resort: a long free fall with nothing below means the player went
        // under Witcher's land (a gap in sampled support, or a hole dug through
        // it). Put them back on the native surface above instead of the void.
        fallTicks = !flying && !gliding && !player.onGround() && player.getDeltaMovement().y < -.5 ? fallTicks + 1 : 0;
        if (fallTicks > 30 && !blockWithin(player, pos, 16) && Double.isNaN(groundBelow(pos.x, pos.y, pos.z))) {
            double above = surfaceAbove(pos.x, pos.y, pos.z);
            if (Double.isFinite(above) && above > pos.y + 2) {
                pos = new Vec3(pos.x, above + .002, pos.z);
                place(player, pos); sync(minecraft, player, pos);
                report("lifted a player who fell under the native surface");
            }
            fallTicks = 0;
        }
        if (player.onGround() && !flying && !wet) {
            // A mined Minecraft platform must not become a permanent invisible
            // checkpoint. Only native sampled support can serve as fallback.
            double ground=groundBelow(pos.x,pos.y,pos.z);
            safe=Double.isFinite(ground)&&Math.abs(pos.y-ground)<.2?pos:null;
        }
        previous = pos;
    }
    private static boolean blockWithin(LocalPlayer player, Vec3 pos, int depth) {
        for (int d = 0; d <= depth; d++) {
            BlockPos block = BlockPos.containing(pos.x, pos.y - d, pos.z);
            if (!player.level().getBlockState(block).getCollisionShape(player.level(), block).isEmpty()) return true;
        }
        return false;
    }
    private static double surfaceAbove(double x, double y, double z) {
        List<SkyTri> triangles = new ArrayList<>();
        SkyCollision.trianglesNear(new AABB(x - 1, y, z - 1, x + 1, y + 200, z + 1), triangles);
        return TriCollider.groundAt(triangles, x, y + 200, z, .15);
    }
    private static double groundBelow(double x,double y,double z) {
        List<SkyTri> triangles=new ArrayList<>();
        SkyCollision.trianglesNear(new AABB(x-1,y-24,z-1,x+1,y+1,z+1),triangles);
        return TriCollider.groundAt(triangles,x,y,z,.15);
    }
    private static void place(LocalPlayer player, Vec3 pos) {
        player.setPos(pos.x, pos.y, pos.z);
        player.xo=pos.x; player.yo=pos.y; player.zo=pos.z;
        player.setDeltaMovement(Vec3.ZERO); player.resetFallDistance();
    }
    private static void sync(Minecraft minecraft, LocalPlayer player, Vec3 pos) {
        var server=minecraft.getSingleplayerServer();
        if(server==null) return;
        var uuid=player.getUUID();
        var dimension=player.level().dimension();
        long expectedGeneration=generation;
        server.execute(() -> {
            var target=server.getPlayerList().getPlayer(uuid);
            if(target!=null&&generation==expectedGeneration&&target.level().dimension().equals(dimension)) {
                target.teleportTo(pos.x,pos.y,pos.z);target.setDeltaMovement(Vec3.ZERO);target.resetFallDistance();
            }
        });
    }
    private static void report(String text) {
        long now=System.currentTimeMillis();
        if(now-lastReport>2000) {SkyCraft.LOG.info("WitchCraft terrain recovery: {}",text);lastReport=now;}
    }
}
