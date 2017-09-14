// The main ray tracer.

#include <cmath>
#include <cstring>
#include <deque>

#include <Fl/fl_ask.H>

#include "RayTracer.h"
#include "scene/light.h"
#include "scene/material.h"
#include "scene/ray.h"
#include "fileio/read.h"
#include "fileio/parse.h"
#include "ui/TraceUI.h"
#include "global.h"
#include "vecCone.h"

using namespace std;

// Trace a top-level ray through normalized window coordinates (x,y)
// through the projection plane, and out into the scene.  All we do is
// enter the main ray-tracing method, getting things started by plugging
// in an initial ray weight of (0.0,0.0,0.0) and an initial recursion depth of 0.
vec3f RayTracer::trace(Scene *scene, double x, double y)
{
	ray r(vec3f(0, 0, 0), vec3f(0, 0, 0));
	scene->getCamera()->rayThrough(x, y, r);

	TraceSet rayset;
	rayset.scene = scene;
	rayset.r = &r;
	rayset.thresh = vec3f(1.0, 1.0, 1.0);
	rayset.depth = 0;
	Material air;
	rayset.material_stack.push_front(&air);

	return traceRay(rayset).clamp();
}

// Do recursive ray tracing!  You'll want to insert a lot of code here
// (or places called from here) to handle reflection, refraction, etc etc.
vec3f RayTracer::traceRay(const TraceSet& rayset)
{
	if (rayset.thresh[0] <= traceUI->GetIntensityThreshold()
		&& rayset.thresh[1] <= traceUI->GetIntensityThreshold()
		&& rayset.thresh[2] <= traceUI->GetIntensityThreshold())
	{
		return vec3f();
	}

	isect i;
	if (rayset.scene->intersect(*rayset.r, i))
	{
		if (IsLeavingObject(rayset, i)) i.N = -i.N;
		const Material &m = i.getMaterial();
		const vec3f &shade = m.shade(rayset.scene, *rayset.r, i);
		const vec3f intensity = prod(shade, rayset.thresh);

		ReflectionSet reflect_rayset;
		reflect_rayset.i = &i;
		vec3f reflection = traceUI->IsEnableReflection()
			? traceReflection(rayset, reflect_rayset) : vec3f();

		RefractionParam refract_param;
		refract_param.i = &i;
		vec3f refraction = traceUI->IsEnableRefraction()
			? traceRefraction(rayset, refract_param) : vec3f();

		if (traceUI->IsEnableFresnel()
			&& (rayset.material_stack.front()->index != 1
				|| i.getMaterial().index != 1))
		{
			const double fresnel_coeff = GetFresnelCoeff(rayset, i);
			const double fresnel_ratio = traceUI->GetFresnelRatio();

			reflection = fresnel_ratio * fresnel_coeff * reflection
				+ (1 - fresnel_ratio) * reflection;
			refraction = fresnel_ratio * (1 - fresnel_coeff) * refraction
				+ (1 - fresnel_ratio) * refraction;
		}
		return intensity + reflection + refraction;
	}
	else
	{
		return vec3f(0.0, 0.0, 0.0);
	}
}

vec3f RayTracer::traceReflection(const TraceSet& rayset,
	const ReflectionSet &reflect_rayset)
{
	const Material &m = reflect_rayset.i->getMaterial();
	if (m.kr.iszero() || rayset.depth >= traceUI->getDepth())
	{
		return vec3f();
	}
	const vec3f &out_point = rayset.r->at(reflect_rayset.i->t) + reflect_rayset.i->N
		* RAY_EPSILON;
	const double dot_rn = reflect_rayset.i->N.dot(-rayset.r->getDirection());
	const vec3f &center_dir = (2.0 * dot_rn * reflect_rayset.i->N-rayset.r->getDirection()).normalize();

	const int sample = traceUI->GetGlossyReflectionSample();
	if (sample == 0)
	{
		ray reflection_r(out_point, center_dir);

		TraceSet next_rayset;
		next_rayset.scene = scene;
		next_rayset.r = &reflection_r;
		next_rayset.thresh = prod(rayset.thresh, m.kr);
		next_rayset.depth = rayset.depth + 1;
		next_rayset.material_stack = rayset.material_stack;
		return traceRay(next_rayset);
	}
	else
	{
		vec3f intensity;
		VecCone vcg(center_dir, 0.1);
		for (int i = 0; i < sample; ++i)
		{
			const vec3f &dir = vcg.Generate();
			ray reflection_r(out_point, dir);

			TraceSet next_rayset;
			next_rayset.scene = scene;
			next_rayset.r = &reflection_r;
			next_rayset.thresh = prod(rayset.thresh, m.kr);
			next_rayset.depth = rayset.depth + 1;
			next_rayset.material_stack = rayset.material_stack;
			intensity += traceRay(next_rayset);
		}
		return intensity / sample;
	}
}

