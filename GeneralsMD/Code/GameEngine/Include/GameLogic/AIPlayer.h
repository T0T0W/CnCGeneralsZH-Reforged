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

// AIPlayer.h
// Computerized opponent
// Author: Michael S. Booth, January 2002

#pragma once

#ifndef _AI_PLAYER_H_
#define _AI_PLAYER_H_

#include "Common/GameMemory.h"
#include "Common/Snapshot.h"
#include "GameLogic/AI.h"			// AISkillLevel, AIRole and the difficulty profile the ladder reads
#include "Common/GameCommon.h"		// MAX_PLAYER_COUNT, for the per-enemy scouting stamps

enum { INVALID_SKILLSET_SELECTION = -1 };

class BuildListInfo;
// this header used to compile only behind whoever happened to include Team.h first
class TeamPrototype;

/**
 * When a team is selected for training, a list of these
 * "work orders" are created, one for each member of the team.
 * This pairs team members with production buildings to keep 
 * track of who is building what, and allows us to track if
 * a building was destroyed while in the process of training a unit.
 */
class WorkOrder : public MemoryPoolObject,
									public Snapshot
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( WorkOrder, "WorkOrder" )		

public:

	WorkOrder():m_thing(NULL), m_factoryID(INVALID_ID), m_isResourceGatherer(false), m_isScout(false), m_numCompleted(0), m_numRequired(1), m_next(NULL) {};

	Bool isWaitingToBuild( void );		///< return true if nothing is yet building this unit
	void validateFactory( Player *thisPlayer );			///< verify factoryID still refers to an active object

public:

	const ThingTemplate *m_thing;			///< thing to build
	ObjectID m_factoryID;							///< ID of object that is building this, or zero if no-one is
	WorkOrder *m_next;
	Int			m_numCompleted;					  ///< Number built.
	Int			m_numRequired;					  ///< Number needed.
	Bool		m_required;								///< True if part of minimum requirement.
	Bool		m_isResourceGatherer;			///< True if resource gatherer.
	Bool		m_isScout;								///< True if this is the one unit kept for looking at the map.

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

};

inline Bool WorkOrder::isWaitingToBuild( void )
{
	if (m_factoryID!=INVALID_ID)
		return false;
	if (m_numCompleted >= m_numRequired)
		return false;
	return true;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class TeamInQueue : public MemoryPoolObject,
										public Snapshot
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( TeamInQueue, "TeamInQueue"  )

private:		

	MAKE_DLINK(TeamInQueue, TeamBuildQueue)				///< the instances of our prototype
	MAKE_DLINK(TeamInQueue, TeamReadyQueue)				///< the instances of our prototype

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

public:

	TeamInQueue() : 
		m_workOrders(NULL), 
		m_team(NULL), 
		m_nextTeamInQueue(NULL), 
		m_sentToStartLocation(false), 
		m_reinforcement(false), 
		m_stopQueueing(false),
		m_reinforcementID(INVALID_ID),
		//Added By Sadullah Nader
		//Initialization(s) inserted
		m_frameStarted(0),
		m_priorityBuild(FALSE)
		//		
	{
	}

	Bool isAllBuilt( void );				///< Returns true if the team is finished building.
	Bool isBuildTimeExpired( void );///< Returns true if the team has run out of build time.
	Bool isMinimumBuilt( void );		///< Returns true if the team has started building at least the minimum number of units.
	Bool includesADozer( void );		///< Returns true if the team includes a dozer unit.
	Bool areBuildsComplete( void );	///< Returns true if all units in factories have finished building.
	void disband( void );						///< Disbands the team (moves units into the default team).
	void stopQueueing(void) {m_stopQueueing=true;} ///< Stops building new units, just finishes current.

public:

	WorkOrder *m_workOrders;				///< list of work orders
	Bool m_priorityBuild;						///< True if the team is specifically requested.
	Team *m_team;										///< the team that units built by the m_workOrders go into
	TeamInQueue *m_nextTeamInQueue; ///< next
	Int	m_frameStarted;							///< Frame we started building.
	Bool m_sentToStartLocation;			///< Has it been sent to it's start location?
	Bool m_stopQueueing;						///< True if we are to quit queueing units (usually because we ran out of build time.)
	Bool m_reinforcement;						///< True if it is a unit to reinforce an existing team.
	ObjectID m_reinforcementID;			///< True if it is a unit to reinforce an existing team.

};


/**
 * The computer-controlled opponent.
 */
