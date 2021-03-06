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

#include "geometry/object.h"
#include "scene/yafaray/object_mesh.h"
#include "scene/yafaray/object_curve.h"
#include "scene/yafaray/object_primitive.h"
#include "scene/yafaray/primitive_sphere.h"
#include "common/param.h"
#include "common/logger.h"

BEGIN_YAFARAY

std::unique_ptr<Object> Object::factory(ParamMap &params, const Scene &scene)
{
	if(Y_LOG_HAS_DEBUG)
	{
		Y_DEBUG PRTEXT(Object::factory) PREND;
		params.printDebug();
	}
	std::string type;
	params.getParam("type", type);
	if(type == "mesh") return MeshObject::factory(params, scene);
	else if(type == "curve") return CurveObject::factory(params, scene);
	else if(type == "sphere")
	{
		auto object = std::unique_ptr<PrimitiveObject>(new PrimitiveObject);
		object->setPrimitive(SpherePrimitive::factory(params, scene, *object));
		return object;
	}
	else return nullptr;
}

END_YAFARAY
