/****************************************************************************
 *      imagetex.cc: a texture class for images
 *      This is part of the libYafaRay package
 *      Based on the original by: Mathias Wein; Copyright (C) 2006 Mathias Wein
 *      Copyright (C) 2010 Rodrigo Placencia Vazquez (DarkTide)
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

#include "texture/texture_image.h"
#include "common/session.h"
#include "common/string.h"
#include "common/param.h"
#include "scene/scene.h"
#include "math/interpolation.h"
#include "format/format.h"
#include "common/file.h"

#ifdef HAVE_OPENCV
#include <opencv2/photo/photo.hpp>
#endif

BEGIN_YAFARAY

float *ImageTexture::ewa_weight_lut_ = nullptr;

ImageTexture::ImageTexture(std::unique_ptr<Image> image)
{
	images_.emplace_back(std::move(image));
}

ImageTexture::~ImageTexture()
{
}

void ImageTexture::resolution(int &x, int &y, int &z) const
{
	x = images_.at(0)->getWidth();
	y = images_.at(0)->getHeight();
	z = 0;
}

Rgba ImageTexture::interpolateImage(const Point3 &p, const MipMapParams *mipmap_params) const
{
	if(mipmap_params && mipmap_params->force_image_level_ > 0.f) return mipMapsTrilinearInterpolation(p, mipmap_params);

	Rgba interpolated_color(0.f);

	switch(interpolation_type_)
	{
		case InterpolationType::None: interpolated_color = noInterpolation(p); break;
		case InterpolationType::Bicubic: interpolated_color = bicubicInterpolation(p); break;
		case InterpolationType::Trilinear:
			if(mipmap_params) interpolated_color = mipMapsTrilinearInterpolation(p, mipmap_params);
			else interpolated_color = bilinearInterpolation(p);
			break;
		case InterpolationType::Ewa:
			if(mipmap_params) interpolated_color = mipMapsEwaInterpolation(p, ewa_max_anisotropy_, mipmap_params);
			else interpolated_color = bilinearInterpolation(p);
			break;
		default: //By default use Bilinear
		case InterpolationType::Bilinear: interpolated_color = bilinearInterpolation(p); break;
	}
	return interpolated_color;
}

Rgba ImageTexture::getColor(const Point3 &p, const MipMapParams *mipmap_params) const
{
	Point3 p_1 = Point3(p.x_, -p.y_, p.z_);
	Rgba ret(0.f);
	const bool outside = doMapping(p_1);
	if(outside) return ret;
	ret = interpolateImage(p_1, mipmap_params);
	return applyAdjustments(ret);
}

Rgba ImageTexture::getRawColor(const Point3 &p, const MipMapParams *mipmap_params) const
{
	// As from v3.2.0 all image buffers are already Linear RGB, if any part of the code requires the original "raw" color, a "de-linearization" (encoding again into the original color space) takes place in this function.

	// For example for Non-RGB / Stencil / Bump / Normal maps, etc, textures are typically already linear and the user should select "linearRGB" in the texture properties, but if the user (by mistake) keeps the default sRGB for them, for example, the default linearization would apply a sRGB to linearRGB conversion that messes up the texture values. This function will try to reconstruct the original texture values. In this case (if the user selected incorrectly sRGB for a normal map, for example), this function will prevent wrong results, but it will be slower and it could be slightly inaccurate as the interpolation will take place in the (incorrectly) linearized texels.

	//If the user correctly selected "linearRGB" for these kind of textures, the function below will not make any changes to the color and will keep the texture "as is" without any linearization nor de-linearization, which is the ideal case (fast and correct).

	//The user is responsible to select the correct textures color spaces, if the user does not do it, results may not be the expected. This function is only a coarse "fail safe"

	Rgba ret = getColor(p, mipmap_params);
	ret.colorSpaceFromLinearRgb(original_image_file_color_space_, original_image_file_gamma_);
	return ret;
}

bool ImageTexture::doMapping(Point3 &texpt) const
{
	bool outside = false;
	texpt = 0.5f * texpt + 0.5f;
	// repeat, only valid for REPEAT clipmode
	if(tex_clip_mode_ == ClipMode::Repeat)
	{
		if(xrepeat_ > 1) texpt.x_ *= static_cast<float>(xrepeat_);
		if(yrepeat_ > 1) texpt.y_ *= static_cast<float>(yrepeat_);
		if(mirror_x_ && static_cast<int>(ceilf(texpt.x_)) % 2 == 0) texpt.x_ = -texpt.x_;
		if(mirror_y_ && static_cast<int>(ceilf(texpt.y_)) % 2 == 0) texpt.y_ = -texpt.y_;
		if(texpt.x_ > 1.f) texpt.x_ -= static_cast<int>(texpt.x_);
		else if(texpt.x_ < 0.f) texpt.x_ += 1 - static_cast<int>(texpt.x_);

		if(texpt.y_ > 1.f) texpt.y_ -= static_cast<int>(texpt.y_);
		else if(texpt.y_ < 0.f) texpt.y_ += 1 - static_cast<int>(texpt.y_);
	}

	// crop
	if(cropx_) texpt.x_ = cropminx_ + texpt.x_ * (cropmaxx_ - cropminx_);
	if(cropy_) texpt.y_ = cropminy_ + texpt.y_ * (cropmaxy_ - cropminy_);

	// rot90
	if(rot_90_) std::swap(texpt.x_, texpt.y_);

	// clipping
	switch(tex_clip_mode_)
	{
		case ClipMode::ClipCube:
		{
			if((texpt.x_ < 0) || (texpt.x_ > 1) || (texpt.y_ < 0) || (texpt.y_ > 1) || (texpt.z_ < -1) || (texpt.z_ > 1))
				outside = true;
			break;
		}
		case ClipMode::Checker:
		{
			const int xs = static_cast<int>(floor(texpt.x_)), ys = static_cast<int>(floor(texpt.y_));
			texpt.x_ -= xs;
			texpt.y_ -= ys;
			if(!checker_odd_ && !((xs + ys) & 1))
			{
				outside = true;
				break;
			}
			if(!checker_even_ && ((xs + ys) & 1))
			{
				outside = true;
				break;
			}
			// scale around center, (0.5, 0.5)
			if(checker_dist_ < 1.0)
			{
				texpt.x_ = (texpt.x_ - 0.5f) / (1.f - checker_dist_) + 0.5f;
				texpt.y_ = (texpt.y_ - 0.5f) / (1.f - checker_dist_) + 0.5f;
			}
			// continue to TCL_CLIP
		}
		case ClipMode::Clip:
		{
			if((texpt.x_ < 0) || (texpt.x_ > 1) || (texpt.y_ < 0) || (texpt.y_ > 1))
				outside = true;
			break;
		}
		case ClipMode::Extend:
		{
			if(texpt.x_ > 0.99999f) texpt.x_ = 0.99999f; else if(texpt.x_ < 0) texpt.x_ = 0;
			if(texpt.y_ > 0.99999f) texpt.y_ = 0.99999f; else if(texpt.y_ < 0) texpt.y_ = 0;
			// no break, fall thru to TEX_REPEAT
		}
		default:
		case ClipMode::Repeat: outside = false; break;
	}
	return outside;
}

void ImageTexture::setCrop(float minx, float miny, float maxx, float maxy)
{
	cropminx_ = minx, cropmaxx_ = maxx, cropminy_ = miny, cropmaxy_ = maxy;
	cropx_ = ((cropminx_ != 0.0) || (cropmaxx_ != 1.0));
	cropy_ = ((cropminy_ != 0.0) || (cropmaxy_ != 1.0));
}

void ImageTexture::findTextureInterpolationCoordinates(int &coord_0, int &coord_1, int &coord_2, int &coord_3, float &coord_decimal_part, float coord_float, int resolution, bool repeat, bool mirror) const
{
	if(repeat)
	{
		coord_1 = (static_cast<int>(coord_float)) % resolution;
		if(mirror)
		{
			if(coord_float < 0.f)
			{
				coord_0 = 1 % resolution;
				coord_2 = coord_1;
				coord_3 = coord_0;
				coord_decimal_part = -coord_float;
			}
			else if(coord_float >= (resolution - 1))
			{
				coord_0 = (2 * resolution - 1) % resolution;
				coord_2 = coord_1;
				coord_3 = coord_0;
				coord_decimal_part = coord_float - (static_cast<int>(coord_float));
			}
			else
			{
				coord_0 = (resolution + coord_1 - 1) % resolution;
				coord_2 = coord_1 + 1;
				if(coord_2 >= resolution) coord_2 = (2 * resolution - coord_2) % resolution;
				coord_3 = coord_1 + 2;
				if(coord_3 >= resolution) coord_3 = (2 * resolution - coord_3) % resolution;
				coord_decimal_part = coord_float - (static_cast<int>(coord_float));
			}
		}
		else
		{
			if(coord_float > 0.f)
			{
				coord_0 = (resolution + coord_1 - 1) % resolution;
				coord_2 = (coord_1 + 1) % resolution;
				coord_3 = (coord_1 + 2) % resolution;
				coord_decimal_part = coord_float - (static_cast<int>(coord_float));
			}
			else
			{
				coord_0 = 1 % resolution;
				coord_2 = (resolution - 1) % resolution;
				coord_3 = (resolution - 2) % resolution;
				coord_decimal_part = -coord_float;
			}
		}
	}
	else
	{
		coord_1 = std::max(0, std::min(resolution - 1, (static_cast<int>(coord_float))));
		if(coord_float > 0.f) coord_2 = std::min(resolution - 1, coord_1 + 1);
		else coord_2 = 0;
		coord_0 = std::max(0, coord_1 - 1);
		coord_3 = std::min(resolution - 1, coord_2 + 1);
		coord_decimal_part = coord_float - floor(coord_float);
	}
}

Rgba ImageTexture::noInterpolation(const Point3 &p, int mipmap_level) const
{
	const int resx = images_.at(mipmap_level)->getWidth();
	const int resy = images_.at(mipmap_level)->getHeight();

	const float xf = (static_cast<float>(resx) * (p.x_ - floor(p.x_)));
	const float yf = (static_cast<float>(resy) * (p.y_ - floor(p.y_)));

	int x_0, x_1, x_2, x_3, y_0, y_1, y_2, y_3;
	float dx, dy;
	findTextureInterpolationCoordinates(x_0, x_1, x_2, x_3, dx, xf, resx, tex_clip_mode_ == ClipMode::Repeat, mirror_x_);
	findTextureInterpolationCoordinates(y_0, y_1, y_2, y_3, dy, yf, resy, tex_clip_mode_ == ClipMode::Repeat, mirror_y_);
	return images_.at(mipmap_level)->getColor(x_1, y_1);
}

Rgba ImageTexture::bilinearInterpolation(const Point3 &p, int mipmap_level) const
{
	const int resx = images_.at(mipmap_level)->getWidth();
	const int resy = images_.at(mipmap_level)->getHeight();

	const float xf = (static_cast<float>(resx) * (p.x_ - floor(p.x_))) - 0.5f;
	const float yf = (static_cast<float>(resy) * (p.y_ - floor(p.y_))) - 0.5f;

	int x_0, x_1, x_2, x_3, y_0, y_1, y_2, y_3;
	float dx, dy;
	findTextureInterpolationCoordinates(x_0, x_1, x_2, x_3, dx, xf, resx, tex_clip_mode_ == ClipMode::Repeat, mirror_x_);
	findTextureInterpolationCoordinates(y_0, y_1, y_2, y_3, dy, yf, resy, tex_clip_mode_ == ClipMode::Repeat, mirror_y_);

	const Rgba c_11 = images_.at(mipmap_level)->getColor(x_1, y_1);
	const Rgba c_21 = images_.at(mipmap_level)->getColor(x_2, y_1);
	const Rgba c_12 = images_.at(mipmap_level)->getColor(x_1, y_2);
	const Rgba c_22 = images_.at(mipmap_level)->getColor(x_2, y_2);

	const float w_11 = (1 - dx) * (1 - dy);
	const float w_12 = (1 - dx) * dy;
	const float w_21 = dx * (1 - dy);
	const float w_22 = dx * dy;

	return (w_11 * c_11) + (w_12 * c_12) + (w_21 * c_21) + (w_22 * c_22);
}

Rgba ImageTexture::bicubicInterpolation(const Point3 &p, int mipmap_level) const
{
	const int resx = images_.at(mipmap_level)->getWidth();
	const int resy = images_.at(mipmap_level)->getHeight();

	const float xf = (static_cast<float>(resx) * (p.x_ - floor(p.x_))) - 0.5f;
	const float yf = (static_cast<float>(resy) * (p.y_ - floor(p.y_))) - 0.5f;

	int x_0, x_1, x_2, x_3, y_0, y_1, y_2, y_3;
	float dx, dy;
	findTextureInterpolationCoordinates(x_0, x_1, x_2, x_3, dx, xf, resx, tex_clip_mode_ == ClipMode::Repeat, mirror_x_);
	findTextureInterpolationCoordinates(y_0, y_1, y_2, y_3, dy, yf, resy, tex_clip_mode_ == ClipMode::Repeat, mirror_y_);

	const Rgba c_00 = images_.at(mipmap_level)->getColor(x_0, y_0);
	const Rgba c_01 = images_.at(mipmap_level)->getColor(x_0, y_1);
	const Rgba c_02 = images_.at(mipmap_level)->getColor(x_0, y_2);
	const Rgba c_03 = images_.at(mipmap_level)->getColor(x_0, y_3);

	const Rgba c_10 = images_.at(mipmap_level)->getColor(x_1, y_0);
	const Rgba c_11 = images_.at(mipmap_level)->getColor(x_1, y_1);
	const Rgba c_12 = images_.at(mipmap_level)->getColor(x_1, y_2);
	const Rgba c_13 = images_.at(mipmap_level)->getColor(x_1, y_3);

	const Rgba c_20 = images_.at(mipmap_level)->getColor(x_2, y_0);
	const Rgba c_21 = images_.at(mipmap_level)->getColor(x_2, y_1);
	const Rgba c_22 = images_.at(mipmap_level)->getColor(x_2, y_2);
	const Rgba c_23 = images_.at(mipmap_level)->getColor(x_2, y_3);

	const Rgba c_30 = images_.at(mipmap_level)->getColor(x_3, y_0);
	const Rgba c_31 = images_.at(mipmap_level)->getColor(x_3, y_1);
	const Rgba c_32 = images_.at(mipmap_level)->getColor(x_3, y_2);
	const Rgba c_33 = images_.at(mipmap_level)->getColor(x_3, y_3);

	const Rgba cy_0 = math::cubicInterpolate(c_00, c_10, c_20, c_30, dx);
	const Rgba cy_1 = math::cubicInterpolate(c_01, c_11, c_21, c_31, dx);
	const Rgba cy_2 = math::cubicInterpolate(c_02, c_12, c_22, c_32, dx);
	const Rgba cy_3 = math::cubicInterpolate(c_03, c_13, c_23, c_33, dx);

	return math::cubicInterpolate(cy_0, cy_1, cy_2, cy_3, dy);
}

Rgba ImageTexture::mipMapsTrilinearInterpolation(const Point3 &p, const MipMapParams *mipmap_params) const
{
	const float ds = std::max(std::abs(mipmap_params->ds_dx_), std::abs(mipmap_params->ds_dy_)) * images_.at(0)->getWidth();
	const float dt = std::max(std::abs(mipmap_params->dt_dx_), std::abs(mipmap_params->dt_dy_)) * images_.at(0)->getHeight();
	float mipmap_level = 0.5f * math::log2(ds * ds + dt * dt);

	if(mipmap_params->force_image_level_ > 0.f) mipmap_level = mipmap_params->force_image_level_ * static_cast<float>(images_.size() - 1);

	mipmap_level += trilinear_level_bias_;

	mipmap_level = std::min(std::max(0.f, mipmap_level), static_cast<float>(images_.size() - 1));

	const int mipmap_level_a = static_cast<int>(floor(mipmap_level));
	const int mipmap_level_b = static_cast<int>(ceil(mipmap_level));
	const float mipmap_level_delta = mipmap_level - static_cast<float>(mipmap_level_a);

	Rgba col = bilinearInterpolation(p, mipmap_level_a);
	const Rgba col_b = bilinearInterpolation(p, mipmap_level_b);

	col.blend(col_b, mipmap_level_delta);
	return col;
}

//All EWA interpolation/calculation code has been adapted from PBRT v2 (https://github.com/mmp/pbrt-v2). see LICENSES file

Rgba ImageTexture::mipMapsEwaInterpolation(const Point3 &p, float max_anisotropy, const MipMapParams *mipmap_params) const
{
	float ds_0 = std::abs(mipmap_params->ds_dx_);
	float ds_1 = std::abs(mipmap_params->ds_dy_);
	float dt_0 = std::abs(mipmap_params->dt_dx_);
	float dt_1 = std::abs(mipmap_params->dt_dy_);

	if((ds_0 * ds_0 + dt_0 * dt_0) < (ds_1 * ds_1 + dt_1 * dt_1))
	{
		std::swap(ds_0, ds_1);
		std::swap(dt_0, dt_1);
	}

	const float major_length = sqrtf(ds_0 * ds_0 + dt_0 * dt_0);
	float minor_length = sqrtf(ds_1 * ds_1 + dt_1 * dt_1);

	if((minor_length * max_anisotropy < major_length) && (minor_length > 0.f))
	{
		const float scale = major_length / (minor_length * max_anisotropy);
		ds_1 *= scale;
		dt_1 *= scale;
		minor_length *= scale;
	}

	if(minor_length <= 0.f) return bilinearInterpolation(p);

	float mipmap_level = static_cast<float>(images_.size() - 1) - 1.f + math::log2(minor_length);
	mipmap_level = std::min(std::max(0.f, mipmap_level), static_cast<float>(images_.size() - 1));

	const int mipmap_level_a = static_cast<int>(floor(mipmap_level));
	const int mipmap_level_b = static_cast<int>(ceil(mipmap_level));
	const float mipmap_level_delta = mipmap_level - static_cast<float>(mipmap_level_a);

	Rgba col = ewaEllipticCalculation(p, ds_0, dt_0, ds_1, dt_1, mipmap_level_a);
	const Rgba col_b = ewaEllipticCalculation(p, ds_0, dt_0, ds_1, dt_1, mipmap_level_b);

	col.blend(col_b, mipmap_level_delta);

	return col;
}

Rgba ImageTexture::ewaEllipticCalculation(const Point3 &p, float ds_0, float dt_0, float ds_1, float dt_1, int mipmap_level) const
{
	if(mipmap_level >= static_cast<float>(images_.size() - 1))
	{
		const int resx = images_.at(0)->getWidth();
		const int resy = images_.at(0)->getHeight();
		return images_.at(images_.size() - 1)->getColor(math::mod(static_cast<int>(p.x_), resx), math::mod(static_cast<int>(p.y_), resy));
	}

	const int resx = images_.at(mipmap_level)->getWidth();
	const int resy = images_.at(mipmap_level)->getHeight();

	const float xf = (static_cast<float>(resx) * (p.x_ - floor(p.x_))) - 0.5f;
	const float yf = (static_cast<float>(resy) * (p.y_ - floor(p.y_))) - 0.5f;

	ds_0 *= resx;
	ds_1 *= resx;
	dt_0 *= resy;
	dt_1 *= resy;

	float a = dt_0 * dt_0 + dt_1 * dt_1 + 1;
	float b = -2.f * (ds_0 * dt_0 + ds_1 * dt_1);
	float c = ds_0 * ds_0 + ds_1 * ds_1 + 1;
	const float inv_f = 1.f / (a * c - b * b * 0.25f);
	a *= inv_f;
	b *= inv_f;
	c *= inv_f;

	const float det = -b * b + 4.f * a * c;
	const float inv_det = 1.f / det;
	const float u_sqrt = sqrtf(det * c);
	const float v_sqrt = sqrtf(a * det);

	const int s_0 = static_cast<int>(ceilf(xf - 2.f * inv_det * u_sqrt));
	const int s_1 = static_cast<int>(floorf(xf + 2.f * inv_det * u_sqrt));
	const int t_0 = static_cast<int>(ceilf(yf - 2.f * inv_det * v_sqrt));
	const int t_1 = static_cast<int>(floorf(yf + 2.f * inv_det * v_sqrt));

	Rgba sum_col(0.f);

	float sum_wts = 0.f;
	for(int it = t_0; it <= t_1; ++it)
	{
		const float tt = it - yf;
		for(int is = s_0; is <= s_1; ++is)
		{
			const float ss = is - xf;
			const float r_2 = a * ss * ss + b * ss * tt + c * tt * tt;
			if(r_2 < 1.f)
			{
				const float weight = ewa_weight_lut_[std::min(static_cast<int>(floorf(r_2 * ewa_weight_lut_size_)), ewa_weight_lut_size_ - 1)];
				const int ismod = math::mod(is, resx);
				const int itmod = math::mod(it, resy);
				sum_col += images_.at(mipmap_level)->getColor(ismod, itmod) * weight;
				sum_wts += weight;
			}
		}
	}
	if(sum_wts > 0.f) sum_col = sum_col / sum_wts;
	else sum_col = Rgba(0.f);
	return sum_col;
}

void ImageTexture::generateEwaLookupTable()
{
	if(!ewa_weight_lut_)
	{
		if(Y_LOG_HAS_DEBUG) Y_DEBUG << "** GENERATING EWA LOOKUP **" << YENDL;
		ewa_weight_lut_ = static_cast<float *>(malloc(sizeof(float) * ewa_weight_lut_size_));
		for(int i = 0; i < ewa_weight_lut_size_; ++i)
		{
			const float alpha = 2.f;
			const float r_2 = float(i) / float(ewa_weight_lut_size_ - 1);
			ewa_weight_lut_[i] = expf(-alpha * r_2) - expf(-alpha);
		}
	}
}

void ImageTexture::generateMipMaps()
{
	if(images_.empty()) return;

#ifdef HAVE_OPENCV
	int img_index = 0;
	//bool blur_seamless = true;
	int w = images_.at(0)->getWidth();
	int h = images_.at(0)->getHeight();

	if(Y_LOG_HAS_VERBOSE) Y_VERBOSE << "Format: generating mipmaps for texture of resolution [" << w << " x " << h << "]" << YENDL;

	const cv::Mat a(h, w, CV_32FC4);
	cv::Mat_<cv::Vec4f> a_vec = a;

	for(int j = 0; j < h; ++j)
	{
		for(int i = 0; i < w; ++i)
		{
			Rgba color = images_[img_index]->getColor(i, j);
			a_vec(j, i)[0] = color.getR();
			a_vec(j, i)[1] = color.getG();
			a_vec(j, i)[2] = color.getB();
			a_vec(j, i)[3] = color.getA();
		}
	}

	//Mipmap generation using the temporary full float buffer to reduce information loss
	while(w > 1 || h > 1)
	{
		int w_2 = (w + 1) / 2;
		int h_2 = (h + 1) / 2;
		++img_index;
		images_.emplace_back(Image::factory(w_2, h_2, images_[img_index - 1]->getType(), images_[img_index - 1]->getOptimization()));

		const cv::Mat b(h_2, w_2, CV_32FC4);
		const cv::Mat_<cv::Vec4f> b_vec = b;
		cv::resize(a, b, cv::Size(w_2, h_2), 0, 0, cv::INTER_AREA);

		for(int j = 0; j < h_2; ++j)
		{
			for(int i = 0; i < w_2; ++i)
			{
				Rgba tmp_col(0.f);
				tmp_col.r_ = b_vec(j, i)[0];
				tmp_col.g_ = b_vec(j, i)[1];
				tmp_col.b_ = b_vec(j, i)[2];
				tmp_col.a_ = b_vec(j, i)[3];
				images_[img_index]->setColor(i, j, tmp_col);
			}
		}
		w = w_2;
		h = h_2;
		if(Y_LOG_HAS_DEBUG) Y_DEBUG << "Format: generated mipmap " << img_index << " [" << w_2 << " x " << h_2 << "]" << YENDL;
	}

	if(Y_LOG_HAS_VERBOSE) Y_VERBOSE << "Format: mipmap generation done: " << img_index << " mipmaps generated." << YENDL;
#else
	Y_WARNING << "Format: cannot generate mipmaps, YafaRay was not built with OpenCV support which is needed for mipmap processing." << YENDL;
#endif
}

ImageTexture::ClipMode string2Cliptype_global(const std::string &clipname)
{
	// default "repeat"
	ImageTexture::ClipMode	tex_clipmode = ImageTexture::ClipMode::Repeat;
	if(clipname.empty()) return tex_clipmode;
	if(clipname == "extend")		tex_clipmode = ImageTexture::ClipMode::Extend;
	else if(clipname == "clip")		tex_clipmode = ImageTexture::ClipMode::Clip;
	else if(clipname == "clipcube")	tex_clipmode = ImageTexture::ClipMode::ClipCube;
	else if(clipname == "checker")	tex_clipmode = ImageTexture::ClipMode::Checker;
	return tex_clipmode;
}

std::unique_ptr<Texture> ImageTexture::factory(ParamMap &params, const Scene &scene)
{
	std::string name;
	std::string interpolation_type_str;
	double gamma = 1.0;
	double expadj = 0.0;
	bool normalmap = false;
	std::string color_space_str = "Raw_Manual_Gamma";
	std::string image_optimization_str = "optimized";
	bool img_grayscale = false;
	params.getParam("interpolate", interpolation_type_str);
	params.getParam("color_space", color_space_str);
	params.getParam("gamma", gamma);
	params.getParam("exposure_adjust", expadj);
	params.getParam("normalmap", normalmap);
	params.getParam("filename", name);
	params.getParam("image_optimization", image_optimization_str);
	params.getParam("img_grayscale", img_grayscale);

	if(name.empty())
	{
		Y_ERROR << "ImageTexture: Required argument filename not found for image texture" << YENDL;
		return nullptr;
	}

	const InterpolationType interpolation_type = Texture::getInterpolationTypeFromName(interpolation_type_str);
	ColorSpace color_space = Rgb::colorSpaceFromName(color_space_str);
	Image::Optimization image_optimization = Image::getOptimizationTypeFromName(image_optimization_str);
	const Path path(name);

	ParamMap format_params;
	format_params["type"] = toLower_global(path.getExtension());
	std::unique_ptr<Format> format = std::unique_ptr<Format>(Format::factory(format_params));
	if(!format)
	{
		Y_ERROR << "ImageTexture: Couldn't create image handler, dropping texture." << YENDL;
		return nullptr;
	}

	if(format->isHdr())
	{
		if(color_space != ColorSpace::LinearRgb && Y_LOG_HAS_VERBOSE) Y_VERBOSE << "ImageTexture: The image is a HDR/EXR file: forcing linear RGB and ignoring selected color space '" << color_space_str << "' and the gamma setting." << YENDL;
		color_space = LinearRgb;
		if(image_optimization_str != "none" && Y_LOG_HAS_VERBOSE) Y_VERBOSE << "ImageTexture: The image is a HDR/EXR file: forcing texture optimization to 'none' and ignoring selected texture optimization '" << image_optimization_str << "'" << YENDL;
		image_optimization = Image::Optimization::None;
	}

	format->setGrayScaleSetting(img_grayscale);

	std::unique_ptr<Image> image = format->loadFromFile(name, image_optimization, color_space, gamma);
	if(!image)
	{
		Y_ERROR << "ImageTexture: Couldn't load image file, dropping texture." << YENDL;
		return nullptr;
	}

	auto tex = std::unique_ptr<ImageTexture>(new ImageTexture(std::move(image)));
	if(!tex) //FIXME: this will never be true, replace by exception handling??
	{
		Y_ERROR << "ImageTexture: Couldn't create image texture." << YENDL;
		return nullptr;
	}

	tex->original_image_file_color_space_ = color_space;
	tex->original_image_file_gamma_ = gamma;

	if(interpolation_type == InterpolationType::Trilinear || interpolation_type == InterpolationType::Ewa)
	{
		tex->generateMipMaps();
		if(!session_global.getDifferentialRaysEnabled())
		{
			if(Y_LOG_HAS_VERBOSE) Y_VERBOSE << "At least one texture using mipmaps interpolation, enabling ray differentials." << YENDL;
			session_global.setDifferentialRaysEnabled(true);	//If there is at least one texture using mipmaps, then enable differential rays in the rendering process.
		}

		/*//FIXME DAVID: TEST SAVING MIPMAPS. CAREFUL: IT COULD CAUSE CRASHES!
		for(int i=0; i<=format->getHighestImgIndex(); ++i)
		{
			std::stringstream ss;
			ss << "//tmp//saved_mipmap_" << ihname << "_global" << i;
			format->saveToFile(ss.str(), i);
		}*/
	}

	// setup image
	bool rot_90 = false;
	bool even_tiles = false, odd_tiles = true;
	bool use_alpha = true, calc_alpha = false;
	int xrep = 1, yrep = 1;
	double minx = 0.0, miny = 0.0, maxx = 1.0, maxy = 1.0;
	double cdist = 0.0;
	std::string clipmode;
	bool mirror_x = false;
	bool mirror_y = false;
	float intensity = 1.f, contrast = 1.f, saturation = 1.f, hue = 0.f, factor_red = 1.f, factor_green = 1.f, factor_blue = 1.f;
	bool clamp = false;
	float trilinear_level_bias = 0.f;
	float ewa_max_anisotropy = 8.f;

	params.getParam("xrepeat", xrep);
	params.getParam("yrepeat", yrep);
	params.getParam("cropmin_x", minx);
	params.getParam("cropmin_y", miny);
	params.getParam("cropmax_x", maxx);
	params.getParam("cropmax_y", maxy);
	params.getParam("rot90", rot_90);
	params.getParam("clipping", clipmode);
	params.getParam("even_tiles", even_tiles);
	params.getParam("odd_tiles", odd_tiles);
	params.getParam("checker_dist", cdist);
	params.getParam("use_alpha", use_alpha);
	params.getParam("calc_alpha", calc_alpha);
	params.getParam("mirror_x", mirror_x);
	params.getParam("mirror_y", mirror_y);
	params.getParam("trilinear_level_bias", trilinear_level_bias);
	params.getParam("ewa_max_anisotropy", ewa_max_anisotropy);

	params.getParam("adj_mult_factor_red", factor_red);
	params.getParam("adj_mult_factor_green", factor_green);
	params.getParam("adj_mult_factor_blue", factor_blue);
	params.getParam("adj_intensity", intensity);
	params.getParam("adj_contrast", contrast);
	params.getParam("adj_saturation", saturation);
	params.getParam("adj_hue", hue);
	params.getParam("adj_clamp", clamp);

	tex->xrepeat_ = xrep;
	tex->yrepeat_ = yrep;
	tex->rot_90_ = rot_90;
	tex->setCrop(minx, miny, maxx, maxy);
	tex->calc_alpha_ = calc_alpha;
	tex->normalmap_ = normalmap;
	tex->tex_clip_mode_ = string2Cliptype_global(clipmode);
	tex->checker_even_ = even_tiles;
	tex->checker_odd_ = odd_tiles;
	tex->checker_dist_ = cdist;
	tex->mirror_x_ = mirror_x;
	tex->mirror_y_ = mirror_y;

	tex->setAdjustments(intensity, contrast, saturation, hue, clamp, factor_red, factor_green, factor_blue);

	tex->trilinear_level_bias_ = trilinear_level_bias;
	tex->ewa_max_anisotropy_ = ewa_max_anisotropy;

	if(interpolation_type == InterpolationType::Ewa) tex->generateEwaLookupTable();

	return tex;
}

END_YAFARAY
