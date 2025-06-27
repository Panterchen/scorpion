begin_version
3
end_version
begin_metric
0
end_metric
9
begin_variable
have_parts0
-1
2
yes
no
end_variable
begin_variable
tools_available1
-1
2
yes
no
end_variable
begin_variable
base_assembled2
-1
2
yes
no
end_variable
begin_variable
head_attached3
-1
2
yes
no
end_variable
begin_variable
battery_installed4
-1
2
yes
no
end_variable
begin_variable
neck_screwed_in5
-1
2
yes
no
end_variable
begin_variable
head_stable6
-1
2
yes
no
end_variable
begin_variable
robot_powered_on7
-1
2
yes
no
end_variable
begin_variable
robot_balanced8
-1
2
yes
no
end_variable
0
begin_state
1
0
1
1
1
1
1
1
1
end_state
begin_goal
2
7 0
8 0
end_goal
4
begin_operator
fetch_parts
0
1
0 0 -1 0
1
end_operator
begin_operator
assemble_base
2
0 0
1 0
1
0 2 -1 0
1
end_operator
begin_operator
attach_head
1
2 0
4
0 3 -1 0
1 0 0 4 -1 0
1 1 0 5 -1 0
2 3 0 5 0 6 -1 0
1
end_operator
begin_operator
power_on_robot
2
4 0
3 0
2
0 7 -1 0
1 6 0 8 -1 0
1
end_operator
0