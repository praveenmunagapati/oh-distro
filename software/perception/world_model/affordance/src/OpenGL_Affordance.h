/*
 * OpenGLAffordance.h
 *
 *  Created on: Jan 14, 2013
 *      Author: mfleder
 */

#ifndef OPENGL_AFFORDANCE_H
#define OPENGL_AFFORDANCE_H

#include "ModelState.h"
#include "AffordanceState.h"
#include "opengl/opengl_object.h"
#include "opengl/opengl_object_cylinder.h"
#include "opengl/opengl_object_sphere.h"
#include "opengl/opengl_object_box.h"
#include "boost/shared_ptr.hpp"

namespace affordance
{

class OpenGL_Affordance: public opengl::OpenGL_Object
{
	//------------------fields
private:
	/**underlying affordance state*/
	const AffConstPtr _affordance; 

	//---we're drawing 1 of these
	opengl::OpenGL_Object_Box _box;
	opengl::OpenGL_Object_Cylinder _cylinder;
	opengl::OpenGL_Object_Sphere _sphere;

	//-----------------constructors
public:
	OpenGL_Affordance(AffConstPtr affordance, 
			  bool isHighlighted = false, 
			  Eigen::Vector3f highlightedColor = Eigen::Vector3f(1.0, 0.0, 0.0));
	virtual ~OpenGL_Affordance();

	//-------drawing
	virtual void draw();

	//observers
	AffConstPtr getAffordance() const;
};

} /* namespace affordance */
#endif /* OPENGL_AFFORDANCE_H */
