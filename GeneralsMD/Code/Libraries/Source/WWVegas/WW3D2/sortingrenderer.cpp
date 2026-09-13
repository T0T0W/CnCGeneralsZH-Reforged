/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/sortingrenderer.cpp                    $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Changes to max texture stage caps																*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "sortingrenderer.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8wrapper.h"
#include "vertmaterial.h"
#include "texture.h"
#include <d3d9.h>
#include "d3dx9math.h"
#include "statistics.h"
#include <wwprofile.h>
#include <algorithm>

#ifdef _INTERNAL
// for occasional debugging...
// #pragma optimize("", off)
// #pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

bool SortingRendererClass::_EnableTriangleDraw=true;
static unsigned DEFAULT_SORTING_POLY_COUNT = 16384;	// (count * 3) must be less than 65536
static unsigned DEFAULT_SORTING_VERTEX_COUNT = 32768;	// count must be less than 65536

void SortingRendererClass::SetMinVertexBufferSize( unsigned val )
{
	DEFAULT_SORTING_VERTEX_COUNT = val;
	DEFAULT_SORTING_POLY_COUNT = val/2;	//typically have 2:1 vertex:triangle ratio.
}

struct ShortVectorIStruct
{
	unsigned short i;
	unsigned short j;
	unsigned short k;
};

struct TempIndexStruct
{
	ShortVectorIStruct tri;
	unsigned short idx;
	float z;
};

// ----------------------------------------------------------------------------
// The pool's entries in depth order, nearest last, as three passes of eleven bits over each depth's
// sort key.  The quicksort that was here was a fifth of a hundred-thousand-particle frame on its
// own, two hundred thousand triangles compared several million times.  This touches each entry four
// times.  It is stable, so entries at exactly the same depth keep the order they were pooled in; the
// quicksort left those in whatever order partitioning did.  Returns whichever of the two arrays
// holds the result.

static const unsigned DEPTH_RADIX_BITS=11;
static const unsigned DEPTH_RADIX_BUCKETS=1<<DEPTH_RADIX_BITS;
static const unsigned DEPTH_RADIX_PASSES=3;

static TempIndexStruct* Sort_By_Depth(TempIndexStruct* items,TempIndexStruct* scratch,unsigned count)
{
	static unsigned histogram[DEPTH_RADIX_PASSES][DEPTH_RADIX_BUCKETS];
	memset(histogram,0,sizeof(histogram));
	for (unsigned i=0;i<count;++i) {
		const unsigned key=SortingRendererClass::_Depth_Sort_Key(items[i].z);
		for (unsigned pass=0;pass<DEPTH_RADIX_PASSES;++pass) {
			++histogram[pass][(key>>(pass*DEPTH_RADIX_BITS))&(DEPTH_RADIX_BUCKETS-1)];
		}
	}

	TempIndexStruct* from=items;
	TempIndexStruct* to=scratch;
	for (unsigned pass=0;pass<DEPTH_RADIX_PASSES;++pass) {
		unsigned* buckets=histogram[pass];
		unsigned offset=0;
		for (unsigned b=0;b<DEPTH_RADIX_BUCKETS;++b) {
			const unsigned in_bucket=buckets[b];
			buckets[b]=offset;
			offset+=in_bucket;
		}

		const unsigned shift=pass*DEPTH_RADIX_BITS;
		for (unsigned i=0;i<count;++i) {
			const unsigned key=SortingRendererClass::_Depth_Sort_Key(from[i].z);
			to[buckets[(key>>shift)&(DEPTH_RADIX_BUCKETS-1)]++]=from[i];
		}

		TempIndexStruct* swap=from;
		from=to;
		to=swap;
	}
	return from;
}

// ----------------------------------------------------------------------------

class SortingNodeStruct : public DLNodeClass<SortingNodeStruct>
{
	W3DMPO_GLUE(SortingNodeStruct)

public:
	RenderStateStruct sorting_state;

	SphereClass bounding_sphere;

	Vector3 transformed_center;
	unsigned short start_index;			// First index used in the ib
	unsigned short polygon_count;			// Polygon count to process (3 indices = one polygon)
	unsigned short min_vertex_index;		// First index used in the vb
	unsigned short vertex_count;			// Number of vertices used in vb
	bool quads;									// four vertices a quad, in order from min_vertex_index; the ib is not read
};

