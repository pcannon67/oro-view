#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>

#include <algorithm>

#include "macros.h"

#include "graph.h"
#include "node.h"
#include "node_relation.h"


using namespace std;
using namespace boost;

Node::Node(string id) : id(id), renderer(NodeRenderer(hash_value(id)))
{
    pos = vec2f(100.0 * (float)rand()/RAND_MAX - 50 , 100 * (float)rand()/RAND_MAX - 50);

    //speed = vec2f(40.0 * (float)rand()/RAND_MAX - 20 , 40.0 * (float)rand()/RAND_MAX - 20);
    speed = vec2f(0.0, 0.0);

    mass = INITIAL_MASS;
    damping = INITIAL_DAMPING;

    charge = INITIAL_CHARGE;

    resetRenderers();

    selected = false;
}

bool Node::operator< (const Node& node2) const {
    return id < node2.getID();
}

const string& Node::getID() const {
    return id;
}

std::vector<NodeRelation>& Node::getRelations() {
    return relations;
}

void Node::addRelation(Node& to, const relation_type type, const std::string& label) {

    TRACE("**** Entering Node::addRelation ****");

    //When adding a new relation, we create as well an initial UNDEFINED reciprocal relation if
    //the destination node has no back relation.
    //And before adding a relation, we check there is no existing UNDEFINED relation.
    //If it's the case, we can safely replace it by a well defined relation.

//    if (type == UNDEFINED) {
//	//Add an undefined relation and return.
//	relations.push_back(NodeRelation(this, &to, UNDEFINED, ""));
//	TRACE("Added UNDEFINED back relation to " << to.getID());
//	return;
//    }

    vector<const NodeRelation*> rels = getRelationTo(to);

    relations.push_back(NodeRelation(this, &to, type, label)); //Add a new relation

    //Ask the graph to create the edge for this relation. If an edge already exist between the two nodes,
    //it will be reused.
    Graph::getInstance()->addEdge(relations.back()); // keep a reference if we need to define a backrelation.

    if(rels.size() == 1 && rels[0]->type == UNDEFINED) { //I had already an undefined relation to the destination node
	
	TRACE("Will replaced an old UNDEFINED relation by a better one!");

	//**** TODO !!! *****/

	//if (edge_p != NULL)
	//    edge_p->removeReferenceRelation(*this);

	//std::remove(relations.begin(), relations.end(), *(rels[0]));
    }

    TRACE("Added relation from " << id << " to " << to.getID());

}

vector<const NodeRelation*> Node::getRelationTo(Node& node) const {

    vector<const NodeRelation*> res;

    BOOST_FOREACH(const NodeRelation& rel, relations) {
		if (rel.to == &node)
			res.push_back(&rel);
	}
    return res;
}

void Node::resetRenderers(){
    stepDone = false;
    renderingDone = false;
}

vec2f Node::coulombRepulsionWith(const Node& node) const {

    //TODO: a simple optimization can be to compute Coulomb force
    //at the same time than Hooke force when possible -> one
    // less distance computation (not sure it makes a big difference)
    vec2f delta = node.pos - pos;

    if (isnan(pos.x) || isnan(node.pos.x)) throw OroViewException("NaN exception!");

    //Coulomb repulsion force is in 1/r^2
    float len = delta.length2();
    if (len < 0.01) len = 0.01; //avoid dividing by zero

    float f = COULOMB_CONSTANT * charge * node.charge / len;

    if(id == "node2") TRACE("\tCoulomb force from " << node.getID() << ": " << f);

    return project(f, delta);
}

