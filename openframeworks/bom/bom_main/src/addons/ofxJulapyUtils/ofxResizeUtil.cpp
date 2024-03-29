/*
 *  ofxResizeUtil.cpp
 *  openFrameworks
 *
 *  Created by lukasz karluk on 19/09/09.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#include "ofxResizeUtil.h"

ofRectangle ofxResizeUtil :: cropToSize( const ofRectangle& srcRect, const ofRectangle& dstRect )
{
	float wRatio, hRatio, scale;
	
	wRatio = dstRect.width  / (float)srcRect.width;
	hRatio = dstRect.height / (float)srcRect.height;
	
	scale = MAX( wRatio, hRatio );
	
	ofRectangle			rect;
	rect.x		= ( dstRect.width  - ( srcRect.width  * scale ) ) * 0.5;
	rect.y		= ( dstRect.height - ( srcRect.height * scale ) ) * 0.5;
	rect.width	= srcRect.width  * scale;
	rect.height	= srcRect.height * scale;
	
	return rect;
}

ofRectangle ofxResizeUtil :: fitToSize( const ofRectangle& srcRect, const ofRectangle& dstRect )
{
	float wRatio, hRatio, scale;
	
	wRatio = dstRect.width  / (float)srcRect.width;
	hRatio = dstRect.height / (float)srcRect.height;
	
	scale = MIN( wRatio, hRatio );
	
	ofRectangle			rect;
	rect.x		= ( dstRect.width  - ( srcRect.width  * scale ) ) * 0.5;
	rect.y		= ( dstRect.height - ( srcRect.height * scale ) ) * 0.5;
	rect.width	= srcRect.width  * scale;
	rect.height	= srcRect.height * scale;
	
	return rect;
}