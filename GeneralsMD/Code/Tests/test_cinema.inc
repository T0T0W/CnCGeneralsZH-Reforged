// -cinema's shot list. A shot that is half-read still flies a camera, just not the one written down,
// and footage is judged by eye, so every refused line has to say why and every accepted one has to
// carry exactly the numbers on it.

#include "GameClient/CinemaDirector.h"

TEST(cinema_parses_a_move_and_a_cut)
{
	CinemaShot shot;
	Bool isShot = FALSE;
	AsciiString reason;

	CHECK( CinemaDirector_parseLine( "2.5 move 1200 900 4", &shot, &isShot, &reason ) );
	CHECK( isShot );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_MOVE );
	CHECK_NEAR( shot.at, 2.5f, 0.001f );
	CHECK_NEAR( shot.routeX[ 0 ], 1200.0f, 0.001f );
	CHECK_NEAR( shot.routeY[ 0 ], 900.0f, 0.001f );
	CHECK_NEAR( shot.seconds, 4.0f, 0.001f );

	// a cut takes x y and then as much of zoom, angle and pitch as it cares to give
	CHECK( CinemaDirector_parseLine( "0 cut 500 600 0.8 45", &shot, &isShot, &reason ) );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_CUT );
	CHECK_EQ( shot.values, 2 );
	CHECK_NEAR( shot.value[ 0 ], 0.8f, 0.001f );
	CHECK_NEAR( shot.value[ 1 ], 45.0f, 0.001f );
	CHECK_NEAR( shot.seconds, 0.0f, 0.001f );
}

TEST(cinema_parses_a_route_follow_and_switches)
{
	CinemaShot shot;
	Bool isShot = FALSE;
	AsciiString reason;

	CHECK( CinemaDirector_parseLine( "10 route 6 100 100 200 150 300 100 # along the river", &shot, &isShot, &reason ) );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_ROUTE );
	CHECK_EQ( shot.routePoints, 3 );
	CHECK_NEAR( shot.routeX[ 2 ], 300.0f, 0.001f );
	CHECK_NEAR( shot.seconds, 6.0f, 0.001f );

	CHECK( CinemaDirector_parseLine( "12 follow AmericaTankCrusader 5", &shot, &isShot, &reason ) );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_FOLLOW );
	CHECK_STR( shot.name.str(), "AmericaTankCrusader" );
	CHECK_NEAR( shot.seconds, 5.0f, 0.001f );

	CHECK( CinemaDirector_parseLine( "0 hud on", &shot, &isShot, &reason ) );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_HUD );
	CHECK( shot.on );

	CHECK( CinemaDirector_parseLine( "   # only a comment", &shot, &isShot, &reason ) );
	CHECK( !isShot );
}

TEST(cinema_refuses_what_it_cannot_read)
{
	CinemaShot shot;
	Bool isShot = FALSE;
	AsciiString reason;

	// strtod would read "3s" as 3; a typo has to be refused, not fired at the wrong time
	CHECK( !CinemaDirector_parseLine( "3s move 1 2 3", &shot, &isShot, &reason ) );
	CHECK( !reason.isEmpty() );
	CHECK( !CinemaDirector_parseLine( "3 move 1 2", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "3 zoom 0 2", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "3 pitch 50 2", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "3 route 5 100", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "3 hud maybe", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "3 dolly 1 2", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "-1 shot", &shot, &isShot, &reason ) );
}

TEST(cinema_eases_and_curves_through_its_points)
{
	CHECK_NEAR( CinemaDirector_ease( -1.0f ), 0.0f, 0.0001f );
	CHECK_NEAR( CinemaDirector_ease( 0.5f ), 0.5f, 0.0001f );
	CHECK_NEAR( CinemaDirector_ease( 2.0f ), 1.0f, 0.0001f );
	// level at the ends: the first tenth covers far less than a tenth of the way
	CHECK( CinemaDirector_ease( 0.1f ) < 0.05f );

	const Real xs[] = { 0.0f, 100.0f, 200.0f };
	const Real ys[] = { 0.0f, 100.0f, 0.0f };
	Real x = 0.0f;
	Real y = 0.0f;
	CinemaDirector_routePoint( xs, ys, 3, 0.0f, &x, &y );
	CHECK_NEAR( x, 0.0f, 0.001f );
	CinemaDirector_routePoint( xs, ys, 3, 0.5f, &x, &y );
	CHECK_NEAR( x, 100.0f, 0.001f );
	CHECK_NEAR( y, 100.0f, 0.001f );
	CinemaDirector_routePoint( xs, ys, 3, 1.0f, &x, &y );
	CHECK_NEAR( x, 200.0f, 0.001f );
	CHECK_NEAR( y, 0.0f, 0.001f );
	// between the points it bends through them rather than cutting the corner
	CinemaDirector_routePoint( xs, ys, 3, 0.25f, &x, &y );
	CHECK( y > 50.0f );
}
