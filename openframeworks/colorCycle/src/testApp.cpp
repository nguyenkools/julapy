#include "testApp.h"

//////////////////////////////////////////////
//	SETUP.
//////////////////////////////////////////////

void testApp :: setup()
{
	ofSetFrameRate( frameRate = 60 );
	ofSetVerticalSync( true );
	ofSetCircleResolution( 100 );

	cc.setScreenSize( ofGetWidth(), ofGetHeight() );
	cc.setup();
}

//////////////////////////////////////////////
//	UPDATE.
//////////////////////////////////////////////

void testApp :: update()
{
	cc.update();
}

//////////////////////////////////////////////
//	DRAW.
//////////////////////////////////////////////

void testApp :: draw()
{
	cc.draw();
}

//////////////////////////////////////////////
//	HANDLERS.
//////////////////////////////////////////////

void testApp :: keyPressed( int key )
{
	if( key == ' ' )
	{
		cc.togglePanel();
	}
}

void testApp :: keyReleased( int key )
{

}

void testApp :: mouseMoved( int x, int y )
{
	float gx = x / (float)ofGetWidth()  * 2 - 1;
	float gy = y / (float)ofGetHeight() * 2 - 1;
	
//	cc.setGravity( gx, gy );
}

void testApp :: mouseDragged( int x, int y, int button )
{
	cc.drag( x, y );
}

void testApp :: mousePressed( int x, int y, int button )
{
	cc.down( x, y );
}

void testApp :: mouseReleased( int x, int y, int button )
{
	cc.up( x, y );
}

void testApp :: windowResized( int w, int h )
{

}