class AIPlayer : public MemoryPoolObject,
								 public Snapshot
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIPlayer, "AIPlayer"  )		

public:

	AIPlayer( Player *p );							///< constructor
	
	virtual Bool computeSuperweaponTarget(const SpecialPowerTemplate *power, Coord3D *pos, Int playerNdx, Real weaponRadius); ///< Calculates best pos for weapon given radius.

	/** What every computer player spent this logic frame, by job, plus whichever single one cost
		* the most.  The slow-frame report in GameLogic.cpp could only say "players 16.2ms", which is
		* enough to know a stutter is in here and not enough to do anything about it.  Reset once a
		* frame by AI::update; empty outside a logging build. */
	static void resetFrameProfile( void );
	static const char *getProfileReport( void );

	/** Base building is the expensive job once the expansion convoy is out of the way, and it is a
		* loop over the build list that calls several searches per entry. These name which one.
		* AISkirmishPlayer overrides processBaseBuilding, so both copies are instrumented and the
		* timers have to be reachable from either file. */
	enum BaseSubPhase
	{
		BASE_SUB_HOLE = 0,		///< scanning every object in the game for a GLA rebuild hole
		BASE_SUB_DOZERFIX,		///< a building under construction whose dozer went missing
		BASE_SUB_SAFE,				///< isLocationSafe for one build list entry
		BASE_SUB_FINDDOZER,		///< findDozer for one build list entry
		BASE_SUB_CANMAKE,			///< canMakeUnit for one build list entry
		BASE_SUB_BUILD,				///< actually placing the one building this pass will start
		BASE_SUB_COUNT
	};
	static void profileBaseSubBegin( void );
	static void profileBaseSubEnd( Int slot );
	static void profileBaseSubCount( Int slot );

public: // AIPlayer interface, may be overridden by AISkirmishPlayer.  jba.

	virtual void update();											///< simulates the behavior of a player

	virtual void newMap();											///< New map loaded call.

	/// Invoked when a unit I am training comes into existence
	virtual void onUnitProduced( Object *factory, Object *unit );

	/// Invoked when a structure I am building comes into existence
	virtual void onStructureProduced( Object *factory, Object *structure );

	virtual void buildSpecificAITeam(TeamPrototype *teamProto, Bool priorityBuild); ///< Builds this team immediately.

	virtual void buildAIBaseDefense(Bool flank); ///< Builds base defense on front or flank of base.

	virtual void buildAIBaseDefenseStructure(const AsciiString &thingName, Bool flank); ///< Builds base defense on front or flank of base.

	virtual void buildSpecificAIBuilding(const AsciiString &thingName); ///< Builds this building as soon as possible.


	virtual void recruitSpecificAITeam(TeamPrototype *teamProto, Real recruitRadius); ///< Builds this team immediately.

	virtual Bool isSkirmishAI(void) {return false;}
	virtual Player *getAiEnemy(void) {return NULL;}	///< Solo AI attacks based on scripting.  Only skirmish auto-acquires an enemy at this point.  jba.
	virtual Bool checkBridges(Object *unit, Waypoint *way) {return false;}
	/** Center, Flank or Backdoor: the approach with the least enemy firepower this AI knows about.
		* The script's own label when the rung does not read the map, or the label is not one of the three. */
	AsciiString chooseApproachLabel(const Coord3D *from, const AsciiString &requested, Int pathSuffix);
	/** C2: park an attack team that is not a wave on its own until the rest of the wave is in hand.
		* TRUE when it is parked, and the caller's own order must not go out. */
	Bool holdTeamForWave(Team *team, const AsciiString &approach, Int pathSuffix);
	virtual void repairStructure(ObjectID structure);

	virtual void selectSkillset(Int skillset);

