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

#ifndef YAFARAY_TEXTURE_BASIC_H
#define YAFARAY_TEXTURE_BASIC_H

#include "texture/texture.h"
#include "texture/noise_generator.h"

BEGIN_YAFARAY

class CloudsTexture final : public Texture
{
	public:
		static std::unique_ptr<Texture> factory(ParamMap &params, const Scene &scene);

	private:
		enum BiasType : int { None, Positive, Negative };
		CloudsTexture(int dep, float sz, bool hd,
					  const Rgb &c_1, const Rgb &c_2,
					  const std::string &ntype, const std::string &btype);
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;;

		int depth_, bias_;
		float size_;
		bool hard_;
		Rgb color_1_, color_2_;
		std::unique_ptr<NoiseGenerator> n_gen_;
};


class MarbleTexture final : public Texture
{
	public:
		static std::unique_ptr<Texture> factory(ParamMap &params, const Scene &scene);

	private:
		MarbleTexture(int oct, float sz, const Rgb &c_1, const Rgb &c_2,
					  float turb, float shp, bool hrd, const std::string &ntype, const std::string &shape);

		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;;

		int octaves_;
		Rgb color_1_, color_2_;
		float turb_, sharpness_, size_;
		bool hard_;
		std::unique_ptr<NoiseGenerator> n_gen_;
		enum Shape {Sin, Saw, Tri} wshape_;
};

class WoodTexture final : public Texture
{
	public:
		static std::unique_ptr<Texture> factory(ParamMap &params, const Scene &scene);

	private:
		WoodTexture(int oct, float sz, const Rgb &c_1, const Rgb &c_2, float turb,
					bool hrd, const std::string &ntype, const std::string &wtype, const std::string &shape);

		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;;

		int octaves_;
		Rgb color_1_, color_2_;
		float turb_, size_;
		bool hard_, rings_;
		std::unique_ptr<NoiseGenerator> n_gen_;
		enum Shape {Sin, Saw, Tri} wshape_;
};

class VoronoiTexture final : public Texture
{
	public:
		static std::unique_ptr<Texture> factory(ParamMap &params, const Scene &scene);

	private:
		enum class ColorMode : int { IntensityWithoutColor, Position, PositionOutline, PositionOutlineIntensity};
		VoronoiTexture(const Rgb &c_1, const Rgb &c_2,
					   const ColorMode &color_mode,
					   float w_1, float w_2, float w_3, float w_4,
					   float mex, float sz,
					   float isc, const std::string &dname);
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;;

		float w_1_, w_2_, w_3_, w_4_;	// feature weights
		float aw_1_, aw_2_, aw_3_, aw_4_;	// absolute value of above
		float size_ = 1.f;
		ColorMode color_mode_ = ColorMode::IntensityWithoutColor;
		float intensity_scale_ = 1.f;
		VoronoiNoiseGenerator v_gen_;
};

class MusgraveTexture final : public Texture
{
	public:
		static std::unique_ptr<Texture> factory(ParamMap &params, const Scene &scene);

	private:
		MusgraveTexture(const Rgb &c_1, const Rgb &c_2,
						float h, float lacu, float octs, float offs, float gain,
						float size, float iscale,
						const std::string &ntype, const std::string &mtype);
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;;

		Rgb color_1_, color_2_;
		float size_, iscale_;
		std::unique_ptr<NoiseGenerator> n_gen_;
		std::unique_ptr<Musgrave> m_gen_;
};

class DistortedNoiseTexture final : public Texture
{
	public:
		static std::unique_ptr<Texture> factory(ParamMap &params, const Scene &scene);

	private:
		DistortedNoiseTexture(const Rgb &c_1, const Rgb &c_2,
							  float distort, float size,
							  const std::string &noiseb_1, const std::string noiseb_2);
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;;

		Rgb color_1_, color_2_;
		float distort_, size_;
		std::unique_ptr<NoiseGenerator> n_gen_1_, n_gen_2_;
};

/*! RGB cube.
	basically a simple visualization of the RGB color space,
	just goes r in x, g in y and b in z inside the unit cube. */

class RgbCubeTexture final : public Texture
{
	public:
		static std::unique_ptr<Texture> factory(ParamMap &params, const Scene &scene);

	private:
		RgbCubeTexture() = default;
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
};

class BlendTexture final : public Texture
{
	public:
		static std::unique_ptr<Texture> factory(ParamMap &params, const Scene &scene);

	private:
		enum ProgressionType : int { Linear, Quadratic, Easing, Diagonal, Spherical, QuadraticSphere, Radial };
		BlendTexture(const std::string &stype, bool use_flip_axis);
		virtual Rgba getColor(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;
		virtual float getFloat(const Point3 &p, const MipMapParams *mipmap_params = nullptr) const override;

		ProgressionType progression_type_ = Linear;
		bool use_flip_axis_ = false;
};

END_YAFARAY

#endif // YAFARAY_TEXTURE_BASIC_H
