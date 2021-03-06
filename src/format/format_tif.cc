/****************************************************************************
 *
 *      tifHandler.cc: Tag Image File Format (TIFF) image handler
 *      This is part of the libYafaRay package
 *      Copyright (C) 2010 Rodrigo Placencia Vazquez
 *
 *      This library is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU Lesser General Public
 *      License as published by the Free Software Foundation; either
 *      version 2.1 of the License, or (at your option) any later version.
 *
 *      This library is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *      Lesser General Public License for more details.
 *
 *      You should have received a copy of the GNU Lesser General Public
 *      License along with this library; if not, write to the Free Software
 *      Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifdef HAVE_TIFF

#include "format/format_tif.h"
#include "common/logger.h"
#include "common/param.h"
#include "scene/scene.h"
#include "color/color.h"

#if defined(_WIN32)
#include "common/string.h"
#endif //defined(_WIN32)

namespace libtiff
{
#include <tiffio.h>
}

BEGIN_YAFARAY

bool TifFormat::saveToFile(const std::string &name, const Image *image)
{
#if defined(_WIN32)
	std::wstring wname = utf8ToWutf16Le_global(name);
	libtiff::TIFF *out = libtiff::TIFFOpenW(wname.c_str(), "w");	//Windows needs the path in UTF16LE (unicode, UTF16, little endian) so we have to convert the UTF8 path to UTF16
#else
	libtiff::TIFF *out = libtiff::TIFFOpen(name.c_str(), "w");
#endif
	if(!out)
	{
		Y_ERROR << getFormatName() << ": Cannot open file " << name << YENDL;
		return false;
	}
	const int h = image->getHeight();
	const int w = image->getWidth();
	int channels;
	if(image->hasAlpha()) channels = 4;
	else channels = 3;

	libtiff::TIFFSetField(out, TIFFTAG_IMAGEWIDTH, w);
	libtiff::TIFFSetField(out, TIFFTAG_IMAGELENGTH, h);
	libtiff::TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, channels);
	libtiff::TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 8);
	libtiff::TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	libtiff::TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	libtiff::TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);

	const size_t bytes_per_scanline = channels * w;
	libtiff::TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, libtiff::TIFFDefaultStripSize(out, bytes_per_scanline));

	uint8_t *scanline = (uint8_t *)libtiff::_TIFFmalloc(bytes_per_scanline);
	for(int y = 0; y < h; y++)
	{
		for(int x = 0; x < w; x++)
		{
			const int ix = x * channels;
			Rgba col = image->getColor(x, y);
			col.clampRgba01();
			scanline[ix] = (uint8_t)(col.getR() * 255.f);
			scanline[ix + 1] = (uint8_t)(col.getG() * 255.f);
			scanline[ix + 2] = (uint8_t)(col.getB() * 255.f);
			if(image->hasAlpha()) scanline[ix + 3] = (uint8_t)(col.getA() * 255.f);
		}
		if(TIFFWriteScanline(out, scanline, y, 0) < 0)
		{
			Y_ERROR << getFormatName() << ": An error occurred while writing TIFF file" << YENDL;
			libtiff::TIFFClose(out);
			libtiff::_TIFFfree(scanline);
			return false;
		}
	}
	libtiff::TIFFClose(out);
	libtiff::_TIFFfree(scanline);
	if(Y_LOG_HAS_VERBOSE) Y_VERBOSE << getFormatName() << ": Done." << YENDL;
	return true;
}

std::unique_ptr<Image> TifFormat::loadFromFile(const std::string &name, const Image::Optimization &optimization, const ColorSpace &color_space, float gamma)
{
#if defined(_WIN32)
	std::wstring wname = utf8ToWutf16Le_global(name);
	libtiff::TIFF *tif = libtiff::TIFFOpenW(wname.c_str(), "r");	//Windows needs the path in UTF16LE (unicode, UTF16, little endian) so we have to convert the UTF8 path to UTF16
#else
	libtiff::TIFF *tif = libtiff::TIFFOpen(name.c_str(), "r");
#endif
	if(!tif)
	{
		Y_ERROR << getFormatName() << ": Cannot open file " << name << YENDL;
		return nullptr;
	}
	Y_INFO << getFormatName() << ": Loading image \"" << name << "\"..." << YENDL;
	libtiff::uint32 w, h;
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
	libtiff::uint32 *tiff_data = (libtiff::uint32 *)libtiff::_TIFFmalloc(w * h * sizeof(libtiff::uint32));
	if(!libtiff::TIFFReadRGBAImage(tif, w, h, tiff_data, 0))
	{
		Y_ERROR << getFormatName() << ": Error reading TIFF file" << YENDL;
		return nullptr;
	}
	const Image::Type type = Image::getTypeFromSettings(true, grayscale_);
	std::unique_ptr<Image> image = Image::factory(w, h, type, optimization);
	int i = 0;
	for(int y = static_cast<int>(h) - 1; y >= 0; y--)
	{
		for(int x = 0; x < static_cast<int>(w); x++)
		{
			Rgba color;
			color.set((float)TIFFGetR(tiff_data[i]) * inv_max_8_bit_,
					  (float)TIFFGetG(tiff_data[i]) * inv_max_8_bit_,
					  (float)TIFFGetB(tiff_data[i]) * inv_max_8_bit_,
					  (float)TIFFGetA(tiff_data[i]) * inv_max_8_bit_);
			color.linearRgbFromColorSpace(color_space, gamma);
			image->setColor(x, y, color);
			++i;
		}
	}
	libtiff::_TIFFfree(tiff_data);
	libtiff::TIFFClose(tif);
	if(Y_LOG_HAS_VERBOSE) Y_VERBOSE << getFormatName() << ": Done." << YENDL;
	return image;
}

std::unique_ptr<Format> TifFormat::factory(ParamMap &params)
{
	return std::unique_ptr<Format>(new TifFormat());
}

END_YAFARAY

#endif // HAVE_TIFF