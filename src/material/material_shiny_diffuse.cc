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

#include "material/material_shiny_diffuse.h"
#include "common/param.h"
#include "sampler/sample.h"
#include "shader/shader_node.h"
#include "geometry/surface.h"
#include "common/logger.h"
#include "render/render_data.h"
#include <cstring>

BEGIN_YAFARAY

ShinyDiffuseMaterial::ShinyDiffuseMaterial(const Rgb &diffuse_color, const Rgb &mirror_color, float diffuse_strength, float transparency_strength, float translucency_strength, float mirror_strength, float emit_strength, float transmit_filter_strength, Visibility visibility):
		diffuse_color_(diffuse_color), mirror_color_(mirror_color),
		mirror_strength_(mirror_strength), transparency_strength_(transparency_strength), translucency_strength_(translucency_strength), diffuse_strength_(diffuse_strength), transmit_filter_strength_(transmit_filter_strength)
{
	visibility_ = visibility;
	emit_color_ = emit_strength * diffuse_color;
	emit_strength_ = emit_strength;
	bsdf_flags_ = BsdfFlags::None;
	if(emit_strength_ > 0.f) bsdf_flags_ |= BsdfFlags::Emit;
	visibility_ = visibility;
}

/*! ATTENTION! You *MUST* call this function before using the material, no matter
    if you want to use shaderNodes or not!
*/
void ShinyDiffuseMaterial::config()
{
	n_bsdf_ = 0;
	vi_nodes_[0] = vi_nodes_[1] = vi_nodes_[2] = vi_nodes_[3] = false;
	vd_nodes_[0] = vd_nodes_[1] = vd_nodes_[2] = vd_nodes_[3] = false;
	float acc = 1.f;
	if(mirror_strength_ > 0.00001f || mirror_shader_)
	{
		is_mirror_ = true;
		if(mirror_shader_) vi_nodes_[0] = true;
		else if(!has_fresnel_effect_) acc = 1.f - mirror_strength_;
		bsdf_flags_ |= BsdfFlags::Specular | BsdfFlags::Reflect;
		c_flags_[n_bsdf_] = BsdfFlags::Specular | BsdfFlags::Reflect;
		c_index_[n_bsdf_] = 0;
		++n_bsdf_;
	}
	if(transparency_strength_ * acc > 0.00001f || transparency_shader_)
	{
		is_transparent_ = true;
		if(transparency_shader_) vi_nodes_[1] = true;
		else acc *= 1.f - transparency_strength_;
		bsdf_flags_ |= BsdfFlags::Transmit | BsdfFlags::Filter;
		c_flags_[n_bsdf_] = BsdfFlags::Transmit | BsdfFlags::Filter;
		c_index_[n_bsdf_] = 1;
		++n_bsdf_;
	}
	if(translucency_strength_ * acc > 0.00001f || translucency_shader_)
	{
		is_translucent_ = true;
		if(translucency_shader_) vi_nodes_[2] = true;
		else acc *= 1.f - transparency_strength_;
		bsdf_flags_ |= BsdfFlags::Diffuse | BsdfFlags::Transmit;
		c_flags_[n_bsdf_] = BsdfFlags::Diffuse | BsdfFlags::Transmit;
		c_index_[n_bsdf_] = 2;
		++n_bsdf_;
	}
	if(diffuse_strength_ * acc > 0.00001f)
	{
		is_diffuse_ = true;
		if(diffuse_shader_) vi_nodes_[3] = true;
		bsdf_flags_ |= BsdfFlags::Diffuse | BsdfFlags::Reflect;
		c_flags_[n_bsdf_] = BsdfFlags::Diffuse | BsdfFlags::Reflect;
		c_index_[n_bsdf_] = 3;
		++n_bsdf_;
	}
	req_mem_ = req_node_mem_ + sizeof(SdDat);
}

// component should be initialized with mMirrorStrength, mTransparencyStrength, mTranslucencyStrength, mDiffuseStrength
// since values for which useNode is false do not get touched so it can be applied
// twice, for view-independent (initBSDF) and view-dependent (sample/eval) nodes

