// WitchCraft main menu.
//
// tools/menu_scene replaces the Hearts of Stone menu video (the Minecraft
// player sits where Geralt did) and swaps the WitchCraft logo into the menu's
// Flash file in place of the Witcher logo. This makes that menu the one always
// shown.

// The game normally picks the base, Hearts of Stone or Blood and Wine menu at
// random; only the Hearts of Stone video carries the Minecraft player.
@wrapMethod(CR4Game)
function PopulateMenuQueueMainAlways( out menus : array< name > )
{
	if( theGame.GetDLCManager().IsEP1Available() )
	{
		_mainMenuType = 1;
		menus.PushBack( 'CommonMainMenuEP1' );
		return;
	}
	wrappedMethod( menus );
}
