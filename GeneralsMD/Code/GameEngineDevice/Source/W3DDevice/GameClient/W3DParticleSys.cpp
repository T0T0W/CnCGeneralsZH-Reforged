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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// W3DParticleSys.cpp
// W3D Particle System implementation
// Author: Michael S. Booth, November 2001

#include "common/GlobalData.h"
#include "GameClient/Color.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/heightmap.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DSnow.h"
#include "WW3D2/Camera.h"
#include "WW3D2/dx8wrapper.h"
#include "Common/JobSystem.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//------------------------------------------------------------------------------ Performance Timers 
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

//-------------------------------------------------------------------------------------------------


#include "Common/QuickTrig.h"

#ifdef DEBUG_LOGGING
extern Real TheParticleFillMS;
extern UnsignedInt TheParticlesPastGroupLimit;
#endif

/** The preset a particle shader type draws with, and whether it names one at all.  An invalid type
	* leaves the point group's previous shader in place on the old path, so it does not take the
	* direct one. */
static Bool particlePresetShader( ParticleSystemInfo::ParticleShaderType type, ShaderClass *shader )
{
	switch( type )
	{
		case ParticleSystemInfo::ADDITIVE:	*shader = ShaderClass::_PresetAdditiveSpriteShader;				return TRUE;
		case ParticleSystemInfo::ALPHA:			*shader = ShaderClass::_PresetAlphaSpriteShader;					return TRUE;
		case ParticleSystemInfo::ALPHA_TEST:	*shader = ShaderClass::_PresetATestSpriteShader;					return TRUE;
		case ParticleSystemInfo::MULTIPLY:	*shader = ShaderClass::_PresetMultiplicativeSpriteShader;	return TRUE;
	}
	return FALSE;
}

/** What every job of one frame's billboard fill reads. */
struct BillboardFillJob
{
	W3DParticleSystemManager::BillboardFill *	fills;
	Matrix4x4																	view;
	Real																			centerX, centerY, centerZ;
	Real																			extentX, extentY, extentZ;
};

/// systems a pool thread claims at once; a system is a few hundred particles, so one claim a system
/// would spend more on the interlocked claim than the work is worth
static const Int BILLBOARD_FILLS_PER_CLAIM = 4;

/** One sorted billboard system's quads, written into the range reserved for it.  Reads the system's
	* particles and writes only that range and that fill's counts: no allocation, no device calls,
	* nothing another job touches.  The cull and the 512-a-system cut are the ones the serial loop
	* below applies to every other system. */
static void fillBillboards( Int index, void *context )
{
	BillboardFillJob *job = (BillboardFillJob *)context;
	W3DParticleSystemManager::BillboardFill &fill = job->fills[ index ];
	VertexFormatXYZNDUV2 *quad = fill.range.Vertices;
	Int drawn = 0;

	for (Particle *p = fill.system->getFirstParticle(); p; p = p->m_systemNext)
	{
		const Coord3D *pos = p->getPosition();
		const Real psize = p->getSize();

		//Cull particle to edges of screen and terrain.
		if (WWMath::Fabs(pos->x - job->centerX) > (job->extentX + psize))
			continue;

		if (WWMath::Fabs(pos->y - job->centerY) > (job->extentY + psize))
			continue;

		if (WWMath::Fabs(pos->z - job->centerZ) > (job->extentZ + psize))
			continue;

		const RGBColor *color = p->getColor();
		const unsigned packed = DX8Wrapper::Convert_Color_Clamp(
			Vector4( color->red, color->green, color->blue, p->getAlpha() ) );
		const uint8 orientation = (uint8)(p->getAngle() * 255.0f / (2.0f * PI));
		PointGroupClass::Write_Billboard( quad, job->view, Vector3( pos->x, pos->y, pos->z ), psize,
			orientation, packed );
		quad += 4;

		if (++drawn == fill.capacity)
		{
			fill.pastLimit = fill.system->getParticleCount() - drawn;
			break;
		}
	}

	fill.drawn = drawn;
}