void ShinyDiffuseMaterial::getComponents(const bool *use_node, NodeStack &stack, float *component) const
{
	if(is_mirror_) component[0] = use_node[0] ? mirror_shader_->getScalar(stack) : mirror_strength_;
	if(is_transparent_) component[1] = use_node[1] ? transparency_shader_->getScalar(stack) : transparency_strength_;
	if(is_translucent_) component[2] = use_node[2] ? translucency_shader_->getScalar(stack) : translucency_strength_;
	if(is_diffuse_) component[3] = diffuse_strength_;
}

float ShinyDiffuseMaterial::getFresnelKr(const Vec3 &wo, const Vec3 &n, float current_ior_squared) const
{
	if(has_fresnel_effect_)
	{
		const Vec3 N = ((wo * n) < 0.f) ? -n : n;
		const float c = wo * N;
		float g = current_ior_squared + c * c - 1.f;
		if(g < 0.f) g = 0.f;
		else g = math::sqrt(g);
		const float aux = c * (g + c);
		return ((0.5f * (g - c) * (g - c)) / ((g + c) * (g + c))) *
			 (1.f + ((aux - 1) * (aux - 1)) / ((aux + 1) * (aux + 1)));
	}
	else return 1.f;
}

// calculate the absolute value of scattering components from the "normalized"
// fractions which are between 0 (no scattering) and 1 (scatter all remaining light)
// Kr is an optional reflection multiplier (e.g. from Fresnel)
static inline void accumulate_global(const float *component, float *accum, float kr)
{
	accum[0] = component[0] * kr;
	float acc = 1.f - accum[0];
	accum[1] = component[1] * acc;
	acc *= 1.f - component[1];
	accum[2] = component[2] * acc;
	acc *= 1.f - component[2];
	accum[3] = component[3] * acc;
}

void ShinyDiffuseMaterial::initBsdf(const RenderData &render_data, SurfacePoint &sp, BsdfFlags &bsdf_types) const
{
	SdDat *dat = (SdDat *)render_data.arena_;
	memset(dat, 0, 8 * sizeof(float));
	dat->node_stack_ = (char *)render_data.arena_ + sizeof(SdDat);
	//create our "stack" to save node results
	NodeStack stack(dat->node_stack_);

	//bump mapping (extremely experimental)
	if(bump_shader_) evalBump(stack, render_data, sp, bump_shader_);
	for(const auto &node : color_nodes_) node->eval(stack, render_data, sp);
	bsdf_types = bsdf_flags_;
	getComponents(vi_nodes_, stack, dat->component_);
}

/** Initialize Oren Nayar reflectance.
 *  Initialize Oren Nayar A and B coefficient.
 *  @param  sigma Roughness of the surface
 */
void ShinyDiffuseMaterial::initOrenNayar(double sigma)
{
	const double sigma_squared = sigma * sigma;
	oren_nayar_a_ = 1.0 - 0.5 * (sigma_squared / (sigma_squared + 0.33));
	oren_nayar_b_ = 0.45 * sigma_squared / (sigma_squared + 0.09);
	use_oren_nayar_ = true;
}

/** Calculate Oren Nayar reflectance.
 *  Calculate Oren Nayar reflectance for a given reflection.
 *  @param  wi Reflected ray direction
 *  @param  wo Incident ray direction
 *  @param  n  Surface normal
 *  @note   http://en.wikipedia.org/wiki/Oren-Nayar_reflectance_model
 */
float ShinyDiffuseMaterial::orenNayar(const Vec3 &wi, const Vec3 &wo, const Vec3 &n, bool use_texture_sigma, double texture_sigma) const
{
	const float cos_ti = std::max(-1.f, std::min(1.f, n * wi));
	const float cos_to = std::max(-1.f, std::min(1.f, n * wo));
	float maxcos_f = 0.f;

	if(cos_ti < 0.9999f && cos_to < 0.9999f)
	{
		const Vec3 v_1 = (wi - n * cos_ti).normalize();
		const Vec3 v_2 = (wo - n * cos_to).normalize();
		maxcos_f = std::max(0.f, v_1 * v_2);
	}

	float sin_alpha, tan_beta;
	if(cos_to >= cos_ti)
	{
		sin_alpha = math::sqrt(1.f - cos_ti * cos_ti);
		tan_beta = math::sqrt(1.f - cos_to * cos_to) / ((cos_to == 0.f) ? 1e-8f : cos_to); // white (black on windows) dots fix for oren-nayar, could happen with bad normals
	}
	else
	{
		sin_alpha = math::sqrt(1.f - cos_to * cos_to);
		tan_beta = math::sqrt(1.f - cos_ti * cos_ti) / ((cos_ti == 0.f) ? 1e-8f : cos_ti); // white (black on windows) dots fix for oren-nayar, could happen with bad normals
	}

	if(use_texture_sigma)
	{
		const double sigma_squared = texture_sigma * texture_sigma;
		const double m_oren_nayar_texture_a = 1.0 - 0.5 * (sigma_squared / (sigma_squared + 0.33));
		const double m_oren_nayar_texture_b = 0.45 * sigma_squared / (sigma_squared + 0.09);
		return std::min(1.f, std::max(0.f, (float)(m_oren_nayar_texture_a + m_oren_nayar_texture_b * maxcos_f * sin_alpha * tan_beta)));
	}
	else return std::min(1.f, std::max(0.f, oren_nayar_a_ + oren_nayar_b_ * maxcos_f * sin_alpha * tan_beta));
}


