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

#ifndef YAFARAY_IMPORT_XML_H
#define YAFARAY_IMPORT_XML_H

#include "constants.h"
#include "common/param.h"
#include <list>
#include <vector>
#include <string>
#include <memory>

BEGIN_YAFARAY

class Scene;
class Scene;
class XmlParser;
enum ColorSpace : int;

LIBYAFARAY_EXPORT std::unique_ptr<Scene> parseXmlFile_global(const char *filename, ParamMap &render, const std::string &color_space_string, float input_gamma);

typedef void (*StartElementCb_t)(XmlParser &p, const char *element, const char **attrs);
typedef void (*EndElementCb_t)(XmlParser &p, const char *element);

#if HAVE_XML
struct ParserState
{
	StartElementCb_t start_;
	EndElementCb_t end_;
	std::string element_name_;
	int level_;
	std::string last_section_; //! to show last section previous to a XML Parser error
	std::string last_element_; //! to show last element previous to a XML Parser error
	std::string last_element_attrs_; //! to show last element attributes previous to a XML Parser error
};

class XmlParser
{
	public:
		XmlParser(ParamMap &r, ColorSpace input_color_space, float input_gamma);
		void pushState(StartElementCb_t start, EndElementCb_t end, const std::string &element_name);
		void popState();
		void startElement(const char *element, const char **attrs) { ++level_; if(current_) current_->start_(*this, element, attrs); }
		void endElement(const char *element)	{ if(current_) current_->end_(*this, element); --level_; }
		std::string stateElementName() { return current_->element_name_; }
		void setParam(const std::string &name, const Parameter &param) { (*cparams_)[name] = param; }
		int currLevel() const { return level_; }
		int stateLevel() const { return current_ ? current_->level_ : -1; }
		ColorSpace getInputColorSpace() const { return input_color_space_; }
		float getInputGamma() const { return input_gamma_; }
		void setLastSection(const std::string &section) { current_->last_section_ = section; }
		void setLastElementName(const char *element_name);
		void setLastElementNameAttrs(const char **element_attrs);
		std::string getLastSection() const { return current_->last_section_; }
		std::string getLastElementName() const { return current_->last_element_; }
		std::string getLastElementNameAttrs() const { return current_->last_element_attrs_; }
		std::unique_ptr<Scene> getScene() { return std::move(scene_); }

		std::unique_ptr<Scene> scene_;
		ParamMap params_, &render_;
		std::list<ParamMap> eparams_; //! for materials that need to define a whole shader tree etc.
		ParamMap *cparams_ = nullptr; //! just a pointer to the current paramMap, either params or a eparams element
	protected:
		std::vector<ParserState> state_stack_;
		ParserState *current_ = nullptr;
		int level_ = 0;
		float input_gamma_;
		ColorSpace input_color_space_;
};

// state callbacks:
void startElDocument_global(XmlParser &p, const char *element, const char **attrs);
void endElDocument_global(XmlParser &p, const char *element);
void startElScene_global(XmlParser &p, const char *element, const char **attrs);
void endElScene_global(XmlParser &p, const char *element);
void startElObject_global(XmlParser &p, const char *element, const char **attrs);
void endElObject_global(XmlParser &p, const char *element);
void startElInstance_global(XmlParser &p, const char *element, const char **attrs);
void endElInstance_global(XmlParser &p, const char *element);
void startElParammap_global(XmlParser &p, const char *element, const char **attrs);
void endElParammap_global(XmlParser &parser, const char *element);
void startElParamlist_global(XmlParser &p, const char *element, const char **attrs);
void endElParamlist_global(XmlParser &p, const char *element);
void endElRender_global(XmlParser &p, const char *element);

#endif // HAVE_XML

END_YAFARAY

#endif // YAFARAY_IMPORT_XML_H
