/****************************************************************************
 *      textureback.cc: a background using the texture class
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

#include "background/background_texture.h"
#include "common/logger.h"
#include "texture/texture.h"
#include "common/param.h"
#include "scene/scene.h"
#include "light/light.h"
#include "output/output.h"

BEGIN_YAFARAY

TextureBackground::TextureBackground(const Texture *texture, Projection proj, float bpower, float rot, bool ibl, float ibl_blur, bool with_caustic):
		tex_(texture), project_(proj), power_(bpower), ibl_blur_mipmap_level_(math::pow(ibl_blur, 2.f))
{
	with_ibl_ = ibl;
	shoot_caustic_ = with_caustic;

	rotation_ = 2.f * rot / 360.f;
	sin_r_ = math::sin(M_PI * rotation_);
	cos_r_ = math::cos(M_PI * rotation_);
}

Rgb TextureBackground::operator()(const Ray &ray, RenderData &render_data, bool use_ibl_blur) const
{
	return eval(ray, use_ibl_blur);
}

Rgb TextureBackground::eval(const Ray &ray, bool use_ibl_blur) const
{
	float u = 0.f, v = 0.f;
	if(project_ == Angular)
	{
		Point3 dir(ray.dir_);
		dir.x_ = ray.dir_.x_ * cos_r_ + ray.dir_.y_ * sin_r_;
		dir.y_ = ray.dir_.x_ * -sin_r_ + ray.dir_.y_ * cos_r_;
		angmap_global(dir, u, v);
	}
	else
	{
		spheremap_global(ray.dir_, u, v); // This returns u,v in 0,1 range (useful for bgLight_t)
		// Put u,v in -1,1 range for mapping
		u = 2.f * u - 1.f;
		v = 2.f * v - 1.f;
		u += rotation_;
		if(u > 1.f) u -= 2.f;
	}

	Rgb ret;
	if(use_ibl_blur)
	{
		ret = tex_->getColor(Point3(u, v, 0.f)); //FIXME!
	}
	else ret = tex_->getColor(Point3(u, v, 0.f));

	const float min_component = 1.0e-5f;
	if(ret.r_ < min_component) ret.r_ = min_component;
	if(ret.g_ < min_component) ret.g_ = min_component;
	if(ret.b_ < min_component) ret.b_ = min_component;
	return power_ * ret;
}

std::shared_ptr<Background> TextureBackground::factory(ParamMap &params, Scene &scene)
{
	Texture *tex = nullptr;
	std::string texname;
	std::string mapping;
	Projection pr = Spherical;
	float power = 1.0, rot = 0.0;
	bool ibl = false;
	float ibl_blur = 0.f;
	float ibl_clamp_sampling = 0.f;
	int ibl_sam = 16;
	bool caust = true;
	bool diffuse = true;
	bool cast_shadows = true;

	if(!params.getParam("texture", texname))
	{
		Y_ERROR << "TextureBackground: No texture given for texture background!" << YENDL;
		return nullptr;
	}
	tex = scene.getTexture(texname);
	if(!tex)
	{
		Y_ERROR << "TextureBackground: Texture '" << texname << "' for textureback not existant!" << YENDL;
		return nullptr;
	}
	if(params.getParam("mapping", mapping))
	{
		if(mapping == "probe" || mapping == "angular") pr = Angular;
	}
	params.getParam("ibl", ibl);
	params.getParam("smartibl_blur", ibl_blur);
	params.getParam("ibl_clamp_sampling", ibl_clamp_sampling);
	params.getParam("ibl_samples", ibl_sam);
	params.getParam("power", power);
	params.getParam("rotation", rot);
	params.getParam("with_caustic", caust);
	params.getParam("with_diffuse", diffuse);
	params.getParam("cast_shadows", cast_shadows);

	auto tex_bg = std::make_shared<TextureBackground>(TextureBackground(tex, pr, power, rot, ibl, ibl_blur, caust));

	if(ibl)
	{
		ParamMap bgp;
		bgp["type"] = std::string("bglight");
		bgp["samples"] = ibl_sam;
		bgp["with_caustic"] = caust;
		bgp["with_diffuse"] = diffuse;
		bgp["abs_intersect"] = false; //this used to be (pr == angular);  but that caused the IBL light to be in the wrong place (see http://www.yafaray.org/node/714) I don't understand why this was set that way, we should keep an eye on this.
		bgp["cast_shadows"] = cast_shadows;

		if(ibl_blur > 0.f)
		{
			Y_INFO << "TextureBackground: starting background SmartIBL blurring with IBL Blur factor=" << ibl_blur << YENDL;
			tex->generateMipMaps();
			if(Y_LOG_HAS_VERBOSE) Y_VERBOSE << "TextureBackground: background SmartIBL blurring done using mipmaps." << YENDL;
		}

		Light *bglight = scene.createLight("textureBackground_bgLight", bgp);

		bglight->setBackground(tex_bg);

		if(ibl_clamp_sampling > 0.f)
		{
			Y_INFO << "TextureBackground: using IBL sampling clamp=" << ibl_clamp_sampling << YENDL;

			bglight->setClampIntersect(ibl_clamp_sampling);
		}
	}

	return tex_bg;
}

END_YAFARAY