Rgb ShinyDiffuseMaterial::eval(const RenderData &render_data, const SurfacePoint &sp, const Vec3 &wo, const Vec3 &wl, const BsdfFlags &bsdfs, bool force_eval) const
{
	const float cos_ng_wo = sp.ng_ * wo;
	const float cos_ng_wl = sp.ng_ * wl;
	// face forward:
	const Vec3 n = SurfacePoint::normalFaceForward(sp.ng_, sp.n_, wo);
	if(!bsdfs.hasAny(bsdf_flags_ & BsdfFlags::Diffuse)) return Rgb(0.f);

	const SdDat *dat = (SdDat *)render_data.arena_;
	const NodeStack stack(dat->node_stack_);

	float cur_ior_squared;
	if(ior_shader_)
	{
		cur_ior_squared = ior_ + ior_shader_->getScalar(stack);
		cur_ior_squared *= cur_ior_squared;
	}
	else cur_ior_squared = ior_squared_;

	const float kr = getFresnelKr(wo, n, cur_ior_squared);
	const float m_t = (1.f - kr * dat->component_[0]) * (1.f - dat->component_[1]);

	const bool transmit = (cos_ng_wo * cos_ng_wl) < 0.f;

	if(transmit) // light comes from opposite side of surface
	{
		if(is_translucent_) return dat->component_[2] * m_t * (diffuse_shader_ ? diffuse_shader_->getColor(stack) : diffuse_color_);
	}

	if(n * wl < 0.0 && !flat_material_) return Rgb(0.f);
	float m_d = m_t * (1.f - dat->component_[2]) * dat->component_[3];

	if(use_oren_nayar_)
	{
		const double texture_sigma = (sigma_oren_shader_ ? sigma_oren_shader_->getScalar(stack) : 0.f);
		const bool use_texture_sigma = (sigma_oren_shader_ ? true : false);
		if(use_oren_nayar_) m_d *= orenNayar(wo, wl, n, use_texture_sigma, texture_sigma);
	}

	if(diffuse_refl_shader_) m_d *= diffuse_refl_shader_->getScalar(stack);

	Rgb result = m_d * (diffuse_shader_ ? diffuse_shader_->getColor(stack) : diffuse_color_);

	const float wire_frame_amount = (wireframe_shader_ ? wireframe_shader_->getScalar(stack) * wireframe_amount_ : wireframe_amount_);
	applyWireFrame(result, wire_frame_amount, sp);
	return result;
}

Rgb ShinyDiffuseMaterial::emit(const RenderData &render_data, const SurfacePoint &sp, const Vec3 &wo) const
{
	const SdDat *dat = (SdDat *)render_data.arena_;
	const NodeStack stack(dat->node_stack_);
	Rgb result = (diffuse_shader_ ? diffuse_shader_->getColor(stack) * emit_strength_ : emit_color_);
	const float wire_frame_amount = (wireframe_shader_ ? wireframe_shader_->getScalar(stack) * wireframe_amount_ : wireframe_amount_);
	applyWireFrame(result, wire_frame_amount, sp);
	return result;
}

