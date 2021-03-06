/****************************************************************************
 *      This is part of the libYafaRay package
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

#include "volume/volume_noise.h"
#include "geometry/surface.h"
#include "texture/texture.h"
#include "common/param.h"
#include "scene/scene.h"

BEGIN_YAFARAY

class RenderData;
struct PSample;

float NoiseVolumeRegion::density(Point3 p) const
{
	float d = tex_dist_noise_->getColor(p * 0.1f).energy();

	d = 1.0f / (1.0f + math::exp(sharpness_ * (1.0f - cover_ - d)));
	d *= density_;

	return d;
}

std::unique_ptr<VolumeRegion> NoiseVolumeRegion::factory(const ParamMap &params, const Scene &scene)
{
	float ss = .1f;
	float sa = .1f;
	float le = .0f;
	float g = .0f;
	float cov = 1.0f;
	float sharp = 1.0f;
	float dens = 1.0f;
	float min[] = {0, 0, 0};
	float max[] = {0, 0, 0};
	int att_sc = 1;
	std::string tex_name;

	params.getParam("sigma_s", ss);
	params.getParam("sigma_a", sa);
	params.getParam("l_e", le);
	params.getParam("g", g);
	params.getParam("sharpness", sharp);
	params.getParam("density", dens);
	params.getParam("cover", cov);
	params.getParam("minX", min[0]);
	params.getParam("minY", min[1]);
	params.getParam("minZ", min[2]);
	params.getParam("maxX", max[0]);
	params.getParam("maxY", max[1]);
	params.getParam("maxZ", max[2]);
	params.getParam("attgridScale", att_sc);
	params.getParam("texture", tex_name);

	if(tex_name.empty())
	{
		if(Y_LOG_HAS_VERBOSE) Y_VERBOSE << "NoiseVolume: Noise texture not set, the volume region won't be created." << YENDL;
		return nullptr;
	}

	Texture *noise = scene.getTexture(tex_name);

	if(!noise)
	{
		if(Y_LOG_HAS_VERBOSE) Y_VERBOSE << "NoiseVolume: Noise texture '" << tex_name << "' couldn't be found, the volume region won't be created." << YENDL;
		return nullptr;
	}

	return std::unique_ptr<VolumeRegion>(new NoiseVolumeRegion(Rgb(sa), Rgb(ss), Rgb(le), g, cov, sharp, dens, Point3(min[0], min[1], min[2]), Point3(max[0], max[1], max[2]), att_sc, noise));
}

END_YAFARAY
