#ifndef VEC_CONE_H_
#define VEC_CONE_H_

#include "vecmath/vecmath.h"

class VecCone
{
public:
	VecCone(const vec3f &center, const double x);

	vec3f Generate() const;

private:
	vec3f m_center;
	double m_x;
	mat3f m_transform;
};

#endif