Rgb ShinyDiffuseMaterial::sample(const RenderData &render_data, const SurfacePoint &sp, const Vec3 &wo, Vec3 &wi, Sample &s, float &w) const
{
	float accum_c[4];
	const float cos_ng_wo = sp.ng_ * wo;
	const Vec3 n = SurfacePoint::normalFaceForward(sp.ng_, sp.n_, wo);
	const SdDat *dat = (SdDat *)render_data.arena_;
	const NodeStack stack(dat->node_stack_);

	float cur_ior_squared;
	if(ior_shader_)
	{
		cur_ior_squared = ior_ + ior_shader_->getScalar(stack);
		cur_ior_squared *= cur_ior_squared;
	}
	else cur_ior_squared = ior_squared_;

	const float kr = getFresnelKr(wo, n, cur_ior_squared);
	accumulate_global(dat->component_, accum_c, kr);

	float sum = 0.f, val[4], width[4];
	BsdfFlags choice[4];
	int n_match = 0, pick = -1;
	for(int i = 0; i < n_bsdf_; ++i)
	{
		if((s.flags_ & c_flags_[i]) == c_flags_[i])
		{
			width[n_match] = accum_c[c_index_[i]];
			sum += width[n_match];
			choice[n_match] = c_flags_[i];
			val[n_match] = sum;
			++n_match;
		}
	}
	if(!n_match || sum < 0.00001) { s.sampled_flags_ = BsdfFlags::None; s.pdf_ = 0.f; return Rgb(1.f); }
	const float inv_sum = 1.f / sum;
	for(int i = 0; i < n_match; ++i)
	{
		val[i] *= inv_sum;
		width[i] *= inv_sum;
		if((s.s_1_ <= val[i]) && (pick < 0)) pick = i;
	}
	if(pick < 0) pick = n_match - 1;
	float s_1;
	if(pick > 0) s_1 = (s.s_1_ - val[pick - 1]) / width[pick];
	else s_1 = s.s_1_ / width[pick];

	Rgb scolor(0.f);
	switch(static_cast<unsigned int>(choice[pick]))
	{
		case(BsdfFlags::SpecularReflect):
			wi = Vec3::reflectDir(n, wo);
			s.pdf_ = width[pick];
			scolor = (mirror_color_shader_ ? mirror_color_shader_->getColor(stack) : mirror_color_) * (accum_c[0]);
			if(s.reverse_)
			{
				s.pdf_back_ = s.pdf_;
				s.col_back_ = scolor / std::max(std::abs(sp.n_ * wo), 1.0e-6f);
			}
			scolor *= 1.f / std::max(std::abs(sp.n_ * wi), 1.0e-6f);
			break;
		case(BsdfFlags::SpecularTransmit):
			wi = -wo;
			scolor = accum_c[1] * (transmit_filter_strength_ * (diffuse_shader_ ? diffuse_shader_->getColor(stack) : diffuse_color_) + Rgb(1.f - transmit_filter_strength_));
			if(std::abs(wi * n) /*cos_n*/ < 1e-6) s.pdf_ = 0.f;
			else s.pdf_ = width[pick];
			break;
		case(BsdfFlags::Translucency):
			wi = sample::cosHemisphere(-n, sp.nu_, sp.nv_, s_1, s.s_2_);
			if(cos_ng_wo * (sp.ng_ * wi) /* cos_ng_wi */ < 0) scolor = accum_c[2] * (diffuse_shader_ ? diffuse_shader_->getColor(stack) : diffuse_color_);
			s.pdf_ = std::abs(wi * n) * width[pick]; break;
		case(BsdfFlags::DiffuseReflect):
		default:
			wi = sample::cosHemisphere(n, sp.nu_, sp.nv_, s_1, s.s_2_);
			if(cos_ng_wo * (sp.ng_ * wi) /* cos_ng_wi */ > 0) scolor = accum_c[3] * (diffuse_shader_ ? diffuse_shader_->getColor(stack) : diffuse_color_);

			if(use_oren_nayar_)
			{
				const double texture_sigma = (sigma_oren_shader_ ? sigma_oren_shader_->getScalar(stack) : 0.f);
				const bool use_texture_sigma = (sigma_oren_shader_ ? true : false);
				scolor *= orenNayar(wo, wi, n, use_texture_sigma, texture_sigma);
			}
			s.pdf_ = std::abs(wi * n) * width[pick]; break;
	}
	s.sampled_flags_ = choice[pick];
	w = std::abs(wi * sp.n_) / (s.pdf_ * 0.99f + 0.01f);

	const float alpha = getAlpha(render_data, sp, wo);
	w = w * (alpha) + 1.f * (1.f - alpha);

	const float wire_frame_amount = (wireframe_shader_ ? wireframe_shader_->getScalar(stack) * wireframe_amount_ : wireframe_amount_);
	applyWireFrame(scolor, wire_frame_amount, sp);
	return scolor;
}

