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

#ifndef YAFARAY_INTEGRATOR_EMISSION_H
#define YAFARAY_INTEGRATOR_EMISSION_H

#include "integrator/integrator.h"

BEGIN_YAFARAY

class EmissionIntegrator final : public VolumeIntegrator
{
	public:
		static std::unique_ptr<Integrator> factory(ParamMap &params, const Scene &scene);

	private:
		virtual std::string getShortName() const override { return "Em"; }
		virtual std::string getName() const override { return "Emission"; }
		// optical thickness, absorption, attenuation, extinction
		virtual Rgba transmittance(RenderData &render_data, const Ray &ray) const override;
		// emission part
		virtual Rgba integrate(RenderData &render_data, const Ray &ray, int additional_depth = 0) const override;
};

END_YAFARAY

#endif // YAFARAY_INTEGRATOR_EMISSION_H