W3DParticleSystemManager::W3DParticleSystemManager()
{
	m_pointGroup = NULL;
	m_streakLine = NULL;
	m_posBuffer = NULL;
	m_RGBABuffer = NULL;
	m_sizeBuffer = NULL;
	m_angleBuffer = NULL;
	m_readyToRender = false;

	m_onScreenParticleCount = 0;

	m_pointGroup = NEW PointGroupClass();
	//m_streakLine = NULL;
	m_streakLine = NEW StreakLineClass();
	
	m_posBuffer = NEW_REF( ShareBufferClass<Vector3>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_posBuffer") );
	m_RGBABuffer = NEW_REF( ShareBufferClass<Vector4>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_RGBABuffer") );
	m_sizeBuffer = NEW_REF( ShareBufferClass<float>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_sizeBuffer") );
	m_angleBuffer = NEW_REF( ShareBufferClass<uint8>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_angleBuffer") );
}

W3DParticleSystemManager::~W3DParticleSystemManager()
{
	delete m_pointGroup;

//	W3DDisplay::m_3DScene->Remove_Render_Object( m_streakLine );

	if (m_streakLine)
	{
		REF_PTR_RELEASE(m_streakLine);
	}

	REF_PTR_RELEASE(m_posBuffer);
	REF_PTR_RELEASE(m_RGBABuffer);
	REF_PTR_RELEASE(m_sizeBuffer);
	REF_PTR_RELEASE(m_angleBuffer);
}

/**
 * Hack because DoParticles is called from Flush(), which is called
 * multiple times per frame.  We only want to render once.
 * @todo Clean up the flag/Flush hack.
 */
void W3DParticleSystemManager::queueParticleRender()
{
	m_readyToRender = true;
}

/**
 * Nasty hack to render particles last. Called directly by WW3D::Flush()
 */
void DoParticles( RenderInfoClass &rinfo )
{
	if (TheParticleSystemManager)
		TheParticleSystemManager->doParticles(rinfo);
}

void W3DParticleSystemManager::doParticles(RenderInfoClass &rinfo)
{

	if (m_readyToRender == false)
		return;

	// external mechanism must tell us when it's OK to render again...
	m_readyToRender = false;

	//reset each frame
	/// @todo lorenzen sez: this should be debug only:
	m_onScreenParticleCount = 0;

	Int visibleSmudgeCount = 0;
	if (TheSmudgeManager)
		TheSmudgeManager->setSmudgeCountLastFrame(0);	//keep track of visible smudges

 	const FrustumClass & frustum = rinfo.Camera.Get_Frustum();
	AABoxClass bbox;

	//Get a bounding box around our visible universe.  Bounded by terrain and the sky
	//so much tighter fitting volume than what's actually visible.  This will cull
	//particles falling under the ground.

 	TheTerrainRenderObject->getMaximumVisibleBox(frustum, &bbox, TRUE);

	//@todo lorenzen sez: put these in registers for sure
	Real bcX = bbox.Center.X;
	Real bcY = bbox.Center.Y;
	Real bcZ = bbox.Center.Z;
	Real beX = bbox.Extent.X;
	Real beY = bbox.Extent.Y;
	Real beZ = bbox.Extent.Z;

	unsigned int personalities[MAX_POINTS_PER_GROUP];


	m_fieldParticleCount = 0;

	SmudgeSet *set=NULL;
	if (TheSmudgeManager)
		set=TheSmudgeManager->addSmudgeSet();	//global smudge set through which all smudges are rendered.

#ifdef DEBUG_LOGGING
	Int64 tFillStart, tFillEnd;
	QueryPerformanceCounter( (LARGE_INTEGER *)&tFillStart );
#endif

	ParticleSystemManager::ParticleSystemList &particleSysList = TheParticleSystemManager->getAllParticleSystems();

	// Sorted billboards - most smoke, fire and dust - are written on the job pool.  With a hundred
	// thousand particles, walking them one system after another was a quarter of the frame.  Their
	// room in the sorting array is reserved here in list order, and the loop below inserts each one
	// into the sorting pool at its place in the same order, so the pool sees them as it always did.
	// Smudges, which draw on the client's random stream, streaks, volume particles, ground-aligned and
	// alpha-tested systems stay in that loop as they were.
	m_billboardFills.clear();
	for( ParticleSystemManager::ParticleSystemListIt it = particleSysList.begin(); it != particleSysList.end(); ++it)
	{
		ParticleSystem *sys = (*it);
		if (!sys || sys->isUsingDrawables())
			continue;
		if (*((DWORD *)sys->getParticleTypeName().str()) == 0x44554D53)
			continue;

		BillboardFill fill;
		if (sys->isUsingStreak() || !sys->shouldBillboard() || sys->getVolumeParticleDepth() > 1
				|| !particlePresetShader( sys->getShaderType(), &fill.shader )
				|| !PointGroupClass::Would_Sort_Billboards( fill.shader ))
			continue;

		const Int particles = (Int)sys->getParticleCount();
		if (particles == 0)
			continue;

		fill.system = sys;
		fill.capacity = (particles < (Int)MAX_POINTS_PER_GROUP) ? particles : (Int)MAX_POINTS_PER_GROUP;
		fill.fieldIncrement = ( sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned != FALSE ) ? 1 : 0;
		fill.drawn = 0;
		fill.pastLimit = 0;
		fill.texture = W3DDisplay::m_assetManager->Get_Texture( sys->getParticleTypeName().str() );
		PointGroupClass::Reserve_Sorted_Billboards( fill.capacity, &fill.range );
		m_billboardFills.push_back( fill );
	}

	BillboardFillJob fillJob;
	fillJob.fills = m_billboardFills.empty() ? NULL : &m_billboardFills[ 0 ];
	DX8Wrapper::Get_Transform( D3DTS_VIEW, fillJob.view );
	fillJob.centerX = bcX;
	fillJob.centerY = bcY;
	fillJob.centerZ = bcZ;
	fillJob.extentX = beX;
	fillJob.extentY = beY;
	fillJob.extentZ = beZ;
	JobSystem::parallel_for( (Int)m_billboardFills.size(), BILLBOARD_FILLS_PER_CLAIM, fillBillboards, &fillJob );
	size_t nextFill = 0;
	for( ParticleSystemManager::ParticleSystemListIt it = particleSysList.begin(); it != particleSysList.end(); ++it)
	{
		ParticleSystem *sys = (*it);
		if (!sys) {
			continue;
		}

		// only look at particle/point style systems
		if (sys->isUsingDrawables())
			continue;

		//temporary hack that checks if texture name starts with "SMUD" - if so, we can assume it's a smudge type
		if (/*sys->isUsingSmudge()*/ *((DWORD *)sys->getParticleTypeName().str()) == 0x44554D53)
		{
			if (TheSmudgeManager && ((W3DSmudgeManager*)TheSmudgeManager)->getHardwareSupport() && TheGlobalData->m_useHeatEffects)
			{
				//set-up all the per-particle
				for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
				{
					const Coord3D *pos = p->getPosition();
					Real psize = p->getSize();

					//Cull particle to edges of screen and terrain.
					if (WWMath::Fabs( pos->x - bcX ) > ( beX + psize ) )
						continue;

					if (WWMath::Fabs( pos->y - bcY ) > ( beY + psize ) )
						continue;

					if (WWMath::Fabs( pos->z - bcZ ) > ( beZ + psize ) )
						continue;

					Smudge *smudge = set->addSmudgeToSet();

					smudge->m_pos.Set( pos->x, pos->y, pos->z );
					// Same range on both axes. The vertical one was half the horizontal, which
					// nobody could see while the centre vertex was reading the horizontal offset
					// for both - now that it reads .Y, a heat smudge pulled twice as far sideways.
					smudge->m_offset.Set( GameClientRandomValueReal(-0.06f,0.06f), GameClientRandomValueReal(-0.06f,0.06f) );
					smudge->m_size = psize;
					smudge->m_opacity = p->getAlpha();
					visibleSmudgeCount++;
				}
			}
			continue;
		}

		if (nextFill < m_billboardFills.size() && m_billboardFills[ nextFill ].system == sys)
		{
			BillboardFill &fill = m_billboardFills[ nextFill++ ];
			PointGroupClass::Insert_Sorted_Billboards( &fill.range, fill.drawn, fill.texture, fill.shader );
			fill.texture->Release_Ref();	// the draw state took its own reference
			m_fieldParticleCount += fill.fieldIncrement * fill.drawn;
			m_onScreenParticleCount += fill.drawn;
#ifdef DEBUG_LOGGING
			TheParticlesPastGroupLimit += fill.pastLimit;
#endif
			continue;
		}

		/// @todo lorenzen sez: declare these outside the sys loop, and put some in registers
		// initialize them here still, of course
		// build W3D particle buffer
		Int count = 0;
		Vector3 *posArray = m_posBuffer->Get_Array();
		Real *sizeArray = m_sizeBuffer->Get_Array();
		Vector4 *RGBAArray = m_RGBABuffer->Get_Array();
		uint8 *angleArray = m_angleBuffer->Get_Array();
		const Coord3D *pos;
		const RGBColor *color;
		Real psize;
		// the same answer for every particle of the system, so asked once rather than once a particle
		const UnsignedInt fieldParticleIncrement =
			( sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned != FALSE ) ? 1 : 0;




		//set-up all the per-particle
		for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
		{
			pos = p->getPosition();
			psize = p->getSize();

			//Cull particle to edges of screen and terrain.
			if (WWMath::Fabs(pos->x - bcX) > (beX + psize))
				continue;

			if (WWMath::Fabs(pos->y - bcY) > (beY + psize))
				continue;

			if (WWMath::Fabs(pos->z - bcZ) > (beZ + psize))
				continue;

			m_fieldParticleCount += fieldParticleIncrement;
			
			//@todo lorenzen sez: use pointer arithmetic for these arrays
			personalities[count] = p->getPersonality();
			
			posArray[count].X = pos->x;
			posArray[count].Y = pos->y;
			posArray[count].Z = pos->z;

			sizeArray[count] = psize;

			color = p->getColor();
			RGBAArray[count].X = color->red;
			RGBAArray[count].Y = color->green;
			RGBAArray[count].Z = color->blue;
			RGBAArray[count].W = p->getAlpha();
		
			angleArray[count] = (uint8)(p->getAngle() * 255.0f / (2.0f * PI));
			
			if (++count == MAX_POINTS_PER_GROUP)
			{
#ifdef DEBUG_LOGGING
				TheParticlesPastGroupLimit += sys->getParticleCount() - count;
#endif
				break;
			}
		}

		if ( count == 0 )
			continue;	//this system has no particles to render

		TextureClass *texture = W3DDisplay::m_assetManager->Get_Texture( sys->getParticleTypeName().str() );

		
		if ( m_streakLine && sys->isUsingStreak() && (count >= 2) ) 
		{
			m_streakLine->Reset_Line();

			m_streakLine->Set_Texture( texture );
			texture->Release_Ref();//release reference since it's held by streakline
			switch( sys->getShaderType() )
			{
				case ParticleSystemInfo::ADDITIVE:
					m_streakLine->Set_Shader( ShaderClass::_PresetAdditiveSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA:
					m_streakLine->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA_TEST:
					m_streakLine->Set_Shader( ShaderClass::_PresetATestSpriteShader );
					break;
				case ParticleSystemInfo::MULTIPLY:
					m_streakLine->Set_Shader( ShaderClass::_PresetMultiplicativeSpriteShader );
					break;
			}
			
			//UPDATE THE STREAK'S ARRAYS
			m_streakLine->Set_LocsWidthsColors( 
				count,
				m_posBuffer->Get_Array(),
				m_sizeBuffer->Get_Array(),
				m_RGBABuffer->Get_Array(),
				&personalities[0]
				);

			//WWASSERT( m_streakLine->Get_Num_Points() == count );

			// This is the happy place for this!
			RGBAArray[0].X = 0;//eliminates the scissor edge on the trailing edge of the streak
			RGBAArray[0].Y = 0;
			RGBAArray[0].Z = 0;
			RGBAArray[0].W = 0;


			//RENDER STREAK!
			m_streakLine->Render( rinfo );
			
		}
		else 
		{

			WWASSERT( m_pointGroup );

			if ( m_pointGroup ) // this catches the particle and volumeparticle cases
			{
				// render all the systems' particles
				m_pointGroup->Set_Texture( texture );
				texture->Release_Ref();//release reference since it's held by pointGroup
				m_pointGroup->Set_Flag( PointGroupClass::TRANSFORM, true );	// transform to screen space

				switch( sys->getShaderType() )
				{
					case ParticleSystemInfo::ADDITIVE:
						m_pointGroup->Set_Shader( ShaderClass::_PresetAdditiveSpriteShader );
						break;
					case ParticleSystemInfo::ALPHA:
						m_pointGroup->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
						break;
					case ParticleSystemInfo::ALPHA_TEST:
						m_pointGroup->Set_Shader( ShaderClass::_PresetATestSpriteShader );
						break;
					case ParticleSystemInfo::MULTIPLY:
						m_pointGroup->Set_Shader( ShaderClass::_PresetMultiplicativeSpriteShader );
						break;
				}

				/// @todo Use both QUADS and TRIS for particles
				m_pointGroup->Set_Point_Mode( PointGroupClass::QUADS );
				m_pointGroup->Set_Arrays( m_posBuffer, m_RGBABuffer, NULL, m_sizeBuffer, m_angleBuffer, NULL, count );
				m_pointGroup->Set_Billboard(sys->shouldBillboard());

				/// @todo Support animated texture particles
				/// @todo lorenzen sez: unimplemented code wastes cpu cycles
				m_pointGroup->Set_Point_Frame( 0 );

				//RENDER IT!
				if( sys->getVolumeParticleDepth() > 1 )
				{
					m_pointGroup->RenderVolumeParticle( rinfo, sys->getVolumeParticleDepth() );
				}
				else
					m_pointGroup->Render( rinfo );
		
			}
		}


		/// @todo lorenzen sez: this should be debug only:
		//add particle count to total
		m_onScreenParticleCount += count;

	/*
		// draw the wind vector for this particle system on the screen
		UnsignedInt width = TheDisplay->getWidth();
		UnsignedInt height = TheDisplay->getHeight();
		Coord3D worldStart, worldEnd;
		ICoord2D pixelStart, pixelEnd;
		sys->getPosition( &worldStart );
		worldEnd.x = Cos( sys->getWindAngle() ) * 50.0f + worldStart.x;
		worldEnd.y = Sin( sys->getWindAngle() ) * 50.0f + worldStart.y;
		worldEnd.z = worldStart.z;
		TheTacticalView->worldToScreen( &worldStart, &pixelStart );
		TheTacticalView->worldToScreen( &worldEnd, &pixelEnd );
		Color colorStart = GameMakeColor( 255, 255, 255, 255 );
		Color colorEnd = GameMakeColor( 255, 128, 128, 255 );
		TheDisplay->drawLine( pixelStart.x, pixelStart.y, pixelEnd.x, pixelEnd.y, 1.0f, colorStart, colorEnd );
	*/


	}// next system

		/// @todo lorenzen sez: this should be debug only:
	TheParticleSystemManager->setOnScreenParticleCount(m_onScreenParticleCount);

#ifdef DEBUG_LOGGING
	QueryPerformanceCounter( (LARGE_INTEGER *)&tFillEnd );
	{
		Int64 freq;
		QueryPerformanceFrequency( (LARGE_INTEGER *)&freq );
		if( freq > 0 )
			TheParticleFillMS += (Real)((double)(tFillEnd - tFillStart) * 1000.0 / (double)freq);
	}
#endif

	//Draw any particles belonging to weather effects
	if (TheSnowManager)
		((W3DSnowManager *)TheSnowManager)->render(rinfo);

	//Now process screen smudges which are particles that distort the background behind them.
	if(TheSmudgeManager)
	{
		((W3DSmudgeManager *)TheSmudgeManager)->render(rinfo);
		TheSmudgeManager->reset();	//clear all the smudges after rendering since we fill again each frame.
		TheSmudgeManager->setSmudgeCountLastFrame(visibleSmudgeCount);
	}
}
