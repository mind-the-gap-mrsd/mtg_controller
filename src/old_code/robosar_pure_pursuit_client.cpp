#include <ros/ros.h>
#include <actionlib/client/simple_action_client.h>
#include <actionlib/client/terminal_state.h>
#include <mtg_controller/PurePursuitAction.h>

int main (int argc, char **argv)
{
  ros::init(argc, argv, "test_pure_pursuit");

  // create the action client
  // true causes the client to spin its own thread
  actionlib::SimpleActionClient<mtg_pure_pursuit::PurePursuitAction> ac("purepursuit", true);

  ROS_INFO("Waiting for action server to start.");
  // wait for the action server to start
  ac.waitForServer(); //will wait for infinite time

  ROS_INFO("Action server started, sending goal.");
  // send a goal to the action
  mtg_pure_pursuit::PurePursuitGoal goal;

  goal.path.header.frame_id = "map";
  goal.path.header.seq = 0;
  goal.path.header.stamp = ros::Time::now();

  geometry_msgs::PoseStamped pose;
  /*pose.header.seq=0;
  pose.header.stamp = ros::Time::now();
  pose.header.frame_id = "map";
  pose.pose.position.x = 5.000001175150732;
  pose.pose.position.y = -15.93542187094954e-08;
  pose.pose.orientation.z = -0.18145437944873438;
  pose.pose.orientation.w = 0.9833993635237287;
  goal.path.poses.push_back(pose);

  pose.header.seq=0;
  pose.header.stamp = ros::Time::now();
  pose.header.frame_id = "map";
  pose.pose.position.x = 5.432267416943564;
  pose.pose.position.y = -0.16514412848986026;
  pose.pose.orientation.z = -0.1613906061700738;
  pose.pose.orientation.w = 0.9868906080412642;
  goal.path.poses.push_back(pose);*/

  pose.header.seq=0;
  pose.header.stamp = ros::Time::now();
  pose.header.frame_id = "map";
  pose.pose.position.x = 0.5;
  pose.pose.position.y = -0.18;
  pose.pose.orientation.z = -0.18145437944873438;
  pose.pose.orientation.w = 0.9833993635237287;
  goal.path.poses.push_back(pose);

  pose.header.seq=0;
  pose.header.stamp = ros::Time::now();
  pose.header.frame_id = "map";
  pose.pose.position.x = 0.5;
  pose.pose.position.y = -0.77;
  pose.pose.orientation.z = -0.1613906061700738;
  pose.pose.orientation.w = 0.9868906080412642;
  goal.path.poses.push_back(pose);
  //goal.path.poses[0].pose.position.x = 1.0;
  //goal.path.poses[0].pose.orientation.w = 1.0;
  ac.sendGoal(goal);

  //wait for the action to return
  bool finished_before_timeout = ac.waitForResult(ros::Duration(30.0));

  if (finished_before_timeout)
  {
    actionlib::SimpleClientGoalState state = ac.getState();
    ROS_INFO("Action finished: %s",state.toString().c_str());
  }
  else
    ROS_INFO("Action did not finish before the time out.");

  //exit
  return 0;
}