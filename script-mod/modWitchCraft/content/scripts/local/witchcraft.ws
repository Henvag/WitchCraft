// WitchCraft companion script mod.
//
// Minecraft owns the player while WitchCraft plays, so Witcher actors must be hit
// by Minecraft weapons and Geralt's damage must land on the Minecraft player.
// Health, damage and attitudes live in WitcherScript, so this mod reports nearby
// actors to the WitchCraft DLL and applies Minecraft's hits. It talks to the DLL
// through ClampF with a marker minimum (see host/ScriptBridge.h); without the
// DLL those calls are ordinary clamps and the mod stays inactive.

function WcCall( op : int, value : float ) : float
{
	return ClampF( value, -7777777.0, (float)op );
}

function WcConnected() : bool
{
	return WcCall( 1, 0.0 ) == 4242.0;
}

function WcPlayActive() : bool
{
	return WcConnected() && WcCall( 2, 0.0 ) == 1.0;
}

@addField(CR4Player)
var wcActors : array< CActor >;

// A stable small id per actor while it stays known (index + 1).
@addMethod(CR4Player)
function WcActorId( actor : CActor ) : int
{
	var i : int;
	if( !actor )
		return 0;
	i = wcActors.FindFirst( actor );
	if( i < 0 )
	{
		// Bound the table; Minecraft drops stand-ins whose ids stop being reported.
		if( wcActors.Size() >= 1000 )
			wcActors.Clear();
		wcActors.PushBack( actor );
		i = wcActors.Size() - 1;
	}
	return i + 1;
}

@addMethod(CR4Player)
function WcReportActors()
{
	var actors : array< CActor >;
	var actor : CActor;
	var npc : CNewNPC;
	var pos : Vector;
	var i, flags : int;

	actors = GetNPCsAndPlayersInRange( 32.0, 48, '', FLAG_ExcludePlayer + FLAG_OnlyAliveActors );
	WcCall( 10, (float)actors.Size() );
	for( i = 0; i < actors.Size(); i += 1 )
	{
		actor = actors[i];
		npc = (CNewNPC)actor;
		if( !actor || ( npc && npc.IsHorse() ) )
			continue;
		pos = actor.GetWorldPosition();
		flags = 0;
		if( actor.GetAttitude( this ) == AIA_Hostile )
			flags += 1;
		if( actor.IsImmortal() || actor.IsInvulnerable() )
			flags += 4;
		if( actor.IsInCombat() )
			flags += 8;
		WcCall( 11, (float)WcActorId( actor ) );
		WcCall( 12, pos.X );
		WcCall( 13, pos.Y );
		WcCall( 14, pos.Z );
		WcCall( 15, actor.GetHeading() );
		WcCall( 16, actor.GetHealthPercents() );
		WcCall( 17, (float)flags );
		WcCall( 19, 0.0 );
	}
	WcCall( 20, 0.0 );
}

// Minecraft damage (20 = a full Minecraft health bar) as a share of the
// actor's own maximum, so weapons take a similar number of hits on anyone.
@addMethod(CR4Player)
function WcApplyHits()
{
	var id, guard : int;
	var damage : float;
	var actor : CActor;
	var action : W3DamageAction;

	for( guard = 0; guard < 64; guard += 1 )
	{
		id = (int)WcCall( 40, 0.0 );
		if( id <= 0 )
			break;
		damage = WcCall( 41, 0.0 );
		if( id > wcActors.Size() )
			continue;
		actor = wcActors[id - 1];
		if( !actor || !actor.IsAlive() || damage <= 0 )
			continue;
		action = new W3DamageAction in theGame.damageMgr;
		action.Initialize( this, actor, NULL, "WitchCraft", EHRT_Light, CPS_AttackPower, true, false, false, false );
		action.AddDamage( theGame.params.DAMAGE_NAME_DIRECT, damage / 20.0 * 0.7 * actor.GetMaxHealth() );
		theGame.damageMgr.ProcessAction( action );
		delete action;
	}
}

@wrapMethod(CR4Player)
function CombatCheck( time : float , id : int )
{
	wrappedMethod( time, id );
	if( !WcConnected() )
		return;
	WcReportActors();
	WcApplyHits();
}

// While Minecraft plays, Geralt is only a hidden camera anchor: his damage
// goes to the Minecraft player and his own vitality is left untouched.
@wrapMethod(W3DamageManagerProcessor)
function ProcessActionDamage_DealDamage()
{
	var maxVitality, lost : float;
	var kind : int;

	if( action && action.victim == thePlayer && !action.GetSimulateOnly() && WcPlayActive() )
	{
		lost = action.processedDmg.vitalityDamage;
		maxVitality = thePlayer.GetStatMax( BCS_Vitality );
		if( lost > 0 && maxVitality > 0 )
		{
			kind = 3;
			if( action.IsActionMelee() )
				kind = 0;
			else if( action.IsActionRanged() )
				kind = 1;
			else if( action.IsActionWitcherSign() )
				kind = 2;
			WcCall( 30, lost / maxVitality );
			WcCall( 31, (float)thePlayer.WcActorId( (CActor)action.attacker ) );
			WcCall( 32, (float)kind );
		}
		action.processedDmg.vitalityDamage = 0;
		action.processedDmg.essenceDamage = 0;
	}
	wrappedMethod();
}
