/****************************************************************************
 *      This is part of the libYafaRay package
 *      Copyright (C) 2006  Mathias Wein
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
 */

#include "texture/texture.h"
#include "texture/texture_basic.h"
#include "texture/texture_image.h"
#include "common/param.h"

BEGIN_YAFARAY

std::unique_ptr<Texture> Texture::factory(ParamMap &params, const Scene &scene)
{
	if(Y_LOG_HAS_DEBUG)
	{
		Y_DEBUG PRTEXT(**Texture) PREND;
		params.printDebug();
	}
	std::string type;
	params.getParam("type", type);
	if(type == "blend") return BlendTexture::factory(params, scene);
	else if(type == "clouds") return CloudsTexture::factory(params, scene);
	else if(type == "marble") return MarbleTexture::factory(params, scene);
	else if(type == "wood") return WoodTexture::factory(params, scene);
	else if(type == "voronoi") return VoronoiTexture::factory(params, scene);
	else if(type == "musgrave") return MusgraveTexture::factory(params, scene);
	else if(type == "distorted_noise") return DistortedNoiseTexture::factory(params, scene);
	else if(type == "rgb_cube") return RgbCubeTexture::factory(params, scene);
	else if(type == "image") return ImageTexture::factory(params, scene);
	else return nullptr;
}

InterpolationType Texture::getInterpolationTypeFromName(const std::string &interpolation_type_name)
{
	// interpolation type, bilinear default
	if(interpolation_type_name == "none") return InterpolationType::None;
	else if(interpolation_type_name == "bicubic") return InterpolationType::Bicubic;
	else if(interpolation_type_name == "mipmap_trilinear") return InterpolationType::Trilinear;
	else if(interpolation_type_name == "mipmap_ewa") return InterpolationType::Ewa;
	else return InterpolationType::Bilinear;
}

std::string Texture::getInterpolationTypeName(const InterpolationType &interpolation_type)
{
	// interpolation type, bilinear default
	switch(interpolation_type)
	{
		case InterpolationType::None: return "none";
		case InterpolationType::Bilinear: return "bilinear";
		case InterpolationType::Bicubic: return "bicubic";
		case InterpolationType::Trilinear: return "mipmap_trilinear";
		case InterpolationType::Ewa: return "mipmap_ewa";
		default: return "bilinear";
	}
}

END_YAFARAY