vec2f Node::hookeAttractionWith(const NodeRelation& rel) const {

    if (rel.to == rel.from) return vec2f(0.0, 0.0); //don't compute spring force when no edge!

    Edge& e = rel.getEdge();
    float f = - e.spring_constant * (e.length - e.nominal_length);

    vec2f delta = rel.to->pos - pos;

    if(id == "node2") {
	TRACE("\t*** Hooke force ***");
	TRACE("\tNode " << id << ": pos=(" << pos.x << ", " << pos.y <<")");
	TRACE("\tNode " << rel.to->getID() << ": pos=(" << rel.to->pos.x << ", " << rel.to->pos.y <<")");
	TRACE("\tHooke force from " << rel.to->getID() << ": " << f);
    }

    return project(f, delta);
}

vec2f Node::project(float force, vec2f d) const {
    //we need to project this force on x and y
    //-> Fx = F.cos(arctan(Dy/Dx)) = F/sqrt(1-(Dy/Dx)^2)
    //-> Fy = F.sin(arctan(Dy/Dx)) = F.(Dy/Dx)/sqrt(1-(Dy/Dx)^2)
    vec2f res(0.0, 0.0);

    if (d.y == 0) {
	res.x = force;
	return res.x;
    }

    if (d.x == 0) {
	res.y = force;
	return res.y;
    }

    float dydx = d.y/d.x;
    float sqdydx = 1/sqrt(1 + dydx * dydx);

    res.x = force * sqdydx;
    if (d.x > 0.0) res.x = - res.x;
    res.y = force * sqdydx * abs(dydx);
    if (d.y > 0.0) res.y = - res.y;

    if(id == "node2") TRACE("\t-> After projection: Fx=" << res.x << ", Fy=" << res.y);

    return res;
}

void Node::updateKineticEnergy() {
    kinetic_energy = mass * speed.length2();
}

void Node::printEdgeLengths(){
//    BOOST_FOREACH(NodeRelation& rel, relations) {
//	Edge& tmp = rel.getEdge();
//	TRACE("Edge length between " << rel.from->getID() << " and " << rel.to->getID() << " is: " << tmp.length);
//    }

    for(vector<NodeRelation>::iterator it = relations.begin(); it != relations.end(); ++it) {
	NodeRelation& rel = *it;
	Edge& tmp = rel.getEdge();
	TRACE("Edge length between " << rel.from->getID() << " and " << rel.to->getID() << " is: " << tmp.length);
    }

}

void Node::step(float dt){

    if(!stepDone) {

	/** Compute here the new position of the node **/

	// Algo from Wikipedia -- http://en.wikipedia.org/wiki/Force-based_layout

	vec2f force = vec2f(0.0, 0.0);

//	BOOST_FOREACH(const Graph::NodeMap::value_type& node, Graph::getInstance()->getNodes()) {
//	    if (&(node.second) != this)
//		force += coulombRepulsionWith(node.second);
//	}
//
//	if(id == "node2") TRACE("** Coulomb force applying: Fx=" << force.x << ", Fy= " << force.y);

	if(id == "node2") printEdgeLengths();

	BOOST_FOREACH(const NodeRelation& rel, relations) {
	    force += hookeAttractionWith(rel);
	}
	if(id == "node2") TRACE("** Total force applying: Fx=" << force.x << ", Fy= " << force.y);

	speed = (speed + force * dt) * damping;

	updateKineticEnergy();

	//Check we have enough energy to move :)
	if (kinetic_energy > MIN_KINETIC_ENERGY) {
	    pos += speed * dt;
	}

	TRACE("Node " << id << ": pos=(" << pos.x << ", " << pos.y <<")");


	//TRACE("Step computed for " << id << ". Speed is " << speed.x << ", " << speed.y << " (energy: " << kinetic_energy << ").");
	stepDone = true;
    }
}

void Node::render(bool complete){

    if (!renderingDone) {
#ifndef TEXT_ONLY
	renderer.renderAt(pos);
#endif
	//TRACE("Node " << id << " rendered.");
	renderingDone = true;
    }
}

void Node::setSelected(bool select) {

    if ((select && selected)||
	(!select && !selected)) return;

    selected = select;
    renderer.setSelected(select);

    if(select) charge *= 2;
    else charge /= 2;

}