float ShinyDiffuseMaterial::pdf(const RenderData &render_data, const SurfacePoint &sp, const Vec3 &wo, const Vec3 &wi, const BsdfFlags &bsdfs) const
{
	if(!bsdfs.hasAny(BsdfFlags::Diffuse)) return 0.f;

	const SdDat *dat = (SdDat *)render_data.arena_;
	const NodeStack stack(dat->node_stack_);

	float pdf = 0.f;
	float accum_c[4];
	const float cos_ng_wo = sp.ng_ * wo;
	const Vec3 n = SurfacePoint::normalFaceForward(sp.ng_, sp.n_, wo);

	float cur_ior_squared;
	if(ior_shader_)
	{
		cur_ior_squared = ior_ + ior_shader_->getScalar(stack);
		cur_ior_squared *= cur_ior_squared;
	}
	else cur_ior_squared = ior_squared_;

	const float kr = getFresnelKr(wo, n, cur_ior_squared);

	accumulate_global(dat->component_, accum_c, kr);
	float sum = 0.f, width;
	int n_match = 0;
	for(int i = 0; i < n_bsdf_; ++i)
	{
		if(bsdfs.hasAny(c_flags_[i]))
		{
			width = accum_c[c_index_[i]];
			sum += width;
			switch(static_cast<unsigned int>(c_flags_[i]))
			{
				case(BsdfFlags::Diffuse | BsdfFlags::Transmit):  // translucency (diffuse transmitt)
					if(cos_ng_wo * (sp.ng_ * wi) /*cos_ng_wi*/ < 0) pdf += std::abs(wi * n) * width;
					break;
				case(BsdfFlags::Diffuse | BsdfFlags::Reflect):  // lambertian
					pdf += std::abs(wi * n) * width;
					break;
				default:
					break;
			}
			++n_match;
		}
	}
	if(!n_match || sum < 0.00001) return 0.f;
	return pdf / sum;
}


/** Perfect specular reflection.
 *  Calculate perfect specular reflection and refraction from the material for
 *  a given surface point \a sp and a given incident ray direction \a wo
 *  @param  render_data Render state
 *  @param  sp Surface point
 *  @param  wo Incident ray direction
 *  @param  do_reflect Boolean value which is true if you have a reflection, false otherwise
 *  @param  do_refract Boolean value which is true if you have a refraction, false otherwise
 *  @param  wi Array of two vectors to record reflected ray direction (wi[0]) and refracted ray direction (wi[1])
 *  @param  col Array of two colors to record reflected ray color (col[0]) and refracted ray color (col[1])
 */
Material::Specular ShinyDiffuseMaterial::getSpecular(const RenderData &render_data, const SurfacePoint &sp, const Vec3 &wo) const
{
	Specular specular;
	const SdDat *dat = (SdDat *)render_data.arena_;
	const NodeStack stack(dat->node_stack_);
	const bool backface = wo * sp.ng_ < 0.f;
	const Vec3 n  = backface ? -sp.n_ : sp.n_;
	const Vec3 ng = backface ? -sp.ng_ : sp.ng_;
	float cur_ior_squared;
	if(ior_shader_)
	{
		cur_ior_squared = ior_ + ior_shader_->getScalar(stack);
		cur_ior_squared *= cur_ior_squared;
	}
	else cur_ior_squared = ior_squared_;
	const float kr = getFresnelKr(wo, n, cur_ior_squared);
	if(is_transparent_)
	{
		specular.refract_.enabled_ = true;
		specular.refract_.dir_ = -wo;
		const Rgb tcol = transmit_filter_strength_ * (diffuse_shader_ ? diffuse_shader_->getColor(stack) : diffuse_color_) + Rgb(1.f - transmit_filter_strength_);
		specular.refract_.col_ = (1.f - dat->component_[0] * kr) * dat->component_[1] * tcol;
		const float wire_frame_amount = (wireframe_shader_ ? wireframe_shader_->getScalar(stack) * wireframe_amount_ : wireframe_amount_);
		applyWireFrame(specular.refract_.col_, wire_frame_amount, sp);
	}
	if(is_mirror_)
	{
		specular.reflect_.enabled_ = true;
		//Y_WARNING << sp.N << " | " << N << YENDL;
		specular.reflect_.dir_ = wo;
		specular.reflect_.dir_.reflect(n);
		const float cos_wi_ng = specular.reflect_.dir_ * ng;
		if(cos_wi_ng < 0.01)
		{
			specular.reflect_.dir_ += (0.01 - cos_wi_ng) * ng;
			specular.reflect_.dir_.normalize();
		}
		specular.reflect_.col_ = (mirror_color_shader_ ? mirror_color_shader_->getColor(stack) : mirror_color_) * (dat->component_[0] * kr);
		const float wire_frame_amount = (wireframe_shader_ ? wireframe_shader_->getScalar(stack) * wireframe_amount_ : wireframe_amount_);
		applyWireFrame(specular.reflect_.col_, wire_frame_amount, sp);
	}
	return specular;
}

