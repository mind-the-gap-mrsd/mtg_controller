// Created by Indraneel on 22/09/22

#include "lazy_traffic_agent.hpp"

#define PI (3.14159265)
#define CONTROL_ANGLE_THRESHOLD (PI/2.0)

void Agent::stopAgent(void)
{
  // TODO Check if velocity is non zero
  geometry_msgs::Twist vel;
  vel.linear.x = 0.0;
  vel.angular.z = 0.0;
  pub_vel_.publish(vel);
}

void Agent::sendVelocity(RVO::Vector2 velo)
{

  // Check if velocity is non zero
  if (AreSame(velo.x(), 0.0) && AreSame(velo.y(), 0.0)) {
    return;
  }
  // Update status because sending non zero velocity
  status.data = status.BUSY;

  // TODO Put limits on acceleration

  geometry_msgs::Twist vel;

  // Get heading diff with a dot product
  RVO::Vector2 heading = getCurrentHeading();
  RVO::Vector2 velo_norm = norm(velo);
  // Calculate cross product
  double cross_product = heading.x() * velo_norm.y() - heading.y() * velo_norm.x();
  // Calculate dot product
  vel.angular.z = acos(getCurrentHeading() * velo_norm);

  // Map linear velocity based on error in angular velocity
  vel.linear.x = 0.0 + v_max_ * (1.0 - fabs(vel.angular.z) / CONTROL_ANGLE_THRESHOLD);
  vel.linear.x = fabs(vel.angular.z)>CONTROL_ANGLE_THRESHOLD ? 0.0 : vel.linear.x;
  vel.angular.z = std::min(fabs(vel.angular.z), w_max_);
  vel.angular.z = copysign(vel.angular.z, cross_product);
  
  //vel.linear.x = v_max_;
  pub_vel_.publish(vel);

  //ROS_INFO("[LT_CONTROLLER-%s]: Sent Velo LIN: %f ANG: %f", &name_[0], vel.linear.x, vel.angular.z);
}

void Agent::updatePreferredVelocity()
{

  if (current_path_.empty())
  {
    preferred_velocity_ = RVO::Vector2(0.0, 0.0);
    // stopAgent();
    // return;
  }
  else if (current_path_.size() == 1 && checkifGoalReached())
  {

    current_path_.pop();
    preferred_velocity_ = RVO::Vector2(0.0, 0.0);
    stopAgent();
    ROS_WARN("[LT_CONTROLLER-%s] Goal reached!", &name_[0]);
    status.data = status.SUCCEEDED;
  }
  else
  {
    ppProcessLookahead(current_pose_.transform);

    // Calculate preferred velocity vector from current pose to lookahead point
    preferred_velocity_ = RVO::Vector2(lookahead_.transform.translation.x - current_pose_.transform.translation.x,
                                       lookahead_.transform.translation.y - current_pose_.transform.translation.y);
    preferred_velocity_ = norm(preferred_velocity_);
    preferred_velocity_ *= v_max_;
    ROS_INFO("[LT_CONTROLLER-%s]: Preferred Velo X: %f Y: %f", &name_[0], preferred_velocity_.x(), preferred_velocity_.y());
  }

}

//! Helper founction for computing eucledian distances in the x-y plane.
template <typename T1, typename T2>
double distance(T1 pt1, T2 pt2)
{
  return sqrt(pow(pt1.x - pt2.x, 2) + pow(pt1.y - pt2.y, 2));
}

void Agent::ppProcessLookahead(geometry_msgs::Transform current_pose)
{

  // for (; pp_idx_ < cartesian_path_.poses.size(); pp_idx_++)
  int pp_idx_ = 0;
  while (current_path_.size() > 1)
  {
    double dist_to_path = distance(current_path_.front().pose.position, current_pose.translation);
    if (dist_to_path > ld_)
    {
      // Save this as the lookahead point
      lookahead_.transform.translation.x = current_path_.front().pose.position.x;
      lookahead_.transform.translation.y = current_path_.front().pose.position.y;

      // TODO: See how the above conversion can be done more elegantly
      // using tf2_kdl and tf2_geometry_msgs
      ROS_INFO("[LT_CONTROLLER-%s]: Lookahead X: %f Y: %f", &name_[0], lookahead_.transform.translation.x, lookahead_.transform.translation.y);
      return;
    }
    else
    {
      current_path_.pop();
    }
  }

  if (!current_path_.empty())
  {

    // Lookahead point is the last point in the path
    lookahead_.transform.translation.x = current_path_.front().pose.position.x;
    lookahead_.transform.translation.y = current_path_.front().pose.position.y;

    ROS_INFO("[LT_CONTROLLER-%s]:***** Lookahead X: %f Y: %f", &name_[0], lookahead_.transform.translation.x, lookahead_.transform.translation.y);
  }
  else
  {
    ROS_ERROR("[LT_CONTROLLER-%s]: No path to follow. Stopping agent.", &name_[0]);
  }
}

