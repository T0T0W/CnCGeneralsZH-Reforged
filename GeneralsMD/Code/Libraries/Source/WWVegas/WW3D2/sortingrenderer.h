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

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef SORTING_RENDERER_H
#define SORTING_RENDERER_H

#include "always.h"
#include <string.h>

class SortingNodeStruct;
class SphereClass;

class SortingRendererClass
{
	static bool _EnableTriangleDraw;

	static void Flush_Sorting_Pool();
	static void Insert_To_Sorting_Pool(SortingNodeStruct* state);

public:
	static void Insert_Triangles(
		const SphereClass& bounding_sphere,
		unsigned short start_index, 
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	static void Insert_Triangles(
		unsigned short start_index, 
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	/// Quads laid out four vertices each, in order, from min_vertex_index; the pool sorts one entry
	/// a quad and does not read the index buffer.
	static void Insert_Quads(
		unsigned short quad_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	static void Insert_VolumeParticle(
		const SphereClass& bounding_sphere,
		unsigned short start_index, 
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count,
		unsigned short layerCount);

	static void Flush();
	static void Deinit();
	
	static void SetMinVertexBufferSize( unsigned val );

	/// Polygons refused since the start because the pool's node list was full; they are not drawn.
	static unsigned Get_Refused_Polygon_Count();

	/// Where the flush can hand work to more threads: work(0..count-1, context), returning once all of
	/// it is done.  WW3D2 cannot see the game's job pool, so the device layer installs one; without it
	/// the work runs inline.  The work never allocates and never touches the device.
	typedef void (*ParallelForFunc)(int count, int granularity, void (*work)(int index, void *context), void *context);
	static void Set_Parallel_For(ParallelForFunc parallel_for);

	/// A depth as an unsigned key that orders the way the float does, negatives first and -0 just
	/// below +0; the flush's radix sort buckets on it.  The bits come out through memcpy so no
	/// aliasing rule is bent.
	static unsigned _Depth_Sort_Key(float depth)
	{
		unsigned bits;
		memcpy(&bits,&depth,sizeof(bits));
		const unsigned magnitude=bits&0x7FFFFFFFu;
		return (bits&0x80000000u) ? 0x7FFFFFFFu-magnitude : 0x80000000u+magnitude;
	}

	static void _Enable_Triangle_Draw(bool enable) { _EnableTriangleDraw=enable; }
	static bool _Is_Triangle_Draw_Enabled() { return _EnableTriangleDraw; }
};

#endif

