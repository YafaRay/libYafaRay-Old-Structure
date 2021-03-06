/****************************************************************************
 *
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
 *
 */

#include "render/render_view.h"
#include "common/param.h"
#include "scene/scene.h"
#include "common/string.h"
#include "common/logger.h"
#include "light/light.h"

BEGIN_YAFARAY

std::unique_ptr<RenderView> RenderView::factory(ParamMap &params, const Scene &scene)
{
	if(Y_LOG_HAS_DEBUG) Y_DEBUG PRTEXT(**RenderView) PREND;
	params.printDebug();
	std::string name;
	std::string camera_name;
	std::string light_names; //Separated by semicolon ";"
	float wavelength = 0.f;
	params.getParam("name", name);
	params.getParam("camera_name", camera_name);
	params.getParam("light_names", light_names);
	params.getParam("wavelength", wavelength);

	return std::unique_ptr<RenderView>(new RenderView(name, camera_name, light_names, wavelength));
}

bool RenderView::init(const Scene &scene)
{
	camera_ = scene.getCamera(camera_name_);
	if(!camera_)
	{
		Y_ERROR << "RenderView '" << name_ << "': Camera not found in the scene." << YENDL;
		return false;
	}

	lights_.clear();
	const std::vector<std::string> selected_lights_names = tokenize_global(light_names_, ";");

	if(selected_lights_names.empty())
	{
		for(const auto &l : scene.getLights()) lights_[l.first] = l.second.get();
	}
	else
	{
		for(const auto &light_name : selected_lights_names)
		{
			const Light *light = scene.getLight(light_name);
			if(!light)
			{
				Y_WARNING << "RenderView '" << name_ << "' init: view '" << name_ << "' could not find light '" << light_name << "', skipping..." << YENDL;
				continue;
			}
			lights_[light_name] = scene.getLight(light_name);
		}
	}
	if(lights_.empty())
	{
		Y_ERROR << "RenderView '" << name_ << "': Lights not found in the scene." << YENDL;
		return false;
	}
	return true;
}

const std::vector<const Light *> RenderView::getLightsVisible() const
{
	std::vector<const Light *> result;
	for(const auto &l : lights_)
	{
		if(l.second->lightEnabled() && !l.second->photonOnly()) result.push_back(l.second);
	}
	return result;
}

const std::vector<const Light *> RenderView::getLightsEmittingCausticPhotons() const
{
	std::vector<const Light *> result;
	for(const auto &l : lights_)
	{
		if(l.second->lightEnabled() && l.second->shootsCausticP()) result.push_back(l.second);
	}
	return result;
}

const std::vector<const Light *> RenderView::getLightsEmittingDiffusePhotons() const
{
	std::vector<const Light *> result;
	for(const auto &l : lights_)
	{
		if(l.second->lightEnabled() && l.second->shootsDiffuseP()) result.push_back(l.second);
	}
	return result;
}

END_YAFARAY