bool Agent::checkifGoalReached()
{

  double distance_to_goal = distance(current_pose_.transform.translation, current_path_.front().pose.position);
  if (distance_to_goal <= goal_threshold_)
  {
    ROS_WARN("Goal reached!");
    return true;
  }
  else
    return false;
}

RVO::Vector2 Agent::getCurrentHeading()
{

  tf2::Quaternion quat(current_pose_.transform.rotation.x, current_pose_.transform.rotation.y, current_pose_.transform.rotation.z, current_pose_.transform.rotation.w);

  // Convert to RPY
  double roll, pitch, yaw;
  tf2::Matrix3x3(quat).getRPY(roll, pitch, yaw);

  // Convert to unit vector
  RVO::Vector2 heading(cos(yaw), sin(yaw));
  return heading;
}

void Agent::invokeRVO(std::unordered_map<std::string, Agent> agent_map)
{
  // Dont invoke RVO if the preferred velocity is zero
  // or if there is no path to follow
  if ((AreSame(preferred_velocity_.x(), 0.0) && AreSame(preferred_velocity_.y(), 0.0)) ||
       current_path_.empty()) {
    rvo_velocity_ = RVO::Vector2(0.0, 0.0);
    return;
  }

  // Calculate neighbours
  computeNearestNeighbors(agent_map);

  RVO::Vector2 current_position(current_pose_.transform.translation.x, current_pose_.transform.translation.y);
  // Create new self structure for RVO
  rvo_agent_info_s my_info{name_, current_velocity_, preferred_velocity_, current_position, v_max_};
  
  //ROS_INFO("[LT_CONTROLLER-%s]: Neighbours: %ld", &name_[0], neighbors_list_.size());

  // Calculate new velocity
  rvo_velocity_ = rvoComputeNewVelocity(my_info, neighbors_list_);

  // Handle the calculated velocity
  ROS_INFO("[LT_CONTROLLER-%s]: RVO Velo X: %f Y: %f", &name_[0], rvo_velocity_.x(), rvo_velocity_.y());
}

void Agent::computeNearestNeighbors(std::unordered_map<std::string, Agent> agent_map)
{
  priority_queue<AgentDistPair, vector<AgentDistPair>, greater<AgentDistPair>> all_neighbors;
  RVO::Vector2 my_pose(current_pose_.transform.translation.x, current_pose_.transform.translation.y);
  neighbors_list_.clear();

  // Crop agents based on distance
  for (const auto &agent : agent_map) {
    
    // Ignore self
    if(agent.first == name_)
      continue;

    RVO::Vector2 neigh_agent_pos(agent.second.current_pose_.transform.translation.x, agent.second.current_pose_.transform.translation.y);
    string neighbour_agent_name = agent.first;
    float euc_distance = euc_dist(neigh_agent_pos, my_pose);

    if (euc_distance < MAX_NEIGH_DISTANCE)
      all_neighbors.push(make_pair(neighbour_agent_name, euc_distance));
  }

  // Crop agents based on max neighbours
  for (int i = 0; i < MAX_NEIGHBORS && !all_neighbors.empty(); i++) {

    // Get the nearest neighbor from the priority queue
    AgentDistPair agent_dist_pair = all_neighbors.top();
    all_neighbors.pop();

    // Create and add the nearest neighbor to the list of neighbors
    rvo_agent_info_s neigh;
    neigh.agent_name = agent_dist_pair.first;
    RVO::Vector2 neigh_agent_pos(agent_map[neigh.agent_name].current_pose_.transform.translation.x, 
                                  agent_map[neigh.agent_name].current_pose_.transform.translation.y);
    neigh.current_position = neigh_agent_pos;
    neigh.currrent_velocity = agent_map[neigh.agent_name].current_velocity_;
    neigh.preferred_velocity = agent_map[neigh.agent_name].preferred_velocity_;
    neigh.max_vel = agent_map[neigh.agent_name].v_max_;
    neighbors_list_.push_back(neigh);
  }
}