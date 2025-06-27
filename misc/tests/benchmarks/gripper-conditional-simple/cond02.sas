begin_version
3
end_version
begin_metric
0
end_metric
6
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
Atom at-ball1(rooma)
Atom at-ball1(roomb)
Atom at-ball1(gripper)
end_variable
begin_variable
var2
-1
3
Atom at-ball2(rooma)
Atom at-ball2(roomb)
Atom at-ball2(gripper)
end_variable
begin_variable
var3
-1
3
Atom at-ball3(rooma)
Atom at-ball3(roomb)
Atom at-ball3(gripper)
end_variable
begin_variable
var4
-1
3
Atom at-ball4(rooma)
Atom at-ball4(roomb)
Atom at-ball4(gripper)
end_variable
begin_variable
var5
-1
5
Atom gripper(empty)
Atom gripper(ball1)
Atom gripper(ball2)
Atom gripper(ball3)
Atom gripper(ball4)
end_variable
0
begin_state
0
0
0
0
0
0
end_state
begin_goal
4
1 1
2 1
3 1
4 1
end_goal
13
begin_operator
move
0
2
1 0 1 0 -1 0
1 0 0 0 -1 1
1
end_operator
begin_operator
pickup ball1 rooma
3
0 0
1 0
5 0
2
0 1 -1 2
0 5 -1 1
1
end_operator
begin_operator
pickup ball1 roomb
3
0 1
1 1
5 0
2
0 1 -1 2
0 5 -1 1
1
end_operator
begin_operator
pickup ball2 rooma
3
0 0
2 0
5 0
2
0 2 -1 2
0 5 -1 2
1
end_operator
begin_operator
pickup ball2 roomb
3
0 1
2 1
5 0
2
0 2 -1 2
0 5 -1 2
1
end_operator
begin_operator
pickup ball3 rooma
3
0 0
3 0
5 0
2
0 3 -1 2
0 5 -1 3
1
end_operator
begin_operator
pickup ball3 roomb
3
0 1
3 1
5 0
2
0 3 -1 2
0 5 -1 3
1
end_operator
begin_operator
pickup ball4 rooma
3
0 0
4 0
5 0
2
0 4 -1 2
0 5 -1 4
1
end_operator
begin_operator
pickup ball4 roomb
3
0 1
4 1
5 0
2
0 4 -1 2
0 5 -1 4
1
end_operator
begin_operator
drop ball1
2
5 1
1 2
3
1 0 0 1 -1 0
1 0 1 1 -1 1
0 5 -1 0
1
end_operator
begin_operator
drop ball2
2
5 2
2 2
3
1 0 0 2 -1 0
1 0 1 2 -1 1
0 5 -1 0
1
end_operator
begin_operator
drop ball3
2
5 3
3 2
3
1 0 0 3 -1 0
1 0 1 3 -1 1
0 5 -1 0
1
end_operator
begin_operator
drop ball4
2
5 4
4 2
3
1 0 0 4 -1 0
1 0 1 4 -1 1
0 5 -1 0
1
end_operator
0