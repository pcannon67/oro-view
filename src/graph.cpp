/*
    Copyright (c) 2010 Séverin Lemaignan (slemaign@laas.fr)

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version
    3 of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>

#include <iterator>
#include <utility>
#include <fstream>
#include <iostream>

#include "oroview.h"
#include "macros.h"
#include "graph.h"
#include "edge.h"
#include "node_relation.h"

using namespace std;
using namespace boost;

Graph::Graph()
{
}


void Graph::step(float dt) {

    BOOST_FOREACH(Edge& e, edges) {
        e.step(*this, dt);
    }

    BOOST_FOREACH(NodeMap::value_type& n, nodes) {

        n.second.step(*this, dt);
    }
}

void Graph::render(rendering_mode mode, OroView& env, bool debug) {

    // Renders edges
    BOOST_FOREACH(Edge& e, edges) {
        e.render(mode, env);
    }

    // Renders nodes
    BOOST_FOREACH(NodeMap::value_type& n, nodes) {
        n.second.render(mode, env, debug);
    }

}

const Graph::NodeMap& Graph::getNodes() const {
    return nodes;
}

Node& Graph::getNode(const string& id) {

    AliasMap::iterator it = aliases.find(hash_value(id));

    if (it == aliases.end())
        throw OroViewException("Node " + id + " not found");

    return *(it->second);

}

const Node& Graph::getConstNode(const string& id) const {

    AliasMap::const_iterator it = aliases.find(hash_value(id));

    if (it == aliases.end())
        throw OroViewException("Node " + id + " not found");

    return *(it->second);

}

Node* Graph::getNodeByTagID(int tagid) {

    NodeMap::iterator it = nodes.find(tagid);

    if (it == nodes.end())
        return NULL;

    return &(it->second);

}

Node& Graph::getRandomNode() {
    NodeMap::iterator it = nodes.begin();
    advance( it, rand()%nodes.size());
    return it->second;
}

void Graph::select(Node *node) {

   //Already selected?
    if (node->selected) return;

    node->setSelected(true);
    selectedNodes.insert(node);

    updateDistances();
}

void Graph::deselect(Node *node){

    //Already deselected?
    if (!node->selected) return;

    node->setSelected(false);
    selectedNodes.erase(node);

    updateDistances();
}

void Graph::clearSelect(){
    BOOST_FOREACH(Node* node, selectedNodes) {
        node->setSelected(false);
    }

    selectedNodes.clear();

    updateDistances();
}

Node* Graph::getSelected() {
    if (selectedNodes.size() == 1)
        return *selectedNodes.begin();

    return NULL;
}

void Graph::addAlias(const string& alias, const string& id) {

    aliases.insert(make_pair(hash_value(alias),&(getNode(id))));
}

Node& Graph::addNode(const string& id, const string& label, const Node* neighbour, node_type type) {

    pair<NodeMap::iterator, bool> res;

    //TODO: I'm doing 2 !! copies of Node, here??

    res = nodes.insert(make_pair(hash_value(id),Node(id, label, neighbour, type)));

    if ( ! res.second )
        TRACE("Didn't add node " << id << " because it already exists.");
    else {
        TRACE("Added node " << id);
        aliases.insert(make_pair(hash_value(id),&res.first->second));
        updateDistances();
    }

    return res.first->second;
}

/**
Ask the graph to create the edge for this relation. If an edge already exist between the two nodes,
it will be reused.
*/
void Graph::addEdge(Node& from, Node& to, const relation_type type, const std::string& label) {

    NodeRelation& rel = from.addRelation(to, type, label);

    //Don't add an edge if the relation is between the same node.
    //It could be actually useful, but it provokes a segfault somewhere :-/
    if (&from == &to) {
        TRACE("Leaving immediately because of strange segfault: from == to");
        return;
    }

    if (getEdgesBetween(from, to).size() == 0)
        //so now we are confident that there's no edge we can reuse. Let's create a new one.
        edges.push_back(Edge(rel, label));


    return;
}



vector<const Edge*>  Graph::getEdgesFor(const Node& node) const{
    vector<const Edge*> res;

    BOOST_FOREACH(const Edge& e, edges) {
        if (e.getId1() == node.getID() ||
                e.getId2() == node.getID())
            res.push_back(&e);
    }
    return res;
}

vector<Edge*>  Graph::getEdgesBetween(const Node& node1, const Node& node2){
    vector<Edge*> res;

    BOOST_FOREACH(Edge& e, edges) {
        if ((e.getId1() == node1.getID() && e.getId2() == node2.getID()) ||
                (e.getId1() == node2.getID() && e.getId2() == node1.getID()))
            res.push_back(&e);
    }
    return res;
}

void Graph::updateDistances() {

    // No node selected, set all distance to -1
    if (selectedNodes.empty()) {
        // Renders nodes
        BOOST_FOREACH(NodeMap::value_type& n, nodes) {
            n.second.distance_to_selected = -1;
        }

        return;
    }
    //Else, start from the selected node
    BOOST_FOREACH(NodeMap::value_type& n, nodes) {
        n.second.distance_to_selected_updated = false;
    }

    BOOST_FOREACH(Node* node, selectedNodes) {
        recurseUpdateDistances(node, NULL, 0);
    }

}

