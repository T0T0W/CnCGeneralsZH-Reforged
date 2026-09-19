"""Rewrite the skirmish AI's opening build order in SkirmishScripts.scb.

EA's Hard scripts open every USA and China side on a barracks, hold the first power plant until a
second dozer exists, and queue the supply centre only once that power plant stands; China then puts
a gattling cannon by its supply dock ahead of the war factory. On Tournament Desert that brought
USA's first income at frame 2227 and China's war factory at 6785. The opening this writes is the
one a player uses: power plant, supply centre (the GLA stash), then the war factory (the arms dealer)
and the barracks together, and the dock defence after the war factory.

The build queue takes the newest request first (Player::addToPriorityBuildList puts it at the head),
and since AISkirmishPlayer::processBaseBuilding skips a request whose prerequisite is not standing,
the supply centre can be asked for on the first frame and still go up right after the power plant.

usage: opening.py <in.scb> <out.scb> <rungs> [<side index> ...]
       rungs is any of E N H, e.g. NH; no side index means every side.
"""
import copy
import sys

import scbtool

SIDE = 11
OBJECT_TYPE = 15
THIS_PLAYER = "<This Player>"


def scripts_by_name(script_list, rung):
    suffix = f" - {rung}"
    return {script.fields[0][:-len(suffix)]: script for _, script in scbtool.walk_scripts(script_list.children)
            if script.fields[0].endswith(suffix)}


def action_object(script):
    for child in script.children:
        if child.name == "ScriptAction":
            for parameter in child.fields[2]:
                if parameter[0] == OBJECT_TYPE:
                    return parameter[3]
    raise ValueError(f"{script.fields[0]} names no object type")


def set_condition(script, condition_chunk):
    script.children = [condition_chunk] + [child for child in script.children if child.name != "OrCondition"]


def single_condition(chunks, condition_name):
    """The first AND group in the file made of one condition of that name, to copy from."""
    for _, script in scbtool.walk_scripts(chunks):
        for condition in (child for child in script.children if child.name == "OrCondition"):
            if len(condition.children) == 1 and condition.children[0].fields[1] == condition_name:
                return condition
    raise ValueError(f"no lone {condition_name} condition in the file to copy")


def built_condition(built_template, object_type):
    condition = copy.deepcopy(built_template)
    for parameter in condition.children[0].fields[2]:
        if parameter[0] == OBJECT_TYPE:
            parameter[3] = object_type
        elif parameter[0] == SIDE:
            parameter[3] = THIS_PLAYER
    return condition


def rewrite_side(scripts, family, true_condition, built_template):
    """Returns the names of the scripts it changed."""
    supply = scripts[f"{family} Supply Center"]
    supply_type = action_object(supply)
    changed = [supply]
    set_condition(supply, true_condition)
    if family == "GLA":
        factory = scripts["GLA Arms Dealer"]
    else:
        factory = scripts[f"{family} War Factory"]
        power = scripts[f"{family} 1st Power Plant"]
        set_condition(power, true_condition)
        changed.append(power)
    barracks = scripts[f"{family} Barracks"]
    set_condition(barracks, built_condition(built_template, supply_type))
    changed.append(barracks)
    dock_defence = scripts.get("China Supply Gat") if family == "China" else None
    if dock_defence:
        set_condition(dock_defence, built_condition(built_template, action_object(factory)))
        changed.append(dock_defence)
    return [script.fields[0] for script in changed]


def main():
    source, target, rungs = sys.argv[1], sys.argv[2], sys.argv[3].upper()
    sides = {int(side) for side in sys.argv[4:]}
    table, chunks = scbtool.load(source)
    true_condition = single_condition(chunks, "CONDITION_TRUE")
    built_template = single_condition(chunks, "BUILT_BY_PLAYER")
    changed = []
    for list_chunk in (chunk for chunk in chunks if chunk.name == "PlayerScriptsList"):
        for index, script_list in enumerate(list_chunk.children):
            if sides and index not in sides:
                continue
            for rung in rungs:
                scripts = scripts_by_name(script_list, rung)
                family = next((f for f in ("USA", "China", "GLA") if f"{f} Supply Center" in scripts), None)
                if family:
                    changed += [f"[{index}] {name}" for name in rewrite_side(scripts, family, true_condition, built_template)]
    if not changed:
        raise SystemExit("no side matched")
    scbtool.save(target, table, chunks)
    print("\n".join(changed))


if __name__ == "__main__":
    main()
