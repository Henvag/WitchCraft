// WitchCraft main menu.
//
// tools/menu_scene replaces the Hearts of Stone menu video: the Minecraft
// player sits where Geralt did and the WitchCraft logo is drawn beside the
// menu panel. This makes that menu the one always shown and hides the Flash
// Witcher logo above the menu list.

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

@addMethod(CR4IngameMenu)
function WcHideGameLogo()
{
	var list, logo : CScriptedFlashSprite;

	if( !isMainMenu || !m_flashModule )
		return;
	list = m_flashModule.GetChildFlashSprite( "menuListModule" );
	if( list )
		logo = list.GetChildFlashSprite( "mcGameLogo" );
	if( logo )
	{
		logo.SetVisible( false );
		logo.SetAlpha( 0 );
	}
}

// Runs when the main menu opens and whenever its root list is rebuilt.
@wrapMethod(CR4IngameMenu)
function PopulateMenuData()
{
	wrappedMethod();
	WcHideGameLogo();
}
