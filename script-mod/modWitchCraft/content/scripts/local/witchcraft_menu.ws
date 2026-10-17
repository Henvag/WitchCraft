// WitchCraft main menu.
//
// tools/menu_scene replaces the Hearts of Stone menu video (the Minecraft
// player sits where Geralt did), so that menu is always the one shown, the
// Flash Witcher logo is hidden and the version label above the menu list is
// enlarged into a "WitchCraft" title.

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

@addField(CR4IngameMenu)
var wcTitlePlaced : bool;

@addMethod(CR4IngameMenu)
function WcShowTitle()
{
	var list, logo, label : CScriptedFlashSprite;
	var title : CScriptedFlashTextField;
	var scale : float;

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

	// txtVersion is a fixed 434x38 px field centred on the menu panel, just
	// below where the logo was. Larger text would be clipped, so the field
	// itself is scaled up around its centre and moved into the logo's place.
	title = m_flashModule.GetChildFlashTextField( "txtVersion" );
	if( title )
		title.SetTextHtml( "<p align='center'><font color='#F2E6C8'>WitchCraft</font></p>" );
	label = m_flashModule.GetChildFlashSprite( "txtVersion" );
	if( label && !wcTitlePlaced )
	{
		scale = 2.6;
		label.SetXScale( label.GetXScale() * scale );
		label.SetYScale( label.GetYScale() * scale );
		label.SetX( label.GetX() - 217.0 * ( scale - 1.0 ) );
		label.SetY( label.GetY() - 130.0 );
		wcTitlePlaced = true;
	}
}

// Runs when the main menu opens and whenever its root list is rebuilt.
@wrapMethod(CR4IngameMenu)
function PopulateMenuData()
{
	wrappedMethod();
	WcShowTitle();
}
