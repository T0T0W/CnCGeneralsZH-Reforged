/* Command & Conquer Generals Zero Hour - GPL-3.0-or-later */
#ifndef CAMERA_BOUNDARY_H
#define CAMERA_BOUNDARY_H

#include "Lib/BaseType.h"

// The camera target can cross an edge, but stays within a finite distance of the actual map.
inline Region2D cameraBoundaryFromMap(const Region3D& map, Real margin)
{
	if (margin < 0.0f) margin = 0.0f;
	Region2D boundary;
	boundary.lo.x = map.lo.x - margin;
	boundary.lo.y = map.lo.y - margin;
	boundary.hi.x = map.hi.x + margin;
	boundary.hi.y = map.hi.y + margin;
	return boundary;
}

inline Coord3D constrainCameraPosition(Coord3D pos, const Region2D& boundary)
{
	if (pos.x < boundary.lo.x) pos.x = boundary.lo.x;
	if (pos.x > boundary.hi.x) pos.x = boundary.hi.x;
	if (pos.y < boundary.lo.y) pos.y = boundary.lo.y;
	if (pos.y > boundary.hi.y) pos.y = boundary.hi.y;
	return pos;
}

#endif
