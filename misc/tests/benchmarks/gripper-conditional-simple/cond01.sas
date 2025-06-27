begin_version
3
end_version
begin_metric
0
end_metric
2
begin_variable
var0
-1
2
Atom at-robby(rooma)
Atom at-robby(roomb)
end_variable
begin_variable
var1
-1
3
Atom at-ball(rooma)
Atom at-ball(roomb)
Atom at-ball(gripper)
end_variable
0
begin_state
0
0
end_state
begin_goal
1
1 1
end_goal
4
begin_operator
move
0
2
1 0 1 0 -1 0
1 0 0 0 -1 1
1
end_operator
begin_operator
pickup rooma
2
0 0
1 0
1
0 1 -1 2
1
end_operator
begin_operator
pickup roomb
2
0 1
1 1
1
0 1 -1 2
1
end_operator
begin_operator
drop
1
1 2
2
1 0 0 1 -1 0
1 0 1 1 -1 1
1
end_operator
0