static DLListClass<SortingNodeStruct> sorted_list;
// Nodes that arrive with no bounding sphere have nothing to sort by; they wait here and go into
// the sorted list in one piece at flush time, instead of each walking it looking for a place.
static DLListClass<SortingNodeStruct> unsorted_list;
static DLListClass<SortingNodeStruct> clean_list;
static unsigned total_sorting_vertices;

static SortingNodeStruct* Get_Sorting_Struct()
{

	SortingNodeStruct* state=clean_list.Head();
	if (state) {
		state->Remove();
		return state;
	}
	state=W3DNEW SortingNodeStruct();
	return state;
}

// ----------------------------------------------------------------------------
//
// Temporary arrays for the sorting system
//
// ----------------------------------------------------------------------------

static TempIndexStruct* temp_index_array;
static TempIndexStruct* temp_sort_scratch_array;	// the radix sort's second buffer, always the same size
static unsigned temp_index_array_count;

static TempIndexStruct* Get_Temp_Index_Array(unsigned count)
{
	if (count < DEFAULT_SORTING_POLY_COUNT)
		count = DEFAULT_SORTING_POLY_COUNT;
	if (count>temp_index_array_count) {
		delete[] temp_index_array;
		delete[] temp_sort_scratch_array;
		temp_index_array=W3DNEWARRAY TempIndexStruct[count];
		temp_sort_scratch_array=W3DNEWARRAY TempIndexStruct[count];
		temp_index_array_count=count;
	}
	return temp_index_array;
}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	const SphereClass& bounding_sphere,
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (!WW3D::Is_Sorting_Enabled()) {
		DX8Wrapper::Draw_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	SNAPSHOT_SAY(("SortingRenderer::Insert(start_i: %d, polygons : %d, min_vi: %d, vertex_count: %d)\n",
		start_index,polygon_count,min_vertex_index,vertex_count));


	DX8_RECORD_SORTING_RENDER(polygon_count,vertex_count);

	SortingNodeStruct* state=Get_Sorting_Struct();

	DX8Wrapper::Get_Render_State(state->sorting_state);

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));


	state->bounding_sphere=bounding_sphere;
	state->start_index=start_index;
	state->polygon_count=polygon_count;
	state->min_vertex_index=min_vertex_index;
	state->vertex_count=vertex_count;
	state->quads=false;

	SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
	WWASSERT(vertex_buffer);
	WWASSERT(state->vertex_count<=vertex_buffer->Get_Vertex_Count());

	if (state->bounding_sphere.Radius <= 0.0f) {
		// Nothing to sort by, so do not pay the walk down the list looking for a place.
		state->transformed_center=Vector3(0.0f,0.0f,0.0f);
		unsorted_list.Add_Tail(state);
	}
	else {
	D3DXMATRIX mtx=(D3DXMATRIX&)state->sorting_state.world*(D3DXMATRIX&)state->sorting_state.view;
	D3DXVECTOR3 vec=(D3DXVECTOR3&)state->bounding_sphere.Center;
	D3DXVECTOR4 transformed_vec;
	D3DXVec3Transform(
		&transformed_vec,
		&vec,
		&mtx);
	state->transformed_center=Vector3(transformed_vec[0],transformed_vec[1],transformed_vec[2]);


	/// @todo lorenzen sez use a bucket sort here... and stop copying so much data so many times

	SortingNodeStruct* node=sorted_list.Head();
	while (node) {
		if (state->transformed_center.Z>node->transformed_center.Z) {
			if (sorted_list.Head()==sorted_list.Tail())
				sorted_list.Add_Head(state);
			else
				state->Insert_Before(node);
			break;
		}
		node=node->Succ();
	}
	if (!node) sorted_list.Add_Tail(state);
	}

#ifdef WWDEBUG
	unsigned short* indices=NULL;
	SortingIndexBufferClass* index_buffer=static_cast<SortingIndexBufferClass*>(state->sorting_state.index_buffer);
	WWASSERT(index_buffer);
	indices=index_buffer->index_buffer;
	WWASSERT(indices);
	indices+=state->start_index;
	indices+=state->sorting_state.iba_offset;

	for (int i=0;i<state->polygon_count;++i) {
		unsigned short idx1=indices[i*3]-state->min_vertex_index;
		unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
		unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
		WWASSERT(idx1<state->vertex_count);
		WWASSERT(idx2<state->vertex_count);
		WWASSERT(idx3<state->vertex_count);
	}