Rgb ShinyDiffuseMaterial::getTransparency(const RenderData &render_data, const SurfacePoint &sp, const Vec3 &wo) const
{
	if(!is_transparent_) return Rgb(0.f);

	NodeStack stack(render_data.arena_);
	for(const auto &node : color_nodes_sorted_) node->eval(stack, render_data, sp);
	float accum = 1.f;
	const Vec3 n = SurfacePoint::normalFaceForward(sp.ng_, sp.n_, wo);

	float cur_ior_squared;
	if(ior_shader_)
	{
		cur_ior_squared = ior_ + ior_shader_->getScalar(stack);
		cur_ior_squared *= cur_ior_squared;
	}
	else cur_ior_squared = ior_squared_;

	const float kr = getFresnelKr(wo, n, cur_ior_squared);

	if(is_mirror_) accum = 1.f - kr * (mirror_shader_ ? mirror_shader_->getScalar(stack) : mirror_strength_);
	if(is_transparent_) //uhm...should actually be true if this function gets called anyway...
	{
		accum *= transparency_shader_ ? transparency_shader_->getScalar(stack) * accum : transparency_strength_ * accum;
	}
	const Rgb tcol = transmit_filter_strength_ * (diffuse_shader_ ? diffuse_shader_->getColor(stack) : diffuse_color_) + Rgb(1.f - transmit_filter_strength_);
	Rgb result = accum * tcol;
	const float wire_frame_amount = (wireframe_shader_ ? wireframe_shader_->getScalar(stack) * wireframe_amount_ : wireframe_amount_);
	applyWireFrame(result, wire_frame_amount, sp);
	return result;
}

float ShinyDiffuseMaterial::getAlpha(const RenderData &render_data, const SurfacePoint &sp, const Vec3 &wo) const
{
	const SdDat *dat = (SdDat *)render_data.arena_;
	const NodeStack stack(dat->node_stack_);

	if(is_transparent_)
	{
		const Vec3 n = SurfacePoint::normalFaceForward(sp.ng_, sp.n_, wo);
		float cur_ior_squared;
		if(ior_shader_)
		{
			cur_ior_squared = ior_ + ior_shader_->getScalar(stack);
			cur_ior_squared *= cur_ior_squared;
		}
		else cur_ior_squared = ior_squared_;
		const float kr = getFresnelKr(wo, n, cur_ior_squared);
		const float refl = (1.f - dat->component_[0] * kr) * dat->component_[1];
		float result = 1.f - refl;
		const float wire_frame_amount = (wireframe_shader_ ? wireframe_shader_->getScalar(stack) * wireframe_amount_ : wireframe_amount_);
		applyWireFrame(result, wire_frame_amount, sp);
		return result;
	}
	return 1.f;
}

