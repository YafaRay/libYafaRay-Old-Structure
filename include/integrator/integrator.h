#pragma once
/****************************************************************************
 *      integrator.h: the interface definition for light integrators
 *      This is part of the libYafaRay package
 *      Copyright (C) 2006  Mathias Wein (Lynx)
 *      Copyright (C) 2010  Rodrigo Placencia (DarkTide)
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

#ifndef YAFARAY_INTEGRATOR_H
#define YAFARAY_INTEGRATOR_H

#include "constants.h"
#include <string>
#include "render/render_control.h"
#include "render/render_view.h"

BEGIN_YAFARAY

/*!	Integrate the incoming light scattered by the surfaces
	hit by a given ray
*/

class ParamMap;
class Scene;
class ProgressBar;
class Rgba;
class RenderData;
class Ray;
class DiffRay;
class ColorLayers;
class ImageFilm;

class Integrator
{
	public:
		static std::unique_ptr<Integrator> factory(ParamMap &params, const Scene &scene);

		Integrator() = default;
		virtual ~Integrator() = default;
		//! this MUST be called before any other member function!
		virtual bool render(RenderControl &render_control, const RenderView *render_view) { return false; }
		void setScene(const Scene *s) { scene_ = s; }
		/*! do whatever is required to render the image, if suitable for integrating whole image */
		void setProgressBar(std::shared_ptr<ProgressBar> pb) { intpb_ = std::move(pb); }
		/*! gets called before the scene rendering (i.e. before first call to integrate)
			\return false when preprocessing could not be done properly, true otherwise */
		virtual bool preprocess(const RenderControl &render_control, const RenderView *render_view, ImageFilm *image_film) = 0;
		/*! allow the integrator to do some cleanup when an image is done
		(possibly also important for multiframe rendering in the future)	*/
		virtual void cleanup() { render_info_.clear(); aa_noise_info_.clear(); }
		virtual std::string getShortName() const = 0;
		virtual std::string getName() const = 0;
		enum Type { Surface, Volume };
		virtual Type getType() const = 0;
		std::string getRenderInfo() const { return render_info_; }
		std::string getAaNoiseInfo() const { return aa_noise_info_; }
		static constexpr unsigned int getUserDataSize() { return user_data_size_; } //Total number of bytes used for the "arena"-style "userdata" memory

	protected:
		static constexpr unsigned int user_data_size_ = 1024; //Total number of bytes used for the "arena"-style "userdata" memory
		std::string render_info_;
		std::string aa_noise_info_;
		const Scene *scene_ = nullptr;
		std::shared_ptr<ProgressBar> intpb_;
};

class SurfaceIntegrator: public Integrator
{
	public:
		virtual Rgba integrate(RenderData &render_data, const DiffRay &ray, int additional_depth, ColorLayers *color_layers, const RenderView *render_view) const = 0;

	protected:
		SurfaceIntegrator() = default;
		virtual Type getType() const override { return Surface; }

	protected:
		ImageFilm *image_film_ = nullptr;
};

class VolumeIntegrator: public Integrator
{
	public:
		virtual Rgba transmittance(RenderData &render_data, const Ray &ray) const = 0;
		virtual Rgba integrate(RenderData &render_data, const Ray &ray, int additional_depth = 0) const = 0;
		virtual bool preprocess(const RenderControl &render_control, const RenderView *render_view, ImageFilm *image_film) override { return true; };
	protected:
		VolumeIntegrator() = default;
		virtual Type getType() const override { return Volume; }
};

END_YAFARAY

#endif // YAFARAY_INTEGRATOR_H
