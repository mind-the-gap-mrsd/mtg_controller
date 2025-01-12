// Created by Indraneel and Naren on 19/09/22

#include "lazy_traffic_controller.hpp"
#include "mtg_messages/agent_status.h"


LazyTrafficController::LazyTrafficController(): controller_active_(true), fleet_status_outdated_(false), map_frame_id_("map"),
                                            velocity_calc_period_s(0.2), controller_period_s(0.2), nh_("mtg_controller"),tf_listener_(tf_buffer_)  {
    
    status_subscriber_ = nh_.subscribe("/mtg_agent_bringup_node/status", 1, &LazyTrafficController::statusCallback, this);

    // subscribe to occupancy grid map
    occupancy_grid_subscriber_ = nh_.subscribe("/map", 1, &LazyTrafficController::occupancyGridCallback, this);
    // Get latest fleet info from agent bringup
    status_client_ = nh_.serviceClient<mtg_messages::agent_status>("/mtg_agent_bringup_node/agent_status");
    // Get active agents from agent bringup
    active_agents = getFleetStatusInfo();
    ROS_INFO(" [LT_CONTROLLER] Active fleet size %ld",active_agents.size());

    // Initialise agent map
    initialiseAgentMap(active_agents);

    // Start controller thread
    traffic_controller_thread_ = std::thread(&LazyTrafficController::RunController, this);

    // advertise controller service
    controller_service_ = nh_.advertiseService("lazy_traffic_controller", &LazyTrafficController::controllerServiceCallback, this);

    // Start controller timer
    controller_timer_ = nh_.createTimer(ros::Duration(controller_period_s),boost::bind(&LazyTrafficController::computeVelocities, this, _1));
}

LazyTrafficController::~LazyTrafficController() {

    controller_active_ = false;
    traffic_controller_thread_.join();
}

// subscribe to occupancy grid map and update the map
void LazyTrafficController::occupancyGridCallback(const nav_msgs::OccupancyGrid &occupancy_grid_msg) {
    std::lock_guard<std::mutex> lock(map_mutex);
    occupancy_grid_map_ = occupancy_grid_msg;
}

void LazyTrafficController::statusCallback(const std_msgs::Bool &status_msg) {
    fleet_status_outdated_ = true;
}


bool LazyTrafficController::controllerServiceCallback(mtg_messages::mtg_controller::Request &req,
                                                      mtg_messages::mtg_controller::Response &res) {
    
    std::lock_guard<std::mutex> lock(map_mutex);
    if(req.stop_controller) {
        ROS_INFO(" [LT_CONTROLLER] Emergency stop requested");
        // TODO
        // Stop all agents
        for(auto &agent : agent_map_) {
            agent.second.stopAgent();
            agent.second.clearPath();
        }
    }
    else {
        ROS_INFO(" [LT_CONTROLLER] New %ld paths received!", req.paths.size());
        for(int i = 0; i < req.paths.size(); i++) {
            
            // Ensure agent is already in the map
            if(agent_map_.find(req.agent_names[i]) == agent_map_.end()) {
                ROS_ERROR(" [LT_CONTROLLER] Agent %s not found in the map", &req.agent_names[i][0]);
                continue;
            }
            // Parse path and update agent map
            if(req.paths[i].poses.size() > 0) {
                if(req.goal_type.empty()){
                    // If goal type is not specified assume it to be a homing task
                    agent_map_[req.agent_names[i]].goal_type_ = mtg_messages::task_graph_getter::Response::FRONTIER;
                    agent_map_[req.agent_names[i]].goal_threshold_ = 0.4; // increase goal threshold for homing task
                    agent_map_[req.agent_names[i]].homing_ = true;
                }
                else
                    agent_map_[req.agent_names[i]].goal_type_ = req.goal_type[i];
                
                if(!req.goal_id.empty())
                    agent_map_[req.agent_names[i]].status.goal_id = req.goal_id[i];
                std::queue<geometry_msgs::PoseStamped> path_queue;
                for(int j = 0; j < req.paths[i].poses.size(); j++) {
                    path_queue.push(req.paths[i].poses[j]);
                }
                agent_map_[req.agent_names[i]].current_path_ = path_queue;
                
                
            }
            else {
                ROS_ERROR(" [LT_CONTROLLER] Empty path received for agent %s", &req.agent_names[i][0]);
            }

        }
    }

    res.success = true;
   
    return true;
}


void LazyTrafficController::RunController() {

    ROS_INFO("[LT_CONTROLLER] Opening the floodgates! ");

    ros::Rate r(1);
    while(ros::ok() && controller_active_) {
        ros::spinOnce();
        // Check if fleet status is outdated
        if(fleet_status_outdated_) {
            // TODO: Update agent map
            ROS_INFO(" [LT_CONTROLLER] Fleet status outdated, updating!!");
            processNewAgentStatus(getFleetStatusInfo());
            fleet_status_outdated_ = false;
        }
        
        r.sleep();
    }

}

void LazyTrafficController::computeVelocities(const ros::TimerEvent&) {
    
    std::lock_guard<std::mutex> lock(map_mutex);

    static int iter = 1;
    if(iter == (int)(velocity_calc_period_s/controller_period_s)) {

        // Measure execution time of function
        auto start = std::chrono::high_resolution_clock::now();

        // Update current poses of all agents from tf
        updateAgentPoses();
        iter = 1;

        // Calculate preferred velocities for all agents
        for(auto &agent : agent_map_) {
            agent.second.updatePreferredVelocity();
            agent.second.invokeRVO(agent_map_, occupancy_grid_map_);

            agent.second.sendVelocity(agent.second.rvo_velocity_);
            // Inform other subsystems of the controller status
            agent.second.publishStatus();
        }

        auto finish = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = finish - start;
        //ROS_INFO(" [LT_CONTROLLER] Time taken to compute velocities: %f s", elapsed.count());
    }
    else {
        iter++;
         for(auto &agent : agent_map_) {
            
            // Velocity is not sent if it is already zero
            agent.second.sendVelocity(agent.second.rvo_velocity_);
        }
    }

}

    

void LazyTrafficController::updateAgentPoses() {
    
    for(auto it = agent_map_.begin(); it != agent_map_.end(); it++) {
        // Get current pose of agent
        geometry_msgs::TransformStamped current_pose;
        try {
            current_pose = tf_buffer_.lookupTransform(map_frame_id_, it->second.robot_frame_id_, ros::Time(0));
        }
        catch (tf2::TransformException &ex) {
            ROS_WARN("%s",ex.what());
            //ros::Duration(1.0).sleep();
            continue;
        }
        // Calculate current velocity
        // Change in x
        double dx = current_pose.transform.translation.x - it->second.current_pose_.transform.translation.x;
        // Change in y
        double dy = current_pose.transform.translation.y - it->second.current_pose_.transform.translation.y;
        double dt = velocity_calc_period_s;
        assert(!AreSame(dt,0.0));
        // Calculate current velocity
        it->second.current_velocity_ = RVO::Vector2(dx/dt, dy/dt);

        // Update current pose
        it->second.current_pose_ = current_pose;
        // ROS_INFO(" [LT_CONTROLLER] Updated pose of %s %f %f", it->first.c_str(), 
        //                     agent_map_[it->first.c_str()].current_pose_.transform.translation.x, 
        //                     agent_map_[it->first.c_str()].current_pose_.transform.translation.y);
        // ROS_INFO(" [LT_CONTROLLER] Updated velocity of %s %f %f", it->first.c_str(), 
        //                         it->second.current_velocity_.x(), it->second.current_velocity_.y());
    }
}
void LazyTrafficController::processNewAgentStatus(std::set<string> new_fleet_info) {

    std::lock_guard<std::mutex> lock(map_mutex);
    // Get newly added agents
    std::set<string> additions;
    std::set<string> subtractions;
    std::set_difference(new_fleet_info.begin(), new_fleet_info.end(),
                            active_agents.begin(), active_agents.end(), std::inserter(additions, additions.begin()));

    std::set_difference(active_agents.begin(), active_agents.end(),
                            new_fleet_info.begin(), new_fleet_info.end(), std::inserter(subtractions, subtractions.begin()));
    if(!additions.empty()) {

        // Add them to our fleet info!
        active_agents.insert(additions.begin(),additions.end());
        initialiseAgentMap(additions);
    }
    if(!subtractions.empty()) {
        // Dont destroy the action server for now
        // Just stop it from executing
        for(auto s:subtractions) {
            // clear the agent's path
            // publish zero velocity 
            ROS_INFO(" [LT_CONTROLLER] Agent %s has been removed from the fleet", s.c_str());
            agent_map_[s].clearPath();
            agent_map_[s].stopAgent();
        }
    }
    
}
void LazyTrafficController::initialiseAgentMap(std::set<std::string> active_agents) {
    
    for (auto agent : active_agents) {
        ROS_INFO(" [LT_CONTROLLER] Initialising agent %s", agent.c_str());
        agent_map_[agent] = Agent(agent, nh_);
    }
}

std::set<std::string> LazyTrafficController::getFleetStatusInfo() {

    mtg_messages::agent_status srv;
    // wait for service to be available
    status_client_.waitForExistence();

    if (status_client_.call(srv)) {
        std::vector<std::string> agentsVec = srv.response.agents_active;
        std::set<std::string> agentsSet;

        for(auto agent:agentsVec)
            agentsSet.insert(agent);

        return agentsSet;
    }
    else
    {
        ROS_ERROR("[LT_CONTROLLER] Failed to call fleet info service");
        return active_agents;
    }
}