std::unique_ptr<Material> ShinyDiffuseMaterial::factory(ParamMap &params, std::list<ParamMap> &params_list, Scene &scene)
{
	/// Material Parameters
	Rgb diffuse_color = 1.f;
	Rgb mirror_color = 1.f;
	float diffuse_strength = 1.f;
	float transparency_strength = 0.f;
	float translucency_strength = 0.f;
	float mirror_strength = 0.f;
	float emit_strength = 0.f;
	bool has_fresnel_effect = false;
	std::string s_visibility = "normal";
	bool receive_shadows = true;
	bool flat_material = false;
	float ior = 1.33f;
	double transmit_filter_strength = 1.0;
	int mat_pass_index = 0;
	int additionaldepth = 0;
	float transparentbias_factor = 0.f;
	bool transparentbias_multiply_raydepth = false;
	float samplingfactor = 1.f;
	float wire_frame_amount = 0.f;           //!< Wireframe shading amount
	float wire_frame_thickness = 0.01f;      //!< Wireframe thickness
	float wire_frame_exponent = 0.f;         //!< Wireframe exponent (0.f = solid, 1.f=linearly gradual, etc)
	Rgb wire_frame_color = Rgb(1.f); //!< Wireframe shading color

	params.getParam("color", diffuse_color);
	params.getParam("mirror_color", mirror_color);
	params.getParam("transparency", transparency_strength);
	params.getParam("translucency", translucency_strength);
	params.getParam("diffuse_reflect", diffuse_strength);
	params.getParam("specular_reflect", mirror_strength);
	params.getParam("emit", emit_strength);
	params.getParam("IOR", ior);
	params.getParam("fresnel_effect", has_fresnel_effect);
	params.getParam("transmit_filter", transmit_filter_strength);

	params.getParam("receive_shadows",  receive_shadows);
	params.getParam("flat_material",  flat_material);
	params.getParam("visibility", s_visibility);
	params.getParam("mat_pass_index",   mat_pass_index);
	params.getParam("additionaldepth",   additionaldepth);
	params.getParam("transparentbias_factor",   transparentbias_factor);
	params.getParam("transparentbias_multiply_raydepth",   transparentbias_multiply_raydepth);
	params.getParam("samplingfactor",   samplingfactor);

	params.getParam("wireframe_amount", wire_frame_amount);
	params.getParam("wireframe_thickness", wire_frame_thickness);
	params.getParam("wireframe_exponent", wire_frame_exponent);
	params.getParam("wireframe_color", wire_frame_color);

	const Visibility visibility = visibilityFromString_global(s_visibility);

	// !!remember to put diffuse multiplier in material itself!
	auto mat = std::unique_ptr<ShinyDiffuseMaterial>(new ShinyDiffuseMaterial(diffuse_color, mirror_color, diffuse_strength, transparency_strength, translucency_strength, mirror_strength, emit_strength, transmit_filter_strength, visibility));

	mat->setMaterialIndex(mat_pass_index);
	mat->receive_shadows_ = receive_shadows;
	mat->flat_material_ = flat_material;
	mat->additional_depth_ = additionaldepth;
	mat->transparent_bias_factor_ = transparentbias_factor;
	mat->transparent_bias_multiply_ray_depth_ = transparentbias_multiply_raydepth;

	mat->wireframe_amount_ = wire_frame_amount;
	mat->wireframe_thickness_ = wire_frame_thickness;
	mat->wireframe_exponent_ = wire_frame_exponent;
	mat->wireframe_color_ = wire_frame_color;

	mat->setSamplingFactor(samplingfactor);

	if(has_fresnel_effect)
	{
		mat->ior_ = ior;
		mat->ior_squared_ = ior * ior;
		mat->has_fresnel_effect_ = true;
	}

	std::string name;
	if(params.getParam("diffuse_brdf", name))
	{
		if(name == "oren_nayar")
		{
			double sigma = 0.1;
			params.getParam("sigma", sigma);
			mat->initOrenNayar(sigma);
		}
	}

	/// Material Shader Nodes
	std::vector<ShaderNode *> roots;
	std::map<std::string, ShaderNode *> node_list;

	// prepare shader nodes list
	node_list["diffuse_shader"]      = nullptr;
	node_list["mirror_color_shader"] = nullptr;
	node_list["bump_shader"]         = nullptr;
	node_list["mirror_shader"]       = nullptr;
	node_list["transparency_shader"] = nullptr;
	node_list["translucency_shader"] = nullptr;
	node_list["sigma_oren_shader"]   = nullptr;
	node_list["diffuse_refl_shader"] = nullptr;
	node_list["IOR_shader"]          = nullptr;
	node_list["wireframe_shader"]    = nullptr;

	// load shader nodes:
	if(mat->loadNodes(params_list, scene))
	{
		mat->parseNodes(params, roots, node_list);
	}
	else Y_ERROR << "ShinyDiffuse: Loading shader nodes failed!" << YENDL;

	mat->diffuse_shader_      = node_list["diffuse_shader"];
	mat->mirror_color_shader_  = node_list["mirror_color_shader"];
	mat->bump_shader_         = node_list["bump_shader"];
	mat->mirror_shader_       = node_list["mirror_shader"];
	mat->transparency_shader_ = node_list["transparency_shader"];
	mat->translucency_shader_ = node_list["translucency_shader"];
	mat->sigma_oren_shader_    = node_list["sigma_oren_shader"];
	mat->diffuse_refl_shader_  = node_list["diffuse_refl_shader"];
	mat->ior_shader_                = node_list["IOR_shader"];
	mat->wireframe_shader_    = node_list["wireframe_shader"];

	// solve nodes order
	if(!roots.empty())
	{
		mat->solveNodesOrder(roots);
		if(mat->diffuse_shader_)      mat->getNodeList(mat->diffuse_shader_, mat->color_nodes_);
		if(mat->mirror_color_shader_)  mat->getNodeList(mat->mirror_color_shader_, mat->color_nodes_);
		if(mat->mirror_shader_)       mat->getNodeList(mat->mirror_shader_, mat->color_nodes_);
		if(mat->transparency_shader_) mat->getNodeList(mat->transparency_shader_, mat->color_nodes_);
		if(mat->translucency_shader_) mat->getNodeList(mat->translucency_shader_, mat->color_nodes_);
		if(mat->sigma_oren_shader_)    mat->getNodeList(mat->sigma_oren_shader_, mat->color_nodes_);
		if(mat->diffuse_refl_shader_)  mat->getNodeList(mat->diffuse_refl_shader_, mat->color_nodes_);
		if(mat->ior_shader_)                mat->getNodeList(mat->ior_shader_, mat->color_nodes_);
		if(mat->wireframe_shader_)    mat->getNodeList(mat->wireframe_shader_, mat->color_nodes_);
		if(mat->bump_shader_)         mat->getNodeList(mat->bump_shader_, mat->bump_nodes_);
	}
	mat->config();
	return mat;
}

