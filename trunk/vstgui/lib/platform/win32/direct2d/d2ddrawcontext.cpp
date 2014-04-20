//-----------------------------------------------------------------------------
// VST Plug-Ins SDK
// VSTGUI: Graphical User Interface Framework for VST plugins
//
// Version 4.2
//
//-----------------------------------------------------------------------------
// VSTGUI LICENSE
// (c) 2013, Steinberg Media Technologies, All Rights Reserved
//-----------------------------------------------------------------------------
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
// 
//   * Redistributions of source code must retain the above copyright notice, 
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation 
//     and/or other materials provided with the distribution.
//   * Neither the name of the Steinberg Media Technologies nor the names of its
//     contributors may be used to endorse or promote products derived from this 
//     software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
// IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE 
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE  OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.
//-----------------------------------------------------------------------------

#include "d2ddrawcontext.h"

#if WINDOWS && VSTGUI_DIRECT2D_SUPPORT

#include "../win32support.h"
#include "d2dbitmap.h"
#include "d2dgraphicspath.h"
#include "d2dfont.h"

namespace VSTGUI {

//-----------------------------------------------------------------------------
static D2D1_MATRIX_3X2_F convert (const CGraphicsTransform& t)
{
	D2D1_MATRIX_3X2_F matrix;
	matrix._11 = static_cast<FLOAT> (t.m11);
	matrix._12 = static_cast<FLOAT> (t.m12);
	matrix._21 = static_cast<FLOAT> (t.m21);
	matrix._22 = static_cast<FLOAT> (t.m22);
	matrix._31 = static_cast<FLOAT> (t.dx);
	matrix._32 = static_cast<FLOAT> (t.dy);
	return matrix;
}

//-----------------------------------------------------------------------------
D2DDrawContext::D2DApplyClip::D2DApplyClip (D2DDrawContext* drawContext)
: drawContext (drawContext)
{
	drawContext->getRenderTarget ()->PushAxisAlignedClip (makeD2DRect (drawContext->currentState.clipRect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	if (drawContext->getCurrentTransform ().isInvariant () == false)
		drawContext->getRenderTarget ()->SetTransform (convert (drawContext->getCurrentTransform ()));
}

//-----------------------------------------------------------------------------
D2DDrawContext::D2DApplyClip::~D2DApplyClip ()
{
	drawContext->getRenderTarget ()->PopAxisAlignedClip ();
	drawContext->getRenderTarget ()->SetTransform (D2D1::Matrix3x2F::Identity ());
}

//-----------------------------------------------------------------------------
D2DDrawContext::D2DDrawContext (HWND window, const CRect& drawSurface)
: COffscreenContext (drawSurface)
, window (window)
, renderTarget (0)
, fillBrush (0)
, strokeBrush (0)
, fontBrush (0)
, strokeStyle (0)
{
	createRenderTarget ();
}

//-----------------------------------------------------------------------------
D2DDrawContext::D2DDrawContext (D2DBitmap* inBitmap)
: COffscreenContext (new CBitmap (inBitmap))
, window (0)
, renderTarget (0)
, fillBrush (0)
, strokeBrush (0)
, fontBrush (0)
, strokeStyle (0)
{
	createRenderTarget ();
	bitmap->forget ();
}

//-----------------------------------------------------------------------------
D2DDrawContext::~D2DDrawContext ()
{
	releaseRenderTarget ();
}

//-----------------------------------------------------------------------------
void D2DDrawContext::createRenderTarget ()
{
	if (window)
	{
		RECT rc;
		GetClientRect (window, &rc);

		D2D1_SIZE_U size = D2D1::SizeU (rc.right - rc.left, rc.bottom - rc.top);
		ID2D1HwndRenderTarget* hwndRenderTarget = 0;
//		D2D1_RENDER_TARGET_TYPE targetType = D2D1_RENDER_TARGET_TYPE_DEFAULT;
		D2D1_RENDER_TARGET_TYPE targetType = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
		D2D1_PIXEL_FORMAT pixelFormat = D2D1::PixelFormat (DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED);
		HRESULT hr = getD2DFactory ()->CreateHwndRenderTarget (D2D1::RenderTargetProperties (targetType, pixelFormat), D2D1::HwndRenderTargetProperties (window, size, D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS), &hwndRenderTarget);
		if (SUCCEEDED (hr))
		{
			hwndRenderTarget->SetDpi (96.0, 96.0);
			renderTarget = hwndRenderTarget;
		}
	}
	else if (bitmap)
	{
		D2DBitmap* d2dBitmap = dynamic_cast<D2DBitmap*> (bitmap->getPlatformBitmap ());
		if (d2dBitmap)
		{
			D2D1_RENDER_TARGET_TYPE targetType = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
			D2D1_PIXEL_FORMAT pixelFormat = D2D1::PixelFormat (DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED);
			getD2DFactory ()->CreateWicBitmapRenderTarget (d2dBitmap->getBitmap (), D2D1::RenderTargetProperties (targetType, pixelFormat), &renderTarget);
		}
	}
	assert (renderTarget);
	init ();
}

//-----------------------------------------------------------------------------
void D2DDrawContext::releaseRenderTarget ()
{
	if (fillBrush)
	{
		fillBrush->Release ();
		fillBrush = 0;
	}
	if (strokeBrush)
	{
		strokeBrush->Release ();
		strokeBrush = 0;
	}
	if (fontBrush)
	{
		fontBrush->Release ();
		fontBrush = 0;
	}
	if (strokeStyle)
	{
		strokeStyle->Release ();
		strokeStyle = 0;
	}
	if (renderTarget)
	{
		D2DBitmapCache::instance ()->removeRenderTarget (renderTarget);
		renderTarget->Release ();
		renderTarget = 0;
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::beginDraw ()
{
	if (renderTarget)
	{
		renderTarget->BeginDraw ();
		renderTarget->SetTransform (D2D1::Matrix3x2F::Identity ());
 	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::endDraw ()
{
	if (renderTarget)
	{
		renderTarget->Flush ();
		HRESULT result = renderTarget->EndDraw ();
		if (result == D2DERR_RECREATE_TARGET)
		{
			releaseRenderTarget ();
			createRenderTarget ();
		}
		if (bitmap)
		{
			D2DBitmap* d2dBitmap = dynamic_cast<D2DBitmap*> (bitmap->getPlatformBitmap ());
			D2DBitmapCache::instance ()->removeBitmap (d2dBitmap);
		}
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::init ()
{
	COffscreenContext::init ();
}

//-----------------------------------------------------------------------------
CGraphicsPath* D2DDrawContext::createGraphicsPath ()
{
	return new D2DGraphicsPath ();
}

//-----------------------------------------------------------------------------
CGraphicsPath* D2DDrawContext::createTextPath (const CFontRef font, UTF8StringPtr text)
{
 	const D2DFont* ctFont = dynamic_cast<const D2DFont*>(font->getPlatformFont ());
 	return ctFont ? new D2DGraphicsPath (ctFont, text) : 0;
}

//-----------------------------------------------------------------------------
void D2DDrawContext::drawGraphicsPath (CGraphicsPath* _path, PathDrawMode mode, CGraphicsTransform* t)
{
	if (renderTarget == 0)
		return;

	D2DGraphicsPath* d2dPath = dynamic_cast<D2DGraphicsPath*> (_path);
	if (d2dPath == 0)
		return;

	ID2D1PathGeometry* path = d2dPath->getPath (mode == kPathFilledEvenOdd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
	if (path)
	{
		D2DApplyClip ac (this);

		ID2D1Geometry* geometry = 0;
		if (t)
		{
			ID2D1TransformedGeometry* tg = 0;
			getD2DFactory ()->CreateTransformedGeometry (path, convert (*t), &tg);
			geometry = tg;
		}
		else
		{
			geometry = path;
			geometry->AddRef ();
		}

		getRenderTarget ()->SetTransform (D2D1::Matrix3x2F::Translation ((FLOAT)getOffset ().x, (FLOAT)getOffset ().y));

		if (mode == kPathFilled || mode == kPathFilledEvenOdd)
			getRenderTarget ()->FillGeometry (geometry, getFillBrush ());
		else if (mode == kPathStroked)
			getRenderTarget ()->DrawGeometry (geometry, getStrokeBrush (), (FLOAT)getLineWidth (), getStrokeStyle ());

		getRenderTarget ()->SetTransform (D2D1::Matrix3x2F::Identity ());

		geometry->Release ();
	}
}

//-----------------------------------------------------------------------------
ID2D1GradientStopCollection* D2DDrawContext::createGradientStopCollection (const D2DGradient* d2dGradient) const
{
	ID2D1GradientStopCollection* collection = 0;
	D2D1_GRADIENT_STOP* gradientStops = new D2D1_GRADIENT_STOP [d2dGradient->getColorStops ().size ()];
	uint32_t index = 0;
	for (CGradient::ColorStopMap::const_iterator it = d2dGradient->getColorStops ().begin (); it != d2dGradient->getColorStops ().end (); ++it, ++index)
	{
		gradientStops[index].position = (FLOAT)it->first;
		gradientStops[index].color = D2D1::ColorF (it->second.red/255.f, it->second.green/255.f, it->second.blue/255.f, it->second.alpha/255.f * currentState.globalAlpha);
	}
	getRenderTarget ()->CreateGradientStopCollection (gradientStops, static_cast<UINT32> (d2dGradient->getColorStops ().size ()), &collection);
	delete [] gradientStops;
	return collection;
}

//-----------------------------------------------------------------------------
void D2DDrawContext::fillLinearGradient (CGraphicsPath* _path, const CGradient& gradient, const CPoint& startPoint, const CPoint& endPoint, bool evenOdd, CGraphicsTransform* t)
{
	if (renderTarget == 0)
		return;

	D2DGraphicsPath* d2dPath = dynamic_cast<D2DGraphicsPath*> (_path);
	const D2DGradient* d2dGradient = dynamic_cast<const D2DGradient*> (&gradient);
	if (d2dPath == 0 || d2dGradient == 0)
		return;

	ID2D1PathGeometry* path = d2dPath->getPath (evenOdd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
	if (path)
	{
		D2DApplyClip ac (this);

		ID2D1Geometry* geometry = 0;
		if (t)
		{
			ID2D1TransformedGeometry* tg = 0;
			getD2DFactory ()->CreateTransformedGeometry (path, convert (*t), &tg);
			geometry = tg;
		}
		else
		{
			geometry = path;
			geometry->AddRef ();
		}

		ID2D1GradientStopCollection* collection = createGradientStopCollection (d2dGradient);
		if (collection)
		{
			ID2D1LinearGradientBrush* brush = 0;
			D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES properties;
			properties.startPoint = makeD2DPoint (startPoint);
			properties.endPoint = makeD2DPoint (endPoint);
			if (SUCCEEDED (getRenderTarget ()->CreateLinearGradientBrush (properties, collection, &brush)))
			{
				getRenderTarget ()->SetTransform (D2D1::Matrix3x2F::Translation ((FLOAT)getOffset ().x, (FLOAT)getOffset ().y));
				getRenderTarget ()->FillGeometry (geometry, brush);
				getRenderTarget ()->SetTransform (D2D1::Matrix3x2F::Identity ());
				brush->Release ();
			}
			collection->Release ();
		}
		geometry->Release ();
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::fillRadialGradient (CGraphicsPath* _path, const CGradient& gradient, const CPoint& center, CCoord radius, const CPoint& originOffset, bool evenOdd, CGraphicsTransform* t)
{
	if (renderTarget == 0)
		return;

	D2DGraphicsPath* d2dPath = dynamic_cast<D2DGraphicsPath*> (_path);
	const D2DGradient* d2dGradient = dynamic_cast<const D2DGradient*> (&gradient);
	if (d2dPath == 0 || d2dGradient == 0)
		return;

	ID2D1PathGeometry* path = d2dPath->getPath (evenOdd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
	if (path)
	{
		D2DApplyClip ac (this);

		ID2D1Geometry* geometry = 0;
		if (t)
		{
			ID2D1TransformedGeometry* tg = 0;
			getD2DFactory ()->CreateTransformedGeometry (path, convert (*t), &tg);
			geometry = tg;
		}
		else
		{
			geometry = path;
			geometry->AddRef ();
		}
		ID2D1GradientStopCollection* collection = createGradientStopCollection (d2dGradient);
		if (collection)
		{
			// brush properties
			ID2D1RadialGradientBrush* brush = 0;
			D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES properties;
			properties.center = makeD2DPoint (center);
			properties.gradientOriginOffset = makeD2DPoint (originOffset);
			properties.radiusX = (FLOAT)radius;
			properties.radiusY = (FLOAT)radius;

			if (SUCCEEDED (getRenderTarget ()->CreateRadialGradientBrush (properties, collection, &brush)))
			{
				getRenderTarget ()->SetTransform (D2D1::Matrix3x2F::Translation ((FLOAT)getOffset ().x, (FLOAT)getOffset ().y));
				getRenderTarget ()->FillGeometry (geometry, brush);
				getRenderTarget ()->SetTransform (D2D1::Matrix3x2F::Identity ());
				brush->Release ();
			}
			collection->Release ();
		}
		geometry->Release ();
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::clearRect (const CRect& rect)
{
	if (renderTarget)
	{
		CRect oldClip = currentState.clipRect;
		setClipRect (rect);
		D2DApplyClip ac (this);
		renderTarget->Clear (D2D1::ColorF (1.f, 1.f, 1.f, 0.f));
		setClipRect (oldClip);
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::drawBitmap (CBitmap* bitmap, const CRect& dest, const CPoint& offset, float alpha)
{
	D2DBitmap* d2dBitmap = bitmap->getPlatformBitmap () ? dynamic_cast<D2DBitmap*> (bitmap->getPlatformBitmap ()) : 0;
	if (renderTarget && d2dBitmap)
	{
		if (d2dBitmap->getSource ())
		{
			ID2D1Bitmap* d2d1Bitmap = D2DBitmapCache::instance ()->getBitmap (d2dBitmap, renderTarget);
			if (d2d1Bitmap)
			{
				D2DApplyClip clip (this);
				CRect d (dest);
				d.offset (currentState.offset.x, currentState.offset.y);
				d.makeIntegral ();
				CRect source (dest);
				source.offset (-source.left, -source.top);
				source.offset (offset.x, offset.y);
				D2D1_RECT_F sourceRect = makeD2DRect (source);
				renderTarget->DrawBitmap (d2d1Bitmap, makeD2DRect (d), alpha * currentState.globalAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &sourceRect);
			}
		}
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::drawLine (const LinePair& line)
{
	if (renderTarget)
	{
		CPoint penLocation (line.first);
		penLocation.offset (currentState.offset.x, currentState.offset.y);
		CPoint p (line.second);
		p.offset (currentState.offset.x, currentState.offset.y);
		if (currentState.drawMode.integralMode ())
		{
			p.makeIntegral ();
			penLocation.makeIntegral ();
		}

		D2DApplyClip clip (this);
		if ((((int32_t)currentState.frameWidth) % 2))
			renderTarget->SetTransform (D2D1::Matrix3x2F::Translation (0.f, -0.5f));
		renderTarget->DrawLine (makeD2DPoint (penLocation), makeD2DPoint (p), strokeBrush, (FLOAT)currentState.frameWidth, strokeStyle);
		renderTarget->SetTransform (D2D1::Matrix3x2F::Identity ());
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::drawLines (const LineList& lines)
{
	if (lines.size () == 0)
		return;
	VSTGUI_RANGE_BASED_FOR_LOOP(LineList, lines, LinePair, line)
		drawLine (line);
	VSTGUI_RANGE_BASED_FOR_LOOP_END
}

//-----------------------------------------------------------------------------
void D2DDrawContext::drawPolygon (const PointList& polygonPointList, const CDrawStyle drawStyle)
{
	if (polygonPointList.size () == 0)
		return;
	if (renderTarget)
	{
		D2DApplyClip clip (this);
		D2DGraphicsPath path;
		path.beginSubpath (polygonPointList[0]);
		for (uint32_t i = 1; i < polygonPointList.size (); ++i)
		{
			path.addLine (polygonPointList[i]);
		}
		if (drawStyle == kDrawFilled || drawStyle == kDrawFilledAndStroked)
		{
			drawGraphicsPath (&path, kPathFilled);
		}
		if (drawStyle == kDrawStroked || drawStyle == kDrawFilledAndStroked)
		{
			drawGraphicsPath (&path, kPathStroked);
		}
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::drawRect (const CRect &_rect, const CDrawStyle drawStyle)
{
	if (renderTarget)
	{
		CRect rect (_rect);
		rect.offset (currentState.offset.x, currentState.offset.y);
		rect.normalize ();
		if (currentState.drawMode.integralMode ())
		{
			rect.makeIntegral ();
		}
		D2DApplyClip clip (this);
		if (drawStyle == kDrawFilled || drawStyle == kDrawFilledAndStroked)
		{
			renderTarget->FillRectangle (makeD2DRect (rect), fillBrush);
		}
		if (drawStyle == kDrawStroked || drawStyle == kDrawFilledAndStroked)
		{
			rect.left++;
			rect.bottom--;
			if ((((int32_t)currentState.frameWidth) % 2))
				renderTarget->SetTransform (D2D1::Matrix3x2F::Translation (0.f, 0.5f));
			renderTarget->DrawRectangle (makeD2DRect (rect), strokeBrush, (FLOAT)currentState.frameWidth, strokeStyle);
			renderTarget->SetTransform (D2D1::Matrix3x2F::Identity ());
		}
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::drawArc (const CRect& _rect, const float _startAngle, const float _endAngle, const CDrawStyle drawStyle)
{
	CGraphicsPath* path = createGraphicsPath ();
	if (path)
	{
		path->addArc (_rect, _startAngle, _endAngle, false);
		if (drawStyle == kDrawFilled || drawStyle == kDrawFilledAndStroked)
			drawGraphicsPath (path, kPathFilled);
		if (drawStyle == kDrawStroked || drawStyle == kDrawFilledAndStroked)
			drawGraphicsPath (path, kPathStroked);
		path->forget ();
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::drawEllipse (const CRect &_rect, const CDrawStyle drawStyle)
{
	if (renderTarget)
	{
		CRect rect (_rect);
		rect.offset (currentState.offset.x, currentState.offset.y);
		rect.normalize ();
		D2DApplyClip clip (this);
		CPoint center (rect.getTopLeft ());
		center.offset (rect.getWidth () / 2., rect.getHeight () / 2.);
		D2D1_ELLIPSE ellipse;
		ellipse.point = makeD2DPoint (center);
		ellipse.radiusX = (FLOAT)(rect.getWidth () / 2.);
		ellipse.radiusY = (FLOAT)(rect.getHeight () / 2.);
		if (drawStyle == kDrawFilled || drawStyle == kDrawFilledAndStroked)
		{
			renderTarget->FillEllipse (ellipse, fillBrush);
		}
		if (drawStyle == kDrawStroked || drawStyle == kDrawFilledAndStroked)
		{
			renderTarget->DrawEllipse (ellipse, strokeBrush, (FLOAT)currentState.frameWidth, strokeStyle);
		}
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::drawPoint (const CPoint &point, const CColor& color)
{
	saveGlobalState ();
	setLineWidth (1);
	setFrameColor (color);
	CPoint point2 (point);
	point2.h++;
	moveTo (point);
	lineTo (point2);
	restoreGlobalState ();
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setLineStyle (const CLineStyle& style)
{
	if (strokeStyle && currentState.lineStyle == style)
		return;
	setLineStyleInternal (style);
	COffscreenContext::setLineStyle (style);
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setLineStyleInternal (const CLineStyle& style)
{
	if (strokeStyle)
	{
		strokeStyle->Release ();
		strokeStyle = 0;
	}
	D2D1_STROKE_STYLE_PROPERTIES properties;
	switch (style.getLineCap ())
	{
		case CLineStyle::kLineCapButt: properties.startCap = properties.endCap = properties.dashCap = D2D1_CAP_STYLE_FLAT; break;
		case CLineStyle::kLineCapRound: properties.startCap = properties.endCap = properties.dashCap = D2D1_CAP_STYLE_ROUND; break;
		case CLineStyle::kLineCapSquare: properties.startCap = properties.endCap = properties.dashCap = D2D1_CAP_STYLE_SQUARE; break;
	}
	switch (style.getLineJoin ())
	{
		case CLineStyle::kLineJoinMiter: properties.lineJoin = D2D1_LINE_JOIN_MITER; break;
		case CLineStyle::kLineJoinRound: properties.lineJoin = D2D1_LINE_JOIN_ROUND; break;
		case CLineStyle::kLineJoinBevel: properties.lineJoin = D2D1_LINE_JOIN_BEVEL; break;
	}
	properties.dashOffset = (FLOAT)style.getDashPhase ();
	properties.miterLimit = 10.f;
	if (style.getDashCount ())
	{
		properties.dashStyle = D2D1_DASH_STYLE_CUSTOM;
		FLOAT* lengths = new FLOAT[style.getDashCount ()];
		for (uint32_t i = 0; i < style.getDashCount (); i++)
			lengths[i] = (FLOAT)style.getDashLengths ()[i];
		getD2DFactory ()->CreateStrokeStyle (properties, lengths, style.getDashCount (), &strokeStyle);
		delete [] lengths;
	}
	else
	{
		properties.dashStyle = D2D1_DASH_STYLE_SOLID;
		getD2DFactory ()->CreateStrokeStyle (properties, 0, 0, &strokeStyle);
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setLineWidth (CCoord width)
{
	if (currentState.frameWidth == width)
		return;
	COffscreenContext::setLineWidth (width);
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setDrawMode (CDrawMode mode)
{
	if (currentState.drawMode != mode)
	{
		if (renderTarget)
		{
			if (mode == kAntiAliasing)
				renderTarget->SetAntialiasMode (D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			else
				renderTarget->SetAntialiasMode (D2D1_ANTIALIAS_MODE_ALIASED);
		}
	}
	COffscreenContext::setDrawMode (mode);
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setClipRect (const CRect &clip)
{
	COffscreenContext::setClipRect (clip);
}

//-----------------------------------------------------------------------------
void D2DDrawContext::resetClipRect ()
{
	COffscreenContext::resetClipRect ();
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setFillColor (const CColor& color)
{
	if (currentState.fillColor == color)
		return;
	setFillColorInternal (color);
	COffscreenContext::setFillColor (color);
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setFrameColor (const CColor& color)
{
	if (currentState.frameColor == color)
		return;
	setFrameColorInternal (color);
	COffscreenContext::setFrameColor (color);
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setFontColor (const CColor& color)
{
	if (currentState.fontColor == color)
		return;
	setFontColorInternal (color);
	COffscreenContext::setFontColor (color);
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setFillColorInternal (const CColor& color)
{
	if (fillBrush)
	{
		fillBrush->Release ();
		fillBrush = 0;
	}
	if (renderTarget)
	{
		D2D1_COLOR_F d2Color = {color.red/255.f, color.green/255.f, color.blue/255.f, (color.alpha/255.f) * currentState.globalAlpha};
		renderTarget->CreateSolidColorBrush (d2Color, &fillBrush);
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setFrameColorInternal (const CColor& color)
{
	if (strokeBrush)
	{
		strokeBrush->Release ();
		strokeBrush = 0;
	}
	if (renderTarget)
	{
		D2D1_COLOR_F d2Color = {color.red/255.f, color.green/255.f, color.blue/255.f, (color.alpha/255.f) * currentState.globalAlpha};
		renderTarget->CreateSolidColorBrush (d2Color, &strokeBrush);
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setFontColorInternal (const CColor& color)
{
	if (fontBrush)
	{
		fontBrush->Release ();
		fontBrush = 0;
	}
	if (renderTarget)
	{
		D2D1_COLOR_F d2Color = {color.red/255.f, color.green/255.f, color.blue/255.f, (color.alpha/255.f) * currentState.globalAlpha};
		renderTarget->CreateSolidColorBrush (d2Color, &fontBrush);
	}
}

//-----------------------------------------------------------------------------
void D2DDrawContext::setGlobalAlpha (float newAlpha)
{
	if (currentState.globalAlpha == newAlpha)
		return;
	COffscreenContext::setGlobalAlpha (newAlpha);
	setFrameColorInternal (currentState.frameColor);
	setFillColorInternal (currentState.fillColor);
	setFontColorInternal (currentState.fontColor);
}

//-----------------------------------------------------------------------------
void D2DDrawContext::saveGlobalState ()
{
	COffscreenContext::saveGlobalState ();
}

//-----------------------------------------------------------------------------
void D2DDrawContext::restoreGlobalState ()
{
	CColor prevFillColor = currentState.fillColor;
	CColor prevFrameColor = currentState.frameColor;
	CColor prevFontColor = currentState.fontColor;
	CLineStyle prevLineStye = currentState.lineStyle;
	float prevAlpha = currentState.globalAlpha;
	COffscreenContext::restoreGlobalState ();
	if (prevAlpha != currentState.globalAlpha)
	{
		float prevAlpha = currentState.globalAlpha;
		currentState.globalAlpha = -1.f;
		setGlobalAlpha (prevAlpha);
	}
	else
	{
		if (prevFillColor != currentState.fillColor)
		{
			setFillColorInternal (currentState.fillColor);
		}
		if (prevFrameColor != currentState.fillColor)
		{
			setFrameColorInternal (currentState.frameColor);
		}
		if (prevFontColor != currentState.fillColor)
		{
			setFontColorInternal (currentState.fontColor);
		}
		if (prevLineStye != currentState.lineStyle)
		{
			setLineStyleInternal (currentState.lineStyle);
		}
	}
}

} // namespace

#endif // WINDOWS