public:
	Bool getBaseCenter(Coord3D *pos) const {*pos = m_baseCenter; return m_baseCenterSet;}
	/// Difficulty level for this player.
	GameDifficulty getAIDifficulty(void) const;
	void setAIDifficulty(GameDifficulty difficulty) {m_difficulty = difficulty;}

	/** Which rung of the six-step ladder this AI plays at, and what it is trying to do.  Two
		* independent axes: the rung says how well, the role says what.  See AI-ROADMAP.md D6/D8. */
	AISkillLevel getAISkillLevel(void) const {return m_skillLevel;}
	void setAISkillLevel(AISkillLevel level) {m_skillLevel = level;}
	AIRole getAIRole(void) const {return m_role;}

	/// This rung's knobs, for any behaviour that has a difficulty-dependent decision to make.
	const AIDifficultyProfile *getSkillProfile(void) const;

	/// What a seat that predates the ladder (an old save, a replay, a script) plays at.
	static AISkillLevel skillLevelForDifficulty(GameDifficulty difficulty);
	void buildBySupplies(Int minimumCash, const AsciiString &thingName ); ///< Builds a building by supplies.
	void buildSpecificBuildingNearestTeam( const AsciiString &thingName, const Team *team );
	void buildUpgrade(const AsciiString &upgrade ); ///< Builds an upgrade.
	/// A team is about to be destroyed.
	void aiPreTeamDestroy( const Team *team );
	/// Is the nearest supply source safe?
 	Bool isSupplySourceSafe( Int minSupplies );
	/// Is a supply source attacked?
	Bool isSupplySourceAttacked( void );

	Bool isLocationSafe( const Coord3D *pos, const ThingTemplate *tthing);

	/// Have the team guard a supply center.
	void guardSupplyCenter( Team *team, Int minSupplies );

	void setTeamDelaySeconds(Int delay) {m_teamSeconds = delay;}

	/// Calculates the closest construction zone location based on a template.
	Bool calcClosestConstructionZoneLocation( const ThingTemplate *constructTemplate, Coord3D *location );

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

	/** Look at the map.  There was no concept of scouting in the AI at all - it did not need one
		* while every strategic decision read the enemy's object list straight out of the game (see
		* the observer index on getPlayerStructureBounds).  One cheap unit, replaced when it dies,
		* touring the enemy start positions: that is what turns fog back into information. */
	/** Break off a fight that is being lost.  The word "retreat" did not appear anywhere in the AI:
		* teams fought to the last man, which is the single most visible thing that made it look
		* stupid.  Two levels, because pulling out only whole teams still loses the units that were
		* individually finished - Sins does exactly that, and keeps losing capital ships for it. */
	/** B3: decide to expand, instead of waiting for a script to say so.  buildBySupplies is a
		* working expansion mechanism that only ever ran when the "Build supply center" script action
		* fired - the AI never worked out for itself that it was running out of money. */
	virtual void doExpansion(void);

	virtual void doRetreats(void);

	/** C2: hold a finished team at the rally point until there is a force worth sending.  A string of
		* small waves is free veterancy for the other side. */
	Bool shouldHoldForMassing( TeamInQueue *team );

	virtual void doScouting(void);
	Object *findScout(void);						///< a spare unit of ours that can do the touring, not already scouting
	void queueScout(void);							///< ... or build the cheapest one that can
	Bool scoutInQueue(void);						///< one is already on order
	Bool nextScoutTarget(Int slot, const Coord3D *from, Coord3D *pos);	///< the stalest enemy start position worth the walk
	void updateStartIntel(void);				///< cross off the start positions the scouts have looked at, and deduce the rest
	void countStartIntel(Int *enemies, Int *occupied, Int *unchecked) const;	///< the three numbers the odds are made of
	Bool enemyStartGuess(Int playerNdx, Coord3D *pos);	///< where this enemy is, or the best address we have for him

	/** B?: an oil derrick is income standing in the open.  The AI used to walk past the neutral ones
		* and shoot the enemy's, which is the one thing you can do with a derrick that earns nothing. */
	virtual void doCapture(void);
	Object *findCapturer(void);					///< a unit of ours that can walk into a building and own it
	Object *nearestTechBuilding(const Coord3D *from, Bool wantOurs);	///< the closest one worth taking, or worth sitting on
	void queueCapturer(void);						///< ... or build the cheapest thing that can take one
	void queueSupportUnit(const ThingTemplate *tmpl, const char *what);	///< one cheap unit, outside the team system

	/** Past the hoard, buy what the build list never had: another production building beside the last
		* expansion when every one of a kind is busy, another income building when production keeps up,
		* and money units (China's hackers) from any factory standing idle. */
	virtual void doEconomy(void);

	/** Buy a power plant before the margin runs out rather than after, and put it on the far side of
		* the base from whoever this player is fighting. */
	virtual void doPower(void);

	Bool enemyDirection(Coord3D *dir);	///< unit vector from this base towards the nearest enemy's best known address

	void buyMoneyUnit(void);
	Bool placeNear(const ThingTemplate *tmpl, const Coord3D *center, Real innerRadius);	///< a legal, safe spot on a ring round center, queued for a dozer
	Real knownFirepowerAlongPath(Waypoint *way);	///< what this AI has seen that can shoot, along an approach

	virtual void doBaseBuilding(void);
	virtual void checkReadyTeams(void);
	virtual void checkQueuedTeams(void);
	virtual void doTeamBuilding(void);
	virtual void doUpgradesAndSkills(void);
	virtual Object *findDozer(const Coord3D *pos);
	virtual void queueDozer(void);
	void computeEnemyComposition( AIEnemyComposition *out, std::vector<AIVisibleEnemy> *army = NULL );	///< what this AI can see the enemy fielding, and optionally which units
	Real visibleEstateValue( Int playerNdx );					///< what this AI can see that player is worth, in build cost
	virtual Bool selectTeamToBuild( void );			///< determine the next team to build
	virtual Bool selectTeamToReinforce( Int minPriority );			///< determine the next team to reinforce
	virtual Bool startTraining( WorkOrder *order, Bool busyOK, AsciiString teamName);	///< find a production building that can handle the order, and start building
	virtual Bool isAGoodIdeaToBuildTeam( TeamPrototype *proto );		///< return true if team should be built
	virtual void processBaseBuilding( void );		///< do base-building behaviors
	virtual void processTeamBuilding( void );		///< do team-building behaviors
 	/** These two read another player's estate.  observerNdx is who is allowed to know: pass a
 		* player index and only what that player can see (or, for a building, has ever seen) is
 		* counted; -1 keeps the omniscient answer for a caller that is not a player's own thinking. */
 	static Int getPlayerSuperweaponValue( Coord3D *center, Int playerNdx, Real radius, Bool includeMilitaryUnits = TRUE, Int observerNdx = -1 );
// End of aiplayer interface. 

protected:

	MAKE_DLINK_HEAD(TeamInQueue, TeamBuildQueue);		///< List of teams being build
	MAKE_DLINK_HEAD(TeamInQueue, TeamReadyQueue);		///< List of teams built, waiting to reach rally point.

protected:
	Int computeStructureDelay( void );	///< frames to wait before trying the next structure
	Int computeTeamDelay( void );				///< frames to wait before trying the next team

public:
	/// the arithmetic behind the two above, free of the Player so a test can reach it
	static Int computeBuildDelay( Real seconds, Int money, Int poorAt, Int wealthyAt,
																Real poorMod, Real wealthyMod, Real rateScale );

	/**
		Where in a repeating check's cycle this player sits.  Every AIPlayer arms the same timers
		with the same constants on the same frame, so a lobby full of bots does its base building,
		team building and bridge repair in lockstep for the whole match and the cost of all of them
		lands on one logic frame.  Spreading them over the cycle changes *when* each one runs, not
		how often, and it is a function of the player index alone - the simulation stays deterministic.
	*/
	static Int computeUpdatePhase( Int playerIndex, Int cycleFrames );
protected:

	/**
		How much faster than the AIData delays this AI works.  1 = exactly as the data says.
		The skirmish AI overrides it: it is meant to react faster than a scripted campaign AI,
		and used to get that by clamping its own timers, which threw the data away.
	*/
	virtual Real getBuildRateScale( void ) { return 1.0f; }

	Bool isPossibleToBuildTeam( TeamPrototype *proto, Bool requireIdleFactory, Bool &needMoney );		///< return true if team can be considered for building
	Object *buildStructureNow(const ThingTemplate *bldgPlan, BuildListInfo *info );		///< Build a base buiding.
	Object *buildStructureWithDozer(const ThingTemplate *bldgPlan, BuildListInfo *info );		///< Build a base buiding.
	void clearTeamsInQueue( void );			///< Delete all teams in the build queue.
	void computeCenterAndRadiusOfBase(Coord3D *center, Real *radius);
	Object *findFactory(const ThingTemplate *thing, Bool busyOK); ///< Find a factory to build a unit.  If force is true, may return a busy factory.
	void queueUnits( void );						///< Check the team build list, & queue up units at any idle factories.
	void checkForSupplyCenter( BuildListInfo *info, Object *bldg);
 	void queueSupplyTruck(void);
	void updateBridgeRepair(void);
	Bool dozerInQueue(void);
	Object *findSupplyCenter(Int minSupplies);
	void getPlayerStructureBounds(Region2D *bounds, Int playerNdx, Bool conservative = FALSE, Int observerNdx = -1 );

protected:	 

	Player *m_player;									///< the Player we represent

	AISkillLevel m_skillLevel;				///< rung of the ladder: how well this AI plays
	AIRole		m_role;									///< what it is trying to do; rolled once, kept for the match

	enum { MAX_AI_SCOUTS = 2 };				///< the ladder's maxScouts never asks for more than this
	ObjectID	m_scoutID[ MAX_AI_SCOUTS ];	///< the units currently touring the map for us
	Int				m_scoutTargetFor[ MAX_AI_SCOUTS ];	///< which start position each one is walking to
	Int				m_scoutTimer;						///< frames until the next scouting check
	UnsignedInt m_scoutSeenFrame[ MAX_PLAYER_COUNT ];	///< when each start position was last looked at; 0 == never
	Bool			m_startChecked[ MAX_PLAYER_COUNT ];	///< start positions we have looked at, or deduced without looking
	Bool			m_startOccupied[ MAX_PLAYER_COUNT ];	///< ... and which of those turned out to hold an enemy
	Int				m_playerStartNdx[ MAX_PLAYER_COUNT ];	///< the start position each player is known to be at; -1 == not found yet
	UnsignedInt m_startIntelFrame;			///< frame the above was last brought up to date
	ObjectID	m_capturerID;						///< the unit currently out taking tech buildings for us
	Int				m_captureTimer;					///< frames until the next look for something to capture
	Int				m_retreatTimer;					///< frames until the next look at how the fights are going
	Int				m_expandTimer;					///< frames until the next look for somewhere to expand to

	Bool		m_readyToBuildTeam;				///< True if the team select timer has expired.
	Bool		m_readyToBuildStructure;	///< True if the buildDelay timer has expired.
	Int			m_teamTimer;							///< Counts out the time between teams, as specified by ini.
	Int			m_structureTimer;					///< Counts out the time between structures, as specified by ini.
	Int			m_teamSeconds;						///< How many seconds to delay between teams.

	Int			m_buildDelay;							///< Delay for building in case we are resource or prereq. limited.
	Int			m_teamDelay;							///< Delay for teams in case we are resource or factory prereq. limited.

	Int			m_frameLastBuildingBuilt;	///< When we built the last building.

	/* Where buildStructureWithDozer's search for somewhere to put a building had got to when it ran
		 out of its per-frame budget, and the spot it was searching around.  Deliberately not xferred:
		 both machines in a network game compute them the same way from the same frames, and a
		 savegame that restarts a half-finished scan loses nothing but a few frames of searching. */
	Real		m_buildProbeOffset;
	Int			m_buildProbeSkip;		///< position pairs of the ring at m_buildProbeOffset already tried
	Coord3D m_buildProbePos;

	GameDifficulty m_difficulty;

	Int			m_skillsetSelector;

	Coord3D m_baseCenter; // Center of the initial build list of structures.
	Bool		m_baseCenterSet; // True if baseCenter is valid.
	Real m_baseRadius; // Radius of the initial build list of structures.

	// Bridge repair info.
	enum {MAX_STRUCTURES_TO_REPAIR = 2};
	ObjectID m_structuresToRepair[MAX_STRUCTURES_TO_REPAIR];
	ObjectID m_repairDozer;
	UnsignedInt m_frameAfterFailedDozerQueue;	///< frame+1 of the last queueDozer that found no factory, 0 for never
	Coord3D  m_repairDozerOrigin;
	Int			 m_structuresInQueue;
	Bool		 m_dozerQueuedForRepair;
	Bool		 m_dozerIsRepairing;			///< the repair dozer is trying to repair the bridge.
	Int			 m_bridgeTimer;

	UnsignedInt	m_supplySourceAttackCheckFrame;
	ObjectID m_attackedSupplyCenter;

	ObjectID m_curWarehouseID;

	/** C2: attack teams parked at the staging point, waiting to go out together. */
	virtual void doWaves(void);
	enum { MAX_HELD_TEAMS = 16 };
	Bool				m_heldUsed[ MAX_HELD_TEAMS ];
	UnsignedInt	m_heldTeam[ MAX_HELD_TEAMS ];			///< TeamID; Team.h is not included here
	AsciiString	m_heldLabel[ MAX_HELD_TEAMS ];		///< the approach the script asked for
	Int					m_heldSuffix[ MAX_HELD_TEAMS ];		///< the enemy start index its path name ends in
	UnsignedInt	m_heldSince;											///< frame the first of the parked teams arrived
};

#endif // _AI_PLAYER_H_



