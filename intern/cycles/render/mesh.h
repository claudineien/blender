/*
 * Copyright 2011-2013 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __MESH_H__
#define __MESH_H__

#include "attribute.h"
#include "shader.h"

#include "util_boundbox.h"
#include "util_list.h"
#include "util_map.h"
#include "util_param.h"
#include "util_transform.h"
#include "util_types.h"
#include "util_vector.h"

#include "../subd/subd_split.h"

CCL_NAMESPACE_BEGIN

class BVH;
class Device;
class DeviceScene;
class Mesh;
class Progress;
class Scene;
class SceneParams;
class AttributeRequest;
class MeshOsdData;

/* Mesh */

class Mesh {
public:
	/* Mesh Triangle */
	struct Triangle {
		int v[3];

		void bounds_grow(const float3 *verts, BoundBox& bounds) const;
	};

	/* Mesh Curve */
	struct Curve {
		int first_key;
		int num_keys;
		uint shader;

		int num_segments() { return num_keys - 1; }

		void bounds_grow(const int k, const float4 *curve_keys, BoundBox& bounds) const;
	};

	/* Mesh Patch */
	struct Patch {
		int v[4]; /* v[3] is -1 if triangle patch */
		uint shader;
		bool smooth;

		bool is_quad() const { return v[3] != -1; }
	};

	struct SubPatch {
		int patch;
		int edge_factors[4];
		float2 uv[4];
		BoundBox bounds;

		SubPatch() : bounds(BoundBox::empty) {}

		bool is_quad() const { return edge_factors[3] != -1; }

		void bounds_grow(BoundBox& bounds) const;

		bool operator == (const SubPatch& other) const
		{
			if(patch != other.patch)
				return false;

			for(int i = 0; i < 4; i++) {
				if((edge_factors[i] != other.edge_factors[i]) || (uv[i] != other.uv[i]))
					return false;
			}

			return true;
		}
		bool operator != (const SubPatch& other) const { return !(*this == other); }
	};

	/* Displacement */
	enum DisplacementMethod {
		DISPLACE_BUMP = 0,
		DISPLACE_TRUE = 1,
		DISPLACE_BOTH = 2,

		DISPLACE_NUM_METHODS,
	};

	enum SubdivisionType {
		SUBDIVISION_NONE,
		SUBDIVISION_LINEAR,
		SUBDIVISION_CATMALL_CLARK,
	};

	SubdivisionType subdivision_type;

	ustring name;

	/* Mesh Data */
	enum GeometryFlags {
		GEOMETRY_NONE      = 0,
		GEOMETRY_TRIANGLES = (1 << 0),
		GEOMETRY_CURVES    = (1 << 1),
	};
	int geometry_flags;  /* used to distinguish meshes with no verts
	                        and meshed for which geometry is not created */

	vector<float3> verts;
	vector<Triangle> triangles;
	vector<uint> shader;
	vector<bool> smooth;

	bool has_volume;  /* Set in the device_update_flags(). */
	bool has_surface_bssrdf;  /* Set in the device_update_flags(). */

	vector<float4> curve_keys; /* co + radius */
	vector<Curve> curves;

	vector<Patch> patches;
	vector<SubPatch> subpatches;
	MeshOsdData* osd_data;

	vector<uint> used_shaders;
	AttributeSet attributes;
	AttributeSet curve_attributes;

	BoundBox bounds;
	bool transform_applied;
	bool transform_negative_scaled;
	Transform transform_normal;
	DisplacementMethod displacement_method;

	uint motion_steps;
	bool use_motion_blur;

	float displacement_scale;

	/* Update Flags */
	bool need_update;
	bool need_update_rebuild;

	/* BVH */
	BVH *bvh;
	size_t tri_offset;
	size_t vert_offset;

	size_t curve_offset;
	size_t curvekey_offset;

	size_t patch_offset;

	/* Functions */
	Mesh();
	~Mesh();

	void reserve(int numverts, int numfaces, int numcurves, int numcurvekeys, int numpatches);
	void clear();
	void set_triangle(int i, int v0, int v1, int v2, int shader, bool smooth);
	void add_triangle(int v0, int v1, int v2, int shader, bool smooth);
	void add_curve_key(float3 loc, float radius);
	void add_curve(int first_key, int num_keys, int shader);
	void set_patch(int i, int v0, int v1, int v2, int v3, int shader, bool smooth);
	int split_vertex(int vertex);

	void compute_bounds();
	void add_face_normals();
	void add_vertex_normals();

	void pack_normals(Scene *scene, uint *shader, float4 *vnormal);
	void pack_verts(float4 *tri_verts, float4 *tri_vindex, size_t vert_offset);
	void pack_curves(Scene *scene, float4 *curve_key_co, float4 *curve_data, size_t curvekey_offset);
	void compute_bvh(SceneParams *params, Progress *progress, int n, int total);

	bool need_attribute(Scene *scene, AttributeStandard std);
	bool need_attribute(Scene *scene, ustring name);

	void tag_update(Scene *scene, bool rebuild);

	bool has_motion_blur() const;

	/* Check whether the mesh should have own BVH built separately. Briefly,
	 * own BVH is needed for mesh, if:
	 *
	 * - It is instanced multiple times, so each instance object should share the
	 *   same BVH tree.
	 * - Special ray intersection is needed, for example to limit subsurface rays
	 *   to only the mesh itself.
	 */
	bool need_build_bvh() const;

	/* Check if the mesh should be treated as instanced. */
	bool is_instanced() const;

	void update_osd();
	void free_osd_data();

	void split_patches(DiagSplit *split);
	void diced_subpatch_size(int subpatch_id, uint* num_verts, uint* num_tris);
	void dice_subpatch(TessellatedSubPatch* diced, int subpatch_id);
};

/* Mesh Manager */

class MeshManager {
public:
	BVH *bvh;

	bool need_update;
	bool need_flags_update;
	bool need_clear_geom_cache;

	MeshManager();
	~MeshManager();

	bool displace(Device *device, DeviceScene *dscene, Scene *scene, Mesh *mesh, Progress& progress);

	/* attributes */
	void update_osl_attributes(Device *device, Scene *scene, vector<AttributeRequestSet>& mesh_attributes);
	void update_svm_attributes(Device *device, DeviceScene *dscene, Scene *scene, vector<AttributeRequestSet>& mesh_attributes);

	void device_update(Device *device, DeviceScene *dscene, Scene *scene, Progress& progress);
	void device_update_object(Device *device, DeviceScene *dscene, Scene *scene, Progress& progress);
	void device_update_mesh(Device *device, DeviceScene *dscene, Scene *scene, Progress& progress);
	void device_update_attributes(Device *device, DeviceScene *dscene, Scene *scene, Progress& progress);
	void device_update_bvh(Device *device, DeviceScene *dscene, Scene *scene, Progress& progress);
	void device_update_flags(Device *device, DeviceScene *dscene, Scene *scene, Progress& progress);
	void device_update_displacement_images(Device *device, DeviceScene *dscene, Scene *scene, Progress& progress);
	void device_free(Device *device, DeviceScene *dscene);

	void tag_update(Scene *scene);
};

CCL_NAMESPACE_END

#endif /* __MESH_H__ */