void Graph::recurseUpdateDistances(Node* node, Node* parent, int distance) {
    node->distance_to_selected = distance;
    node->distance_to_selected_updated = true;
    TRACE("Node " << node->getID() << " is at " << distance << " nodes from closest selected");

    BOOST_FOREACH(Node* n, node->getConnectedNodes()){
        if (n != parent &&
            (!n->distance_to_selected_updated || distance < n->distance_to_selected))
                recurseUpdateDistances(n, node, distance + 1);
    }
}

int Graph::nodesCount() {
    return nodes.size();
}

int Graph::edgesCount() {
    return edges.size();
}

vec2f Graph::coulombRepulsionFor(const Node& node) const {

    vec2f force(0.0, 0.0);

    //TODO: a simple optimization can be to compute Coulomb force
    //at the same time than Hooke force when possible -> one
    // less distance computation (not sure it makes a big difference)

    BOOST_FOREACH(const NodeMap::value_type& nm, nodes) {
        const Node& n = nm.second;
        if (&n != &node) {
            vec2f delta = n.pos - node.pos;

            //Coulomb repulsion force is in 1/r^2
            float len = delta.length2();
            if (len < 0.01) len = 0.01; //avoid dividing by zero

            float f = COULOMB_CONSTANT * n.charge * node.charge / len;

            force += project(f, delta);
        }
    }

    return force;

}

vec2f Graph::coulombRepulsionAt(const vec2f& pos) const {

    vec2f force(0.0, 0.0);

    //TODO: a simple optimization can be to compute Coulomb force
    //at the same time than Hooke force when possible -> one
    // less distance computation (not sure it makes a big difference)

    BOOST_FOREACH(const NodeMap::value_type& nm, nodes) {
        const Node& n = nm.second;

        vec2f delta = n.pos - pos;

        //Coulomb repulsion force is in 1/r^2
        float len = delta.length2();
        if (len < 0.01) len = 0.01; //avoid dividing by zero

        float f = COULOMB_CONSTANT * n.charge * INITIAL_CHARGE / len;

        force += project(f, delta);
    }

    return force;

}

vec2f Graph::hookeAttractionFor(const Node& node) const {

    vec2f force(0.0, 0.0);

    //TODO: a simple optimization can be to compute Coulomb force
    //at the same time than Hooke force when possible -> one
    // less distance computation (not sure it makes a big difference)

    BOOST_FOREACH(const Edge* e, getEdgesFor(node)) {
        const Node& n_tmp = getConstNode(e->getId1());

        //Retrieve the node at the edge other extremity
        const Node& n2 = ( (&n_tmp != &node) ? n_tmp : getConstNode(e->getId2()) );

        TRACE("\tComputing Hooke force from " << node.getID() << " to " << n2.getID());

        vec2f delta = n2.pos - node.pos;

        float f = - e->spring_constant * (e->length - e->nominal_length);

        force += project(f, delta);
    }

    return force;
}

vec2f Graph::gravityFor(const Node& node) const {
    //Gravity... well, it's actually more like anti-gravity, since it's in:
    // f = g * m * d
    vec2f force(0.0, 0.0);

    float len = node.pos.length2();

    if (len < 0.01) len = 0.01; //avoid dividing by zero

    float f = GRAVITY_CONSTANT * node.mass * len * 0.01;

    force += project(f, node.pos);

    return force;
}

vec2f Graph::project(float force, const vec2f& d) const {
    //we need to project this force on x and y
    //-> Fx = F.cos(arctan(Dy/Dx)) = F/sqrt(1-(Dy/Dx)^2)
    //-> Fy = F.sin(arctan(Dy/Dx)) = F.(Dy/Dx)/sqrt(1-(Dy/Dx)^2)
    vec2f res(0.0, 0.0);

    TRACE("\tForce: " << force << " - Delta: (" << d.x << ", " << d.y << ")");

    if (d.y == 0.0) {
        res.x = force;
        return res;
    }

    if (d.x == 0.0) {
        res.y = force;
        return res;
    }

    float dydx = d.y/d.x;
    float sqdydx = 1/sqrt(1 + dydx * dydx);

    res.x = force * sqdydx;
    if (d.x > 0.0) res.x = - res.x;
    res.y = force * sqdydx * abs(dydx);
    if (d.y > 0.0) res.y = - res.y;

    TRACE("\t-> After projection: Fx=" << res.x << ", Fy=" << res.y);

    return res;
}

void Graph::saveToGraphViz(OroView& env) {

    env.graphvizGraph << "strict digraph ontology {\n";

    // Renders edges
    BOOST_FOREACH(Edge& e, edges) {
        e.render(GRAPHVIZ, env);
    }

    // Renders nodes
    BOOST_FOREACH(NodeMap::value_type& n, nodes) {
        n.second.render(GRAPHVIZ, env, false);
    }

    env.graphvizGraph << "}\n";

    ofstream graphvizFile;
    graphvizFile.open ("ontology.dot");
    graphvizFile << env.graphvizGraph.str();
    graphvizFile.close();

    cout << "Model correctly exported to ontology.dot" << endl;
}

