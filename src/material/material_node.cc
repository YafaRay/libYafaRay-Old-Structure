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

#include "material/material_node.h"
#include "common/logger.h"
#include "common/param.h"
#include "shader/shader_node.h"

BEGIN_YAFARAY

void recursiveSolver_global(ShaderNode *node, std::vector<ShaderNode *> &sorted)
{
	if(node->getId() != 0) return;
	node->setId(1);
	std::vector<const ShaderNode *> dependency_nodes;
	if(node->getDependencies(dependency_nodes))
	{
		for(const auto &dependency_node : dependency_nodes)
			// FIXME someone tell me a smarter way than casting away a const...
			if(dependency_node->getId() == 0) recursiveSolver_global((ShaderNode *) dependency_node, sorted);
	}
	sorted.push_back(node);
}

void recursiveFinder_global(const ShaderNode *node, std::set<const ShaderNode *> &tree)
{
	std::vector<const ShaderNode *> dependency_nodes;
	if(node->getDependencies(dependency_nodes))
	{
		for(const auto &dependency_node : dependency_nodes)
		{
			tree.insert(dependency_node);
			recursiveFinder_global(dependency_node, tree);
		}
	}
	tree.insert(node);
}

NodeMaterial::~NodeMaterial()
{
}

void NodeMaterial::evalNodes(const RenderData &render_data, const SurfacePoint &sp, const std::vector<ShaderNode *> &nodes, NodeStack &stack) const {
	for(const auto &node : nodes) node->eval(stack, render_data, sp);
}

void NodeMaterial::solveNodesOrder(const std::vector<ShaderNode *> &roots)
{
	//set all IDs = 0 to indicate "not tested yet"
	for(unsigned int i = 0; i < color_nodes_.size(); ++i) color_nodes_[i]->setId(0);
	for(unsigned int i = 0; i < roots.size(); ++i) recursiveSolver_global(roots[i], color_nodes_sorted_);
	if(color_nodes_.size() != color_nodes_sorted_.size()) Y_WARNING << "NodeMaterial: Unreachable nodes!" << YENDL;
	//give the nodes an index to be used as the "stack"-index.
	//using the order of evaluation can't hurt, can it?
	for(unsigned int i = 0; i < color_nodes_sorted_.size(); ++i)
	{
		ShaderNode *n = color_nodes_sorted_[i];
		n->setId(i);
	}
	req_node_mem_ = color_nodes_sorted_.size() * sizeof(NodeResult);
}

/*! get a list of all nodes that are in the tree given by root
	prerequisite: nodes have been successfully loaded and stored into allSorted
	since "solveNodesOrder" sorts allNodes, calling getNodeList afterwards gives
	a list in evaluation order. multiple calls are merged in "nodes" */

void NodeMaterial::getNodeList(const ShaderNode *root, std::vector<ShaderNode *> &nodes)
{
	std::set<const ShaderNode *> in_tree;
	for(const auto &node : nodes) in_tree.insert(node);
	recursiveFinder_global(root, in_tree);
	nodes.clear();
	for(const auto &node : color_nodes_sorted_) if(in_tree.find(node) != in_tree.end()) nodes.push_back(node);
}

void NodeMaterial::evalBump(NodeStack &stack, const RenderData &render_data, SurfacePoint &sp, const ShaderNode *bump_shader_node) const
{
	for(const auto &node : bump_nodes_) node->evalDerivative(stack, render_data, sp);
	float du, dv;
	bump_shader_node->getDerivative(stack, du, dv);
	applyBump(sp, du, dv);
}

bool NodeMaterial::loadNodes(const std::list<ParamMap> &params_list, Scene &scene)
{
	bool error = false;
	std::string type;
	std::string name;
	std::string element;

	for(const auto &param_map : params_list)
	{
		if(param_map.getParam("element", element))
		{
			if(element != "shader_node") continue;
		}
		else Y_WARNING << "NodeMaterial: No element type given; assuming shader node" << YENDL;

		if(!param_map.getParam("name", name))
		{
			Y_ERROR << "NodeMaterial: Name of shader node not specified!" << YENDL;
			error = true;
			break;
		}

		if(shaders_table_.find(name) != shaders_table_.end())
		{
			Y_ERROR << "NodeMaterial: Multiple nodes with identically names!" << YENDL;
			error = true;
			break;
		}

		if(!param_map.getParam("type", type))
		{
			Y_ERROR << "NodeMaterial: Type of shader node not specified!" << YENDL;
			error = true;
			break;
		}

		std::unique_ptr<ShaderNode> shader = ShaderNode::factory(param_map, scene);
		if(shader)
		{
			shaders_table_[name] = std::move(shader);
			color_nodes_.push_back(shaders_table_[name].get());
			if(Y_LOG_HAS_VERBOSE) Y_VERBOSE << "NodeMaterial: Added ShaderNode '" << name << "'! (" << (void *)shaders_table_[name].get() << ")" << YENDL;
		}
		else
		{
			Y_ERROR << "NodeMaterial: No shader node could be constructed.'" << type << "'!" << YENDL;
			error = true;
			break;
		}
	}

	if(!error) //configure node inputs
	{
		NodeFinder finder(shaders_table_);
		int n = 0;
		for(const auto &param_map : params_list)
		{
			if(!color_nodes_[n]->configInputs(param_map, finder))
			{
				Y_ERROR << "NodeMaterial: Shader node configuration failed! (n=" << n << ")" << YENDL;
				error = true; break;
			}
			++n;
		}
	}
	if(error) shaders_table_.clear();
	return !error;
}

void NodeMaterial::parseNodes(const ParamMap &params, std::vector<ShaderNode *> &roots, std::map<std::string, ShaderNode *> &node_list)
{
	std::string name;

	for(auto &current_node : node_list)
	{
		if(params.getParam(current_node.first, name))
		{
			const auto node_found = shaders_table_.find(name);
			if(node_found != shaders_table_.end())
			{
				current_node.second = node_found->second.get();
				roots.push_back(current_node.second);
			}
			else Y_WARNING << "Shader node " << current_node.first << " '" << name << "' does not exist!" << YENDL;
		}
	}
}

END_YAFARAY
