#include <moveit/move_group_interface/move_group_interface.h>
#include <moveit/planning_scene_monitor/planning_scene_monitor.h>
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include<geometric_shapes/solid_primitive_dims.h>

#include <moveit_msgs/DisplayRobotState.h>
#include <moveit_msgs/DisplayTrajectory.h>

#include <moveit_msgs/AttachedCollisionObject.h>
#include <moveit_msgs/CollisionObject.h>
#include <moveit_msgs/GraspPlanning.h>

#include <agile_grasp/Grasps.h>
#include <agile_grasp/Grasp.h>

#include<moveit_msgs/CollisionObject.h>
#include <ros/ros.h>

#include<iostream>

//torus width = 0.0014;

static const std::string PLANNING_GROUP = "panda_arm";
moveit::planning_interface::MoveGroupInterface* move_group;
moveit::planning_interface::MoveGroupInterface* hand_group;

void openGripper(trajectory_msgs::JointTrajectory& p)
{
	p.joint_names.resize(3);
	p.joint_names[0] = "panda_joint7";
	p.joint_names[1] = "panda_finger_joint1";
	p.joint_names[2] = "panda_finger_joint2";

	p.points.resize(1);
	p.points[0].positions.resize(3);
	p.points[0].positions[0] = 1.0;
	p.points[0].positions[1] = 0.477;
	p.points[0].positions[2] = 0.477;
}

void closeGripper(trajectory_msgs::JointTrajectory& p)
{
	p.joint_names.resize(3);
	p.joint_names[0] = "panda_joint7";
	p.joint_names[1] = "panda_finger_joint1";
	p.joint_names[2] = "panda_finger_joint2";

	p.points.resize(1);
	p.points[0].positions.resize(3);
	p.points[0].positions[0] = 0.0;
	p.points[0].positions[1] = 0.0;
	p.points[0].positions[2] = 0.0;
}

void agile_graspCallback(const agile_grasp::GraspsConstPtr& msg)
{
  ROS_INFO("Grasp points received");

  std::vector<moveit_msgs::Grasp> grasps;
   
  geometry_msgs::PoseStamped p;
  p.header.frame_id = "/panda_link0";
  p.pose.position.x = msg->grasps[0].surface_center.x+0.5;
  p.pose.position.y = msg->grasps[0].surface_center.y;
  p.pose.position.z = msg->grasps[0].surface_center.z+0.1;//14;
  p.pose.orientation.x = -0.923955;
  p.pose.orientation.y = -0.382501;
  p.pose.orientation.z = -0.000045;
  p.pose.orientation.w = 0.000024;

  moveit_msgs::Grasp g;
  g.grasp_pose = p;

//   g.pre_grasp_approach.direction.vector.z = 1.0;
  g.pre_grasp_approach.direction.header.frame_id = "/panda_link0";
  g.pre_grasp_approach.min_distance = 0.45;
  g.pre_grasp_approach.desired_distance = 0.5;

  g.post_grasp_retreat.direction.header.frame_id = "/panda_link0";
  g.post_grasp_retreat.direction.vector.z = 1.0;
  g.post_grasp_retreat.min_distance = 0.1;
  g.post_grasp_retreat.desired_distance = 0.25;

  openGripper(g.pre_grasp_posture);

  closeGripper(g.grasp_posture);

  grasps.push_back(g);
  move_group->setSupportSurfaceName("");
  move_group->pick("", grasps);
}

int main(int argc, char* argv[])
{
  /* code for main function */
  ros::init(argc, argv, "pick_and_place");
  ros::NodeHandle nh("~");
  ros::AsyncSpinner spinner(2);
  
  move_group = new moveit::planning_interface::MoveGroupInterface(PLANNING_GROUP);
//   hand_group = new moveit::planning_interface::MoveGroupInterface("hand");

  ros::WallDuration(1.0).sleep();


  ros::Subscriber sub = nh.subscribe("/find_grasps/grasps", 1, agile_graspCallback);

  spinner.start();



  ros::waitForShutdown();

  delete move_group;
  delete hand_group;

  return 0;
}