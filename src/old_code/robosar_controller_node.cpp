// Created by Charvi and Indraneel on 4/04/22

#include "lg_action_server.hpp"

int main(int argc, char** argv)
{
  ros::init(argc, argv, "mtg_controller");
  LGControllerAction controller("agent1");
  ros::spin();

  return 0;
}