#endif // WWDEBUG
}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system, with no bounding information.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	SphereClass sphere(Vector3(0.0f,0.0f,0.0f),0.0f);
	Insert_Triangles(sphere,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
// Insert quads laid out four vertices each, in order, from min_vertex_index in the vertex buffer
// that is set.  The pool sorts and writes a quad as one piece: a billboard's four corners share one
// depth, so its two triangles always had the same key, and a quad is four vertices and six indices
// where two loose triangles were six and six.  No bounding information, like the call above.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Quads(
	unsigned short quad_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	const unsigned short polygon_count=quad_count*2;
	if (!WW3D::Is_Sorting_Enabled()) {
		DX8Wrapper::Draw_Triangles(0,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	DX8_RECORD_SORTING_RENDER(polygon_count,vertex_count);

	SortingNodeStruct* state=Get_Sorting_Struct();
	DX8Wrapper::Get_Render_State(state->sorting_state);

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));
	WWASSERT(vertex_count==quad_count*4);

	state->bounding_sphere=SphereClass(Vector3(0.0f,0.0f,0.0f),0.0f);
	state->start_index=0;
	state->polygon_count=polygon_count;
	state->min_vertex_index=min_vertex_index;
	state->vertex_count=vertex_count;
	state->quads=true;
	state->transformed_center=Vector3(0.0f,0.0f,0.0f);
	unsorted_list.Add_Tail(state);
}

// ----------------------------------------------------------------------------
//
// Flush all sorting polygons.
//
// ----------------------------------------------------------------------------

void Release_Refs(SortingNodeStruct* state)
{
	int i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(state->sorting_state.vertex_buffers[i]);
	}
	REF_PTR_RELEASE(state->sorting_state.index_buffer);
	REF_PTR_RELEASE(state->sorting_state.material);
	for (i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i) 
	{
		REF_PTR_RELEASE(state->sorting_state.Textures[i]);
	}
}

static unsigned overlapping_node_count;
static unsigned overlapping_entry_count;	// sort entries: a triangle each, or a quad each for a quad node
static const unsigned MAX_OVERLAPPING_NODES=4096;
// One batch of the flush: a 16 bit index names 65535 vertices, and the index buffer holds 65535.
static const unsigned MAX_BATCH_VERTICES=65535;
static const unsigned MAX_BATCH_INDICES=65535;
// A sort entry whose third index is this is a quad; its first index is the quad's first vertex.
static const unsigned short QUAD_ENTRY=0xFFFF;
static SortingNodeStruct* overlapping_nodes[MAX_OVERLAPPING_NODES];
// Where each pooled node's first vertex sits, worked out once in the sort pass for the copy pass.
static VertexFormatXYZNDUV2* overlapping_node_vertices[MAX_OVERLAPPING_NODES];
// Where each pooled node's sort entries begin, worked out in order before the nodes fill them in parallel.
static unsigned overlapping_node_entry_offsets[MAX_OVERLAPPING_NODES];
static unsigned refused_polygon_count;

unsigned SortingRendererClass::Get_Refused_Polygon_Count()
{
	return refused_polygon_count;
}

static SortingRendererClass::ParallelForFunc parallel_for_hook=NULL;

void SortingRendererClass::Set_Parallel_For(ParallelForFunc parallel_for)
{
	parallel_for_hook=parallel_for;
}

static void Run_Parallel(int count,int granularity,void (*work)(int,void*),void* context)
{
	if (count<=0) return;
	if (parallel_for_hook) {
		parallel_for_hook(count,granularity,work,context);
		return;
	}
	for (int i=0;i<count;++i) {
		work(i,context);
	}
}

// nodes a thread claims at once in the depth pass; a particle node is a few hundred quads
static const int NODES_PER_CLAIM=4;
// entries one job copies into a batch at once
static const unsigned ENTRIES_PER_COPY_CHUNK=2048;

// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_To_Sorting_Pool(SortingNodeStruct* state)
{
	if (overlapping_node_count>=MAX_OVERLAPPING_NODES) {
		refused_polygon_count+=state->polygon_count;
		Release_Refs(state);
		WWASSERT(0);
		return;
	}

	overlapping_nodes[overlapping_node_count]=state;
	overlapping_entry_count+=state->quads ? state->polygon_count/2 : state->polygon_count;
	overlapping_node_count++;
}

// ----------------------------------------------------------------------------
//static unsigned prevLight = 0xffffffff;

static void Apply_Render_State(RenderStateStruct& render_state)
{



	DX8Wrapper::Set_Shader(render_state.shader);

	DX8Wrapper::Set_Material(render_state.material);

	for (int i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i) 
	{
		DX8Wrapper::Set_Texture(i,render_state.Textures[i]);
	}

	DX8Wrapper::_Set_DX8_Transform(D3DTS_WORLD,render_state.world);
	DX8Wrapper::_Set_DX8_Transform(D3DTS_VIEW,render_state.view);



  if (!render_state.material->Get_Lighting())
    return;
  //prevLight = render_state.lightsHash;

	if (render_state.LightEnable[0]) 
  {
    
    DX8Wrapper::Set_DX8_Light(0,&render_state.Lights[0]);
		if (render_state.LightEnable[1]) 
    {
			DX8Wrapper::Set_DX8_Light(1,&render_state.Lights[1]);
			if (render_state.LightEnable[2]) 
      {
				DX8Wrapper::Set_DX8_Light(2,&render_state.Lights[2]);
				if (render_state.LightEnable[3]) 
					DX8Wrapper::Set_DX8_Light(3,&render_state.Lights[3]);
				else 
					DX8Wrapper::Set_DX8_Light(3,NULL);
			}
			else 
				DX8Wrapper::Set_DX8_Light(2,NULL);
		}
		else 
			DX8Wrapper::Set_DX8_Light(1,NULL);
	}
	else 
		DX8Wrapper::Set_DX8_Light(0,NULL);


}

// ----------------------------------------------------------------------------
// Would Apply_Render_State leave the device exactly as it is?  Byte compares on the lights and the
// matrices can only say no to two states that are really the same, which costs a draw and nothing
// else.

static bool Same_Draw_State(const RenderStateStruct& a, const RenderStateStruct& b)
{
	if (a.shader.Get_Bits()!=b.shader.Get_Bits() || a.material!=b.material) return false;
	for (int i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i) {
		if (a.Textures[i]!=b.Textures[i]) return false;
	}
	if (memcmp(&a.world,&b.world,sizeof(a.world))!=0 || memcmp(&a.view,&b.view,sizeof(a.view))!=0) return false;
	if (a.material && a.material->Get_Lighting()) {
		if (memcmp(a.LightEnable,b.LightEnable,sizeof(a.LightEnable))!=0) return false;
		if (memcmp(a.Lights,b.Lights,sizeof(a.Lights))!=0) return false;
	}
	return true;
}

// ----------------------------------------------------------------------------

static inline bool Entry_Is_Quad(const TempIndexStruct& entry)
{
	return entry.tri.k==QUAD_ENTRY;
}

static inline unsigned Entry_Vertex_Count(const TempIndexStruct& entry)
{
	return Entry_Is_Quad(entry) ? 4 : 3;
}

static inline unsigned Entry_Index_Count(const TempIndexStruct& entry)
{
	return Entry_Is_Quad(entry) ? 6 : 3;
}

// Where each entry of a batch writes its vertices and indices.  Worked out in order on the render
// thread so the copy can be split across threads; the array only grows, and only there.
struct BatchEntryOffset
{
	unsigned vertex;
	unsigned index;
};

static BatchEntryOffset* batch_offsets;
static unsigned batch_offsets_count;

static const BatchEntryOffset* Compute_Batch_Offsets(const TempIndexStruct* entries,unsigned entry_count)
{
	if (entry_count>batch_offsets_count) {
		delete[] batch_offsets;
		batch_offsets=W3DNEWARRAY BatchEntryOffset[entry_count];
		batch_offsets_count=entry_count;
	}
	unsigned vertex=0;
	unsigned index=0;
	for (unsigned e=0;e<entry_count;++e) {
		batch_offsets[e].vertex=vertex;
		batch_offsets[e].index=index;
		vertex+=Entry_Vertex_Count(entries[e]);
		index+=Entry_Index_Count(entries[e]);
	}
	return batch_offsets;
}

struct BatchCopyJob
{
	const TempIndexStruct* entries;
	const BatchEntryOffset* offsets;
	unsigned entry_count;
	VertexFormatXYZNDUV2* vertices;
	unsigned short* indices;
};

static void Copy_Batch_Vertices(int chunk,void* context)
{
	const BatchCopyJob* job=(const BatchCopyJob*)context;
	const unsigned first=(unsigned)chunk*ENTRIES_PER_COPY_CHUNK;
	const unsigned last=MIN(first+ENTRIES_PER_COPY_CHUNK,job->entry_count);
	for (unsigned e=first;e<last;++e) {
		const TempIndexStruct& entry=job->entries[e];
		const VertexFormatXYZNDUV2* src_verts=overlapping_node_vertices[entry.idx];
		VertexFormatXYZNDUV2* dest_verts=job->vertices+job->offsets[e].vertex;
		if (Entry_Is_Quad(entry)) {
			memcpy(dest_verts,src_verts+entry.tri.i,sizeof(VertexFormatXYZNDUV2)*4);
		}
		else {
			dest_verts[0]=src_verts[entry.tri.i];
			dest_verts[1]=src_verts[entry.tri.j];
			dest_verts[2]=src_verts[entry.tri.k];
		}
	}
}

static void Write_Batch_Indices(int chunk,void* context)
{
	const BatchCopyJob* job=(const BatchCopyJob*)context;
	const unsigned first=(unsigned)chunk*ENTRIES_PER_COPY_CHUNK;
	const unsigned last=MIN(first+ENTRIES_PER_COPY_CHUNK,job->entry_count);
	for (unsigned e=first;e<last;++e) {
		const unsigned short vertex=(unsigned short)job->offsets[e].vertex;
		unsigned short* index_array=job->indices+job->offsets[e].index;
		if (Entry_Is_Quad(job->entries[e])) {
			index_array[0]=vertex;
			index_array[1]=vertex+1;
			index_array[2]=vertex+2;
			index_array[3]=vertex+2;
			index_array[4]=vertex+3;
			index_array[5]=vertex;
		}
		else {
			index_array[0]=vertex;
			index_array[1]=vertex+1;
			index_array[2]=vertex+2;
		}
	}
}

// Draw one batch of the sorted entries, in that order.
//
// Each entry's vertices are copied out of its node on their own - three for a triangle, four for a
// quad - so a run of entries covers one contiguous stretch of the batch's vertices and indices.  A
// quad's indices are the pattern PointGroupClass's own quad index buffers use.
//
// A run ends where the sorted entries move to another node, and it used to be drawn there even
// when the next node draws with exactly the same state - thirty inferno cannons each with its own
// fire system on the same texture, interleaved in depth, were a draw every few triangles.  A run
// carries on across nodes whose state matches.  The order of the entries does not change, so
// neither does the picture.
static void Flush_Sorting_Batch(const TempIndexStruct* entries,unsigned entry_count,unsigned vertex_count,unsigned index_count)
{
	BatchCopyJob job;
	job.entries=entries;
	job.offsets=Compute_Batch_Offsets(entries,entry_count);
	job.entry_count=entry_count;
	job.vertices=NULL;
	job.indices=NULL;
	const int chunks=(int)((entry_count+ENTRIES_PER_COPY_CHUNK-1)/ENTRIES_PER_COPY_CHUNK);

	DynamicVBAccessClass dyn_vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,(unsigned short)vertex_count);
	{
		DynamicVBAccessClass::WriteLockClass lock(&dyn_vb_access);

		// If you have a crash in here and "dest_verts" points to illegal memory area,
		// it is because D3D is in illegal state, and the only known cure is rebooting.
		// This illegal state is usually caused by Quake3-engine powered games such as MOHAA.
		job.vertices=(VertexFormatXYZNDUV2 *)lock.Get_Formatted_Vertex_Array();
		Run_Parallel(chunks,1,Copy_Batch_Vertices,&job);
	}

	DynamicIBAccessClass dyn_ib_access(BUFFER_TYPE_DYNAMIC_DX8,(unsigned short)index_count);
	{
		DynamicIBAccessClass::WriteLockClass lock(&dyn_ib_access);
		job.indices=lock.Get_Index_Array();

		try {
		Run_Parallel(chunks,1,Write_Batch_Indices,&job);
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}
	}

	DX8Wrapper::Set_Index_Buffer(dyn_ib_access,0); // Override with this buffer (do something to prevent need for this!)
	DX8Wrapper::Set_Vertex_Buffer(dyn_vb_access); // Override with this buffer (do something to prevent need for this!)

	DX8Wrapper::Apply_Render_State_Changes();

	unsigned run_first_index=0;
	unsigned run_first_vertex=0;
	unsigned run_triangles=0;
	unsigned index_cursor=0;
	unsigned vertex_cursor=0;
	unsigned node_id=entries[0].idx;
	for (unsigned e=0;e<entry_count;++e) {
		const unsigned entry_node=entries[e].idx;
		if (entry_node!=node_id) {
			SortingNodeStruct* state=overlapping_nodes[node_id];
			SortingNodeStruct* next=overlapping_nodes[entry_node];
			if (!Same_Draw_State(state->sorting_state,next->sorting_state)) {
				Apply_Render_State(state->sorting_state);
				DX8Wrapper::Draw_Triangles(run_first_index,run_triangles,run_first_vertex,vertex_cursor-run_first_vertex);
				run_first_index=index_cursor;
				run_first_vertex=vertex_cursor;
				run_triangles=0;
			}
			node_id=entry_node;
		}
		run_triangles+=Entry_Is_Quad(entries[e]) ? 2 : 1;
		index_cursor+=Entry_Index_Count(entries[e]);
		vertex_cursor+=Entry_Vertex_Count(entries[e]);
	}

	Apply_Render_State(overlapping_nodes[node_id]->sorting_state);
	DX8Wrapper::Draw_Triangles(run_first_index,run_triangles,run_first_vertex,vertex_cursor-run_first_vertex);
}

// ----------------------------------------------------------------------------
//
// The pool used to go out as one dynamic vertex buffer and one 16 bit index buffer, so a node that
// would have taken either past 65535 was refused and not drawn: a screen with a hundred thousand
// particles on it drew about sixteen thousand of them.  EA had left the batching as a @todo right
// here.  Every triangle is still sorted against every other, back to front, and only then written out
// in batches, in that order.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Flush_Sorting_Pool()
{
	if (!overlapping_node_count) return;

	SNAPSHOT_SAY(("SortingSystem - Flush \n"));

	TempIndexStruct* tis=Get_Temp_Index_Array(overlapping_entry_count);

	// Every entry's depth, with its indices kept relative to its own node's first vertex.  Each node
	// writes only its own entries, so once the offsets are known the nodes fill them in parallel.
	struct NodeDepthJob {
		static void Run(int node_id,void* context) {
		TempIndexStruct* tis=(TempIndexStruct*)context;
		SortingNodeStruct* state=overlapping_nodes[node_id];
		const VertexFormatXYZNDUV2* src_verts=overlapping_node_vertices[node_id];
		const unsigned polygon_array_offset=overlapping_node_entry_offsets[node_id];

		D3DXMATRIX d3d_mtx=(D3DXMATRIX&)state->sorting_state.world*(D3DXMATRIX&)state->sorting_state.view;
		const Matrix4x4& mtx=(const Matrix4x4&)d3d_mtx;
		const bool camera_space=(mtx[0][2] == 0.0f && mtx[1][2] == 0.0f && mtx[3][2] == 0.0f && mtx[2][2] == 1.0f);

		if (state->quads) {
			const int quad_count=state->polygon_count/2;
			for (int q=0;q<quad_count;++q) {
				const VertexFormatXYZNDUV2 *v = src_verts + q*4;
				TempIndexStruct *tis_ptr = tis + polygon_array_offset + q;
				tis_ptr->tri.i = (unsigned short)(q*4);
				tis_ptr->tri.j = 0;
				tis_ptr->tri.k = QUAD_ENTRY;
				tis_ptr->idx = node_id;
				if (camera_space) {
					tis_ptr->z = (v[0].z + v[1].z + v[2].z + v[3].z)*0.25f;
				} else {
					tis_ptr->z = (mtx[0][2]*(v[0].x + v[1].x + v[2].x + v[3].x) +
												mtx[1][2]*(v[0].y + v[1].y + v[2].y + v[3].y) +
												mtx[2][2]*(v[0].z + v[1].z + v[2].z + v[3].z))*0.25f + mtx[3][2];
				}
				DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Quad has invalid center"));
			}
			return;
		}

		unsigned short* indices=NULL;
		SortingIndexBufferClass* index_buffer=static_cast<SortingIndexBufferClass*>(state->sorting_state.index_buffer);
		WWASSERT(index_buffer);
		indices=index_buffer->index_buffer;
		WWASSERT(indices);
		indices+=state->start_index;
		indices+=state->sorting_state.iba_offset;

		if (camera_space) {
			// The common case for particle systems.
			for (int i=0;i<state->polygon_count;++i) {
				unsigned short idx1=indices[i*3]-state->min_vertex_index;
				unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
				unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
				WWASSERT(idx1<state->vertex_count);
				WWASSERT(idx2<state->vertex_count);
				WWASSERT(idx3<state->vertex_count);
				const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
				const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
				const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
				unsigned array_index=i+polygon_array_offset;
				WWASSERT(array_index<overlapping_entry_count);
				TempIndexStruct *tis_ptr = tis + array_index;
				tis_ptr->tri.i = idx1;
				tis_ptr->tri.j = idx2;
				tis_ptr->tri.k = idx3;
				tis_ptr->idx = node_id;
				tis_ptr->z = (v1->z + v2->z + v3->z)/3.0f;
				DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
			}
		} else {
			for (int i=0;i<state->polygon_count;++i) {
				unsigned short idx1=indices[i*3]-state->min_vertex_index;
				unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
				unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
				WWASSERT(idx1<state->vertex_count);
				WWASSERT(idx2<state->vertex_count);
				WWASSERT(idx3<state->vertex_count);
				const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
				const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
				const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
				unsigned array_index=i+polygon_array_offset;
				WWASSERT(array_index<overlapping_entry_count);
				TempIndexStruct *tis_ptr = tis + array_index;
				tis_ptr->tri.i = idx1;
				tis_ptr->tri.j = idx2;
				tis_ptr->tri.k = idx3;
				tis_ptr->idx = node_id;
				tis_ptr->z = (mtx[0][2]*(v1->x + v2->x + v3->x) +
											mtx[1][2]*(v1->y + v2->y + v3->y) +
											mtx[2][2]*(v1->z + v2->z + v3->z))/3.0f + mtx[3][2];
				DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
			}
		}
		}
	};

	unsigned entry_offset=0;
	for (unsigned node_id=0;node_id<overlapping_node_count;++node_id) {
		SortingNodeStruct* state=overlapping_nodes[node_id];
		SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
		WWASSERT(vertex_buffer);
		VertexFormatXYZNDUV2* src_verts=vertex_buffer->VertexBuffer;
		WWASSERT(src_verts);
		src_verts+=state->sorting_state.vba_offset;
		src_verts+=state->sorting_state.index_base_offset;
		src_verts+=state->min_vertex_index;
		overlapping_node_vertices[node_id]=src_verts;
		overlapping_node_entry_offsets[node_id]=entry_offset;
		entry_offset+=state->quads ? state->polygon_count/2 : state->polygon_count;
	}
	Run_Parallel((int)overlapping_node_count,NODES_PER_CLAIM,NodeDepthJob::Run,tis);

	const TempIndexStruct* sorted=Sort_By_Depth(tis,temp_sort_scratch_array,overlapping_entry_count);

	unsigned batch_start=0;
	while (batch_start<overlapping_entry_count) {
		unsigned batch_end=batch_start;
		unsigned batch_vertices=0;
		unsigned batch_indices=0;
		while (batch_end<overlapping_entry_count) {
			const unsigned vertices=Entry_Vertex_Count(sorted[batch_end]);
			const unsigned indices=Entry_Index_Count(sorted[batch_end]);
			if (batch_vertices+vertices>MAX_BATCH_VERTICES || batch_indices+indices>MAX_BATCH_INDICES) {
				break;
			}
			batch_vertices+=vertices;
			batch_indices+=indices;
			++batch_end;
		}
		Flush_Sorting_Batch(sorted+batch_start,batch_end-batch_start,batch_vertices,batch_indices);
		batch_start=batch_end;
	}

	// Release all references and return nodes back to the clean list for the frame...
	for (unsigned node_id=0;node_id<overlapping_node_count;++node_id) {
		SortingNodeStruct* state=overlapping_nodes[node_id];
		Release_Refs(state);
		clean_list.Add_Head(state);
	}
	overlapping_node_count=0;
	overlapping_entry_count=0;

	SNAPSHOT_SAY(("SortingSystem - Done flushing\n"));

}

// ----------------------------------------------------------------------------

void SortingRendererClass::Flush()
{
	WWPROFILE("SortingRenderer::Flush");
	Matrix4x4 old_view;
	Matrix4x4 old_world;
	DX8Wrapper::Get_Transform(D3DTS_VIEW,old_view);
	DX8Wrapper::Get_Transform(D3DTS_WORLD,old_world);

	// Put the unsorted nodes in where the sorted ones cross behind the camera, in the order they
	// arrived.  Insert_Before takes care of the head, so the boundary node can be the head.
	if (unsorted_list.Head()) {
		SortingNodeStruct* at=sorted_list.Head();
		while (at && at->transformed_center.Z > 0.0f)
			at=at->Succ();

		while (SortingNodeStruct* state=unsorted_list.Head()) {
			state->Remove();
			if (at)
				state->Insert_Before(at);
			else
				sorted_list.Add_Tail(state);
		}
	}

	while (SortingNodeStruct* state=sorted_list.Head()) {
		state->Remove();
		
		if ((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
			(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)) {
			Insert_To_Sorting_Pool(state);
		}
		else {
			DX8Wrapper::Set_Render_State(state->sorting_state);
			DX8Wrapper::Draw_Triangles(state->start_index,state->polygon_count,state->min_vertex_index,state->vertex_count);
			DX8Wrapper::Release_Render_State();
			Release_Refs(state);
			clean_list.Add_Head(state);
		}
	}

	bool old_enable=DX8Wrapper::_Is_Triangle_Draw_Enabled();
	DX8Wrapper::_Enable_Triangle_Draw(_EnableTriangleDraw);
	Flush_Sorting_Pool();
	DX8Wrapper::_Enable_Triangle_Draw(old_enable);

	DX8Wrapper::Set_Index_Buffer(0,0);
	DX8Wrapper::Set_Vertex_Buffer(0);
	total_sorting_vertices=0;

	DynamicIBAccessClass::_Reset(false);
	DynamicVBAccessClass::_Reset(false);


	DX8Wrapper::Set_Transform(D3DTS_VIEW,old_view);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,old_world);

}

// ----------------------------------------------------------------------------

void SortingRendererClass::Deinit()
{
	SortingNodeStruct *head = NULL;

	//
	//	Flush the sorted list
	//
	while ((head = sorted_list.Head ()) != NULL) {
		sorted_list.Remove_Head ();
		delete head;
	}

	//
	//	Flush the unsorted list
	//
	while ((head = unsorted_list.Head ()) != NULL) {
		unsorted_list.Remove_Head ();
		delete head;
	}

	//
	//	Flush the clean list
	//
	while ((head = clean_list.Head ()) != NULL) {
		clean_list.Remove_Head ();
		delete head;
	}

	delete[] temp_index_array;
	temp_index_array=NULL;
	delete[] temp_sort_scratch_array;
	temp_sort_scratch_array=NULL;
	temp_index_array_count=0;
	delete[] batch_offsets;
	batch_offsets=NULL;
	batch_offsets_count=0;
}


// ----------------------------------------------------------------------------
//
// Insert a VolumeParticle triangle into the sorting system.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_VolumeParticle(
	const SphereClass& bounding_sphere,
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count,
	unsigned short layerCount)
{
	if (!WW3D::Is_Sorting_Enabled()) {
		DX8Wrapper::Draw_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	//FOR VOLUME_PARTICLE LOGIC:
	// WE MUST MULTIPLY THE VERTCOUNT AND POLYCOUNT BY THE VOLUME_PARTICLE DEPTH
	DX8_RECORD_SORTING_RENDER( polygon_count * layerCount,vertex_count * layerCount);//THIS IS VOLUME_PARTICLE SPECIFIC

	SortingNodeStruct* state=Get_Sorting_Struct();
	DX8Wrapper::Get_Render_State(state->sorting_state);

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));

	state->bounding_sphere=bounding_sphere;
	state->start_index=start_index;
	state->min_vertex_index=min_vertex_index;
	state->polygon_count=polygon_count * layerCount;//THIS IS VOLUME_PARTICLE SPECIFIC
	state->vertex_count=vertex_count * layerCount;//THIS IS VOLUME_PARTICLE SPECIFIC
	state->quads=false;

	SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
	WWASSERT(vertex_buffer);
	WWASSERT(state->vertex_count<=vertex_buffer->Get_Vertex_Count());

	// Transform the center point to view space for sorting

	D3DXMATRIX mtx=(D3DXMATRIX&)state->sorting_state.world*(D3DXMATRIX&)state->sorting_state.view;
	D3DXVECTOR3 vec=(D3DXVECTOR3&)state->bounding_sphere.Center;
	D3DXVECTOR4 transformed_vec;
	D3DXVec3Transform(
		&transformed_vec,
		&vec,
		&mtx); 
	state->transformed_center=Vector3(transformed_vec[0],transformed_vec[1],transformed_vec[2]);


	// BUT WHAT IS THE DEAL WITH THE VERTCOUNT AND POLYCOUNT BEING N BUT TRANSFORMED CENTER COUNT == 1

	//THE TRANSFORMED CENTER[2] IS THE ZBUFFER DEPTH
	
	/// @todo lorenzen sez use a bucket sort here... and stop copying so much data so many times

	SortingNodeStruct* node=sorted_list.Head();
	while (node) {
		if (state->transformed_center.Z>node->transformed_center.Z) {
			if (sorted_list.Head()==sorted_list.Tail())
				sorted_list.Add_Head(state);
			else
				state->Insert_Before(node);
			break;
		}
		node=node->Succ();
	}
	if (!node) sorted_list.Add_Tail(state);
}
