// WitchCraft main menu.
//
// The menu background video is replaced by tools/menu_scene (the Minecraft
// player kneels where Geralt did and the WitchCraft logo is drawn into it), so
// the Flash Witcher logo above the menu list is hidden here. Without the
// replacement video this only removes the Witcher logo.

@addMethod(CR4IngameMenu)
function WcHideGameLogo()
{
	var list, logo : CScriptedFlashSprite;
	var title : CScriptedFlashTextField;

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
	// The version label sits where the logo was, on the menu's dark panel
	// (which would hide a logo drawn into the video), so it carries the name.
	title = m_flashModule.GetChildFlashTextField( "txtVersion" );
	if( title )
		title.SetTextHtml( "<p align='center'><font size='54' color='#F2E6C8'>WitchCraft</font></p>" );
}

// Runs when the main menu opens and whenever its root list is rebuilt.
@wrapMethod(CR4IngameMenu)
function PopulateMenuData()
{
	wrappedMethod();
	WcHideGameLogo();
}