vec3f RayTracer::traceRefraction(const TraceSet &rayset,
	const RefractionParam &refelect_rayset)
{
	const Material &m = refelect_rayset.i->getMaterial();
	if (!m.kt.iszero() && rayset.depth < traceUI->getDepth())
	{
		deque<const Material*> mat_stack = rayset.material_stack;
		const Material *m2 = mat_stack.front();
		double ni, nt;
		vec3f push_point;
		if (IsLeavingObject(rayset, *refelect_rayset.i))
		{
			const Material *outside = mat_stack[1];
			ni = m.index;
			nt = outside->index;
			mat_stack.pop_front();
		}
		else
		{
			ni = m2->index;
			nt = m.index;
			mat_stack.push_front(&m);
		}
		const double nr = ni / nt;
		const double dot_rn = refelect_rayset.i->N.dot(-rayset.r->getDirection());
		push_point = rayset.r->at(refelect_rayset.i->t) - refelect_rayset.i->N * RAY_EPSILON;

		const double root = 1 - nr * nr * (1 - dot_rn * dot_rn);
		if (root < 0.0) return vec3f();
		else
		{
			const double coeff = nr * dot_rn - sqrt(root);
			const vec3f &refraction_dir = coeff * refelect_rayset.i->N - nr
				* -rayset.r->getDirection();
			ray refraction_r(push_point, refraction_dir);

			TraceSet next_rayset;
			next_rayset.scene = scene;
			next_rayset.r = &refraction_r;
			next_rayset.thresh = prod(rayset.thresh, m.kt);
			next_rayset.depth = rayset.depth + 1;
			next_rayset.material_stack = mat_stack;
			return traceRay(next_rayset);
		}
	}
	else
	{
		return vec3f();
	}
}

bool RayTracer::IsLeavingObject(const TraceSet& rayset, const isect &i) const
{
	return (&i.getMaterial() == rayset.material_stack.front());
}

double RayTracer::GetFresnelCoeff(const TraceSet& rayset, const isect &i) const
{
	// Schlick's approximation
	double ni, nt;
	if (IsLeavingObject(rayset, i))
	{
		ni = i.getMaterial().index;
		nt = rayset.material_stack.front()->index;
	}
	else
	{
		ni = rayset.material_stack.front()->index;
		nt = i.getMaterial().index;
	}
	double r0 = (ni - nt) / (ni + nt);
	r0 *= r0;
	const double dot_rn = i.N.dot(-rayset.r->getDirection());

	if (ni <= nt)
	{
		return r0 + (1 - r0) * pow(1 - dot_rn, 5);
	}
	else
	{
		const double nr = ni / nt;
		const double root = 1 - nr * nr * (1 - dot_rn * dot_rn);
		if (root < 0.0) return 1.0;
		else
		{
			const double cos_theta_t = sqrt(1 - (ni / nt));
			return r0 + (1 - r0) * pow(1 - cos_theta_t, 5);
		}
	}
}

RayTracer::RayTracer()
{
	buffer = NULL;
	buffer_width = buffer_height = 256;
	scene = NULL;

	m_bSceneLoaded = false;
}


RayTracer::~RayTracer()
{
	delete[] buffer;
	delete scene;
}

void RayTracer::getBuffer(unsigned char *&buf, int &w, int &h)
{
	buf = buffer;
	w = buffer_width;
	h = buffer_height;
}

double RayTracer::aspectRatio()
{
	return scene ? scene->getCamera()->getAspectRatio() : 1;
}

bool RayTracer::sceneLoaded()
{
	return m_bSceneLoaded;
}

bool RayTracer::loadScene(const char* fn)
{
	try
	{
		scene = readScene(fn);
	}
	catch (const ParseError &pe)
	{
		fl_alert("ParseError: %s\n", pe.getMsg().c_str());
		return false;
	}

	if (!scene)
		return false;

	buffer_width = 256;
	buffer_height = (int)(buffer_width / scene->getCamera()->getAspectRatio() + 0.5);

	bufferSize = buffer_width * buffer_height * 3;
	buffer = new unsigned char[bufferSize];

	scene->initScene();

	m_bSceneLoaded = true;

	return true;
}

void RayTracer::traceSetup(int w, int h)
{
	if (buffer_width != w || buffer_height != h)
	{
		buffer_width = w;
		buffer_height = h;

		bufferSize = buffer_width * buffer_height * 3;
		delete[] buffer;
		buffer = new unsigned char[bufferSize];
	}
	memset(buffer, 0, w*h * 3);
}

void RayTracer::traceLines(int start, int stop)
{
	vec3f col;
	if (!scene)
		return;

	if (stop > buffer_height)
		stop = buffer_height;

	for (int j = start; j < stop; ++j)
		for (int i = 0; i < buffer_width; ++i)
			tracePixel(i, j);
}

void RayTracer::tracePixel(int i, int j)
{
	vec3f col;

	if (!scene)
		return;

	double x = double(i) / double(buffer_width);
	double y = double(j) / double(buffer_height);

	if (traceUI->GetSuperSampling() > 0)
	{
		const int sample = traceUI->GetSuperSampling();
		const double pixel_w = 1.0 / buffer_width;
		const double pixel_h = 1.0 / buffer_height;
		const double sub_pixel_w = pixel_w / sample;
		const double sub_pixel_h = pixel_h / sample;
		for (int i = 0; i < sample; ++i)
		{
			const double base_y = y + ((double)i / sample - 0.5) * pixel_h;
			for (int j = 0; j < sample; ++j)
			{
				const double base_x = x + ((double)j / sample - 0.5) * pixel_w;

				const double jitter_y = (rand() / (double)RAND_MAX - 0.5)
					* sub_pixel_h + base_y;
				const double jitter_x = (rand() / (double)RAND_MAX - 0.5)
					* sub_pixel_w + base_x;
				col += trace(scene, jitter_x, jitter_y);
			}
		}
		col /= sample * sample;
	}
	else
	{
		col = trace(scene, x, y);
	}

	unsigned char *pixel = buffer + (i + j * buffer_width) * 3;

	pixel[0] = (int)(255.0 * col[0]);
	pixel[1] = (int)(255.0 * col[1]);
	pixel[2] = (int)(255.0 * col[2]);
}