import dev.skycraft.client.GroundRecovery;
public class GroundRecoveryTest {
    static void check(boolean ok) { if(!ok) throw new AssertionError(); }
    public static void main(String[] args) {
        check(GroundRecovery.crossedSurface(10.1,9.7,10));
        check(!GroundRecovery.crossedSurface(10.1,10.04,10));
        check(!GroundRecovery.crossedSurface(9,8.9,10)); // Existing ceiling is not ground.
        check(!GroundRecovery.crossedSurface(10,10.5,10)); // Jump remains vanilla.
        check(!GroundRecovery.crossedSurface(10,9.5,Double.NaN));
        check(GroundRecovery.missingSupport(10,9,1,Double.NaN,false,false,false));
        check(!GroundRecovery.missingSupport(10,9,1,7,false,false,false)); // Real lower surface.
        check(!GroundRecovery.missingSupport(10,9,1,Double.NaN,true,false,false));
        check(!GroundRecovery.missingSupport(10,9,1,Double.NaN,false,true,false));
        check(!GroundRecovery.missingSupport(10,9,1,Double.NaN,false,false,true));
        check(!GroundRecovery.missingSupport(10,9,50,Double.NaN,false,false,false));
        System.out.println("Ground recovery: crossing, missing support, legitimate fall, jump, flight, water and blocks passed");
    }
}
