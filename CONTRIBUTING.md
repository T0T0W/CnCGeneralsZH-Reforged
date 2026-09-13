# Contributing

Pull requests are welcome. These are the things that decide whether one goes in as it is, goes in
in pieces, or waits for a question to be answered.

## One change per pull request

A pull request that does one thing is merged or turned down as a whole. Five unrelated fixes in one
get taken apart by hand, the parts that go in are rewritten against the current code, and they stop
being your commits. Split them and each one can be merged with your name on it.

Say in the description when a change removes or reverses something `CHANGELOG.md` describes. A
feature that disappears without a word is the fastest way to have a pull request held back.

## Commit messages

English, Conventional Commits, imperative, and under 72 characters on the first line:

```
type(scope): summary
```

The type is one of `feat`, `fix`, `perf`, `build`, `refactor`, `docs` or `test`. The scope is the
library or target the change lives in, such as `gameclient`, `gameengine`, `w3ddevice`, `ww3d2`,
`wwlib` or `cmake`. A `docs` commit may leave the scope out.

```
fix(gameclient): keep the minimap drag alive while the arrow keys scroll
perf(ww3d2): batch translucent particles and draw every one
docs: explain the pull request process
```

Every pull request runs a check that reads the first line of each of its commits and fails the ones
that do not match. Fix a failing message with `git rebase -i` on your own branch and force push it;
nobody minds a rewritten pull request branch.

## Before you open it

Build Release and run the tests. The build is Win32 only.

```console
cmake -S GeneralsMD/Code -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

A change to how the game behaves leaves a test behind that fails without it. Put the fix back to
the old behaviour once and watch the test go red before you trust it.

A change to what a unit or the computer opponent decides has to leave replays and network games
alone: `replay-check.ps1` plays the same match twice and compares the checksums. A switch or an
option that changes such a decision is off in network games, or every machine in the match plays a
different game.

A change a player can see gets an entry in `CHANGELOG.md` in the same commit. It is written for
players: what was wrong and what happens now, with a number where there is one, and no file,
function or option names except the ones a player types into `Options.ini`.

## What happens to it

A pull request that applies cleanly to `main` is squash merged. The commit message is written to
the rules above if yours was not, and you stay the author.

A pull request that conflicts with `main`, or where only part of it is taken, is ported by hand.
The commit that carries it names you as a co-author, the pull request is marked merged, and a
comment on it says which parts went in.

Questions are asked on the pull request, and it stays open until they are answered.
