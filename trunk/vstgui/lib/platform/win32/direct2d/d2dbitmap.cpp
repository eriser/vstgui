//-----------------------------------------------------------------------------
// VST Plug-Ins SDK
// VSTGUI: Graphical User Interface Framework for VST plugins : 
//
// Version 4.0
//
//-----------------------------------------------------------------------------
// VSTGUI LICENSE
// (c) 2010, Steinberg Media Technologies, All Rights Reserved
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
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A  PARTICULAR PURPOSE ARE DISCLAIMED. 
// IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE 
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE  OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.
//-----------------------------------------------------------------------------

#include "d2dbitmap.h"

#if WINDOWS && VSTGUI_DIRECT2D_SUPPORT

#include "../win32support.h"
#include <wincodec.h>
#include <d2d1.h>
#include <assert.h>

#pragma comment (lib,"windowscodecs.lib") // this bumps client requirement to XP SP2, maybe we should use LoadLibrary

namespace VSTGUI {

//-----------------------------------------------------------------------------
class WICGlobal
{
public:
	static IWICImagingFactory* getFactory ();
protected:
	WICGlobal ();
	~WICGlobal ();
	IWICImagingFactory* factory;
};

//-----------------------------------------------------------------------------
D2DBitmap::D2DBitmap ()
: converter (0)
{
}

//-----------------------------------------------------------------------------
D2DBitmap::D2DBitmap (const CPoint& size)
: converter (0)
, size (size)
{
}

//-----------------------------------------------------------------------------
D2DBitmap::~D2DBitmap ()
{
	D2DBitmapCache::instance ()->removeBitmap (this);
	if (converter)
		converter->Release ();
}

//-----------------------------------------------------------------------------
IWICBitmapSource* D2DBitmap::getSource ()
{
	return converter;
}

//-----------------------------------------------------------------------------
bool D2DBitmap::load (const CResourceDescription& resourceDesc)
{
	if (converter)
		return true;

	IWICBitmapDecoder* decoder = 0;
	HRSRC rsrc = 0;
	if (resourceDesc.type == CResourceDescription::kIntegerType)
		rsrc = FindResourceA (GetInstance (), MAKEINTRESOURCEA (resourceDesc.u.id), "PNG");
	else
		rsrc = FindResourceA (GetInstance (), resourceDesc.u.name, "PNG");
	if (rsrc)
	{
		HGLOBAL resDataLoad = LoadResource (GetInstance (), rsrc);
		if (resDataLoad)
		{
			BYTE* resData = (BYTE*)LockResource (resDataLoad);
			DWORD resSize = SizeofResource (GetInstance (), rsrc);
			IWICStream* stream = 0;
			if (SUCCEEDED (WICGlobal::getFactory ()->CreateStream (&stream)))
			{
				if (SUCCEEDED (stream->InitializeFromMemory (resData, resSize)))
				{
					WICGlobal::getFactory ()->CreateDecoderFromStream (stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder);
				}
				stream->Release ();
			}
			FreeResource (resDataLoad);
		}
	}
	if (decoder)
	{
		IWICBitmapFrameDecode* frame;
		if (SUCCEEDED (decoder->GetFrame (0, &frame)))
		{
			UINT w = 0;
			UINT h = 0;
			frame->GetSize (&w, &h);
			size.x = w;
			size.y = h;
			WICGlobal::getFactory ()->CreateFormatConverter (&converter);
			if (converter)
			{
				if (!SUCCEEDED (converter->Initialize (frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeMedianCut)))
				{
					converter->Release ();
					converter = 0;
				}
			}
			frame->Release ();
		}
		decoder->Release ();
	}
	return converter != 0;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
D2DOffscreenBitmap::D2DOffscreenBitmap (const CPoint& size)
: D2DBitmap (size)
, bitmap (0)
{
	REFWICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
	WICBitmapCreateCacheOption options = WICBitmapCacheOnLoad;
	HRESULT hr = WICGlobal::getFactory ()->CreateBitmap ((UINT)size.x, (UINT)size.y, pixelFormat, options, &bitmap);
	assert (hr == S_OK);
}

//-----------------------------------------------------------------------------
IWICBitmapSource* D2DOffscreenBitmap::getSource ()
{
	return bitmap;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
ID2D1Bitmap* D2DBitmapCache::getBitmap (D2DBitmap* bitmap, ID2D1RenderTarget* renderTarget)
{
	std::map<D2DBitmap*, std::map<ID2D1RenderTarget*, ID2D1Bitmap*> >::iterator it = cache.find (bitmap);
	if (it != cache.end ())
	{
		std::map<ID2D1RenderTarget*, ID2D1Bitmap*>::iterator it2 = it->second.find (renderTarget);
		if (it2 != it->second.end ())
		{
			return it2->second;
		}
		ID2D1Bitmap* b = createBitmap (bitmap, renderTarget);
		if (b)
			it->second.insert (std::make_pair (renderTarget, b));
		return b;
	}
	std::pair<std::map<D2DBitmap*, std::map<ID2D1RenderTarget*, ID2D1Bitmap*> >::iterator, bool> insertSuccess = cache.insert (std::make_pair (bitmap, std::map<ID2D1RenderTarget*, ID2D1Bitmap*> ()));
	if (insertSuccess.second == true)
	{
		ID2D1Bitmap* b = createBitmap (bitmap, renderTarget);
		if (b)
		{
			insertSuccess.first->second.insert (std::make_pair (renderTarget, b));
			return b;
		}
	}
	return 0;
}

//-----------------------------------------------------------------------------
void D2DBitmapCache::removeBitmap (D2DBitmap* bitmap)
{
	std::map<D2DBitmap*, std::map<ID2D1RenderTarget*, ID2D1Bitmap*> >::iterator it = cache.find (bitmap);
	if (it != cache.end ())
	{
		std::map<ID2D1RenderTarget*, ID2D1Bitmap*>::iterator it2 = it->second.begin ();
		while (it2 != it->second.end ())
		{
			it2->second->Release ();
			it2++;
		}
		cache.erase (it);
	}
}

//-----------------------------------------------------------------------------
void D2DBitmapCache::removeRenderTarget (ID2D1RenderTarget* renderTarget)
{
	std::map<D2DBitmap*, std::map<ID2D1RenderTarget*, ID2D1Bitmap*> >::iterator it = cache.begin ();
	while (it != cache.end ())
	{
		std::map<ID2D1RenderTarget*, ID2D1Bitmap*>::iterator it2 = it->second.begin ();
		while (it2 != it->second.end ())
		{
			if (it2->first == renderTarget)
			{
				it2->second->Release ();
				it->second.erase (it2++);
			}
			else
				it2++;
		}
		it++;
	}
}

//-----------------------------------------------------------------------------
ID2D1Bitmap* D2DBitmapCache::createBitmap (D2DBitmap* bitmap, ID2D1RenderTarget* renderTarget)
{
	ID2D1Bitmap* d2d1Bitmap = 0; 
	renderTarget->CreateBitmapFromWicBitmap (bitmap->getSource (), &d2d1Bitmap);
	return d2d1Bitmap;
}

//-----------------------------------------------------------------------------
D2DBitmapCache::~D2DBitmapCache ()
{
#if DEBUG
	assert (cache.size () == 0);
#endif
}

//-----------------------------------------------------------------------------
D2DBitmapCache* D2DBitmapCache::instance ()
{
	static D2DBitmapCache gInstance;
	return &gInstance;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
WICGlobal::WICGlobal ()
: factory (0)
{
	HRESULT hr = CoCreateInstance (CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&factory);
}

//-----------------------------------------------------------------------------
WICGlobal::~WICGlobal ()
{
	if (factory)
		factory->Release ();
}

//-----------------------------------------------------------------------------
IWICImagingFactory* WICGlobal::getFactory ()
{
	static WICGlobal wicGlobal;
	return wicGlobal.factory;
}

} // namespace

#endif // WINDOWS
