#pragma once
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

#ifndef YAFARAY_VOLUMEHANDLER_BEER_H
#define YAFARAY_VOLUMEHANDLER_BEER_H

#include "volume/volume.h"

BEGIN_YAFARAY

class ParamMap;
class Scene;

class BeerVolumeHandler : public VolumeHandler
{
	public:
		static std::unique_ptr<VolumeHandler> factory(const ParamMap &params, const Scene &scene);

	protected:
		BeerVolumeHandler(const Rgb &sigma): sigma_a_(sigma) {};
		BeerVolumeHandler(const Rgb &acol, double dist);

	private:
		virtual bool transmittance(const RenderData &render_data, const Ray &ray, Rgb &col) const override;
		virtual bool scatter(const RenderData &render_data, const Ray &ray, Ray &s_ray, PSample &s) const override;
		Rgb getSubSurfaceColor(const RenderData &render_data) const { return sigma_a_; }
		Rgb sigma_a_;
};

END_YAFARAY

#endif // YAFARAY_VOLUMEHANDLER_BEER_H