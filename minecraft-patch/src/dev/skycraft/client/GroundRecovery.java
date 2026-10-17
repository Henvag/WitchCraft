package dev.skycraft.client;

/** Decisions kept independent of Minecraft so recorded collision gaps can be tested. */
public final class GroundRecovery {
    private GroundRecovery() {}
    public static boolean crossedSurface(double previousY, double currentY, double ground) {
        return Double.isFinite(previousY) && Double.isFinite(currentY) && Double.isFinite(ground)
            && currentY < previousY && previousY >= ground - .15 && currentY < ground - .04;
    }
    public static boolean missingSupport(double safeY, double currentY, double distance,
                                         double ground, boolean flying, boolean wet, boolean blockBelow) {
        return !flying && !wet && !blockBelow && Double.isNaN(ground)
            && Double.isFinite(safeY) && Double.isFinite(currentY) && Double.isFinite(distance)
            && distance <= 8 && safeY - currentY > .75;
    }
}
