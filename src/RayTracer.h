#ifndef __RAYTRACER_H__
#define __RAYTRACER_H__

// The main ray tracer.

#include <deque>
#include "scene/scene.h"
#include "scene/ray.h"

class Material;

class RayTracer
{
public:
	RayTracer();
	~RayTracer();

	vec3f trace(Scene *scene, double x, double y);

	void getBuffer(unsigned char *&buf, int &w, int &h);
	double aspectRatio();
	void traceSetup(int w, int h);
	void traceLines(int start = 0, int stop = 10000000);
	void tracePixel(int i, int j);

	bool loadScene(const char* fn);

	bool sceneLoaded();

private:
	struct TraceSet
	{
		Scene *scene;
		const ray *r;
		vec3f thresh;
		int depth;
		std::deque<const Material*> material_stack;
	};

	struct ReflectionSet
	{
		const isect *i;
	};

	struct RefractionParam
	{
		const isect *i;
	};

	vec3f traceRay(const TraceSet& param);
	vec3f traceReflection(const TraceSet& param, const ReflectionSet &rparam);
	vec3f traceRefraction(const TraceSet& param, const RefractionParam &rparam);

	bool IsLeavingObject(const TraceSet& param, const isect &i) const;

	double GetFresnelCoeff(const TraceSet& param, const isect &i) const;

	unsigned char *buffer;
	int buffer_width, buffer_height;
	int bufferSize;
	Scene *scene;

	bool m_bSceneLoaded;
};

#endif // __RAYTRACER_H__