Rgb ShinyDiffuseMaterial::getDiffuseColor(const RenderData &render_data) const {
	const SdDat *dat = (SdDat *)render_data.arena_;
	const NodeStack stack(dat->node_stack_);
	if(is_diffuse_) return (diffuse_refl_shader_ ? diffuse_refl_shader_->getScalar(stack) : diffuse_strength_) * (diffuse_shader_ ? diffuse_shader_->getColor(stack) : diffuse_color_);
	else return Rgb(0.f);
}

Rgb ShinyDiffuseMaterial::getGlossyColor(const RenderData &render_data) const {
	const SdDat *dat = (SdDat *)render_data.arena_;
	const NodeStack stack(dat->node_stack_);
	if(is_mirror_) return (mirror_shader_ ? mirror_shader_->getScalar(stack) : mirror_strength_) * (mirror_color_shader_ ? mirror_color_shader_->getColor(stack) : mirror_color_);
	else return Rgb(0.f);
}

Rgb ShinyDiffuseMaterial::getTransColor(const RenderData &render_data) const {
	const SdDat *dat = (SdDat *)render_data.arena_;
	const NodeStack stack(dat->node_stack_);
	if(is_transparent_) return (transparency_shader_ ? transparency_shader_->getScalar(stack) : transparency_strength_) * (diffuse_shader_ ? diffuse_shader_->getColor(stack) : diffuse_color_);
	else return Rgb(0.f);
}

Rgb ShinyDiffuseMaterial::getMirrorColor(const RenderData &render_data) const {
	const SdDat *dat = (SdDat *)render_data.arena_;
	const NodeStack stack(dat->node_stack_);
	if(is_mirror_) return (mirror_shader_ ? mirror_shader_->getScalar(stack) : mirror_strength_) * (mirror_color_shader_ ? mirror_color_shader_->getColor(stack) : mirror_color_);
	else return Rgb(0.f);
}

Rgb ShinyDiffuseMaterial::getSubSurfaceColor(const RenderData &render_data) const {
	const SdDat *dat = (SdDat *)render_data.arena_;
	const NodeStack stack(dat->node_stack_);
	if(is_translucent_) return (translucency_shader_ ? translucency_shader_->getScalar(stack) : translucency_strength_) * (diffuse_shader_ ? diffuse_shader_->getColor(stack) : diffuse_color_);
	else return Rgb(0.f);
}

END_YAFARAY
