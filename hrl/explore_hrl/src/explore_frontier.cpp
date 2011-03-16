/*********************************************************************
 *
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2008, Robert Bosch LLC.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Robert Bosch nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *********************************************************************/

/*
 *  Created on: Apr 6, 2009
 *      Author: duhadway
 */

#include <explore_hrl/explore_frontier.h>
#include <explore_hrl/Line.h>
#include <explore_hrl/Vector.h>
#include <math.h>

using namespace visualization_msgs;
using namespace geom;
using namespace costmap_2d;

namespace explore {

ExploreFrontier::ExploreFrontier() :
  map_(),
  lastMarkerCount_(0),
  planner_(NULL),
  frontiers_()
{
}

ExploreFrontier::~ExploreFrontier()
{

}

bool ExploreFrontier::getFrontiers(Costmap2DROS& costmap, std::vector<geometry_msgs::Pose>& frontiers)
{
  findFrontiers(costmap);
  if (frontiers_.size() == 0)
    return false;

  frontiers.clear();
  for (uint i=0; i < frontiers_.size(); i++) {
    geometry_msgs::Pose frontier;
    frontiers.push_back(frontiers_[i].pose);
  }

  return (frontiers.size() > 0);
}

float ExploreFrontier::getFrontierCost(const Frontier& frontier) {
  //planner_->computePotential( frontier.pose.position );
  ROS_DEBUG("cost of frontier: %f, at position: (%.2f, %.2f, %.2f)\n", planner_->getPointPotential(frontier.pose.position), 
      frontier.pose.position.x, frontier.pose.position.y, tf::getYaw(frontier.pose.orientation));
  if (planner_ != NULL)
    return planner_->getPointPotential(frontier.pose.position);
  else
    return 1.0;
}

double ExploreFrontier::getOrientationChange(const Frontier& frontier, const tf::Stamped<tf::Pose>& robot_pose){
    double robot_yaw = tf::getYaw(robot_pose.getRotation());
    double robot_atan2 = atan2(robot_pose.getOrigin().y() + sin(robot_yaw), robot_pose.getOrigin().x() + cos(robot_yaw));
    double frontier_atan2 = atan2(frontier.pose.position.x, frontier.pose.position.y);
    double orientation_change = robot_atan2 - frontier_atan2;
    ROS_DEBUG("Orientation change: %.3f", orientation_change * (180.0 / M_PI));
    return orientation_change;
}

float ExploreFrontier::getFrontierGain(const Frontier& frontier, double map_resolution) {
  return frontier.size * map_resolution;
}

bool ExploreFrontier::getExplorationGoals(Costmap2DROS& costmap, geometry_msgs::Point start, navfn::NavfnROS* planner, std::vector<geometry_msgs::Pose>& goals, double potential_scale, double orientation_scale, double gain_scale)
{
  planner->computePotential(start);
  planner_ = planner;
  costmapResolution_ = costmap.getResolution();

  findFrontiers(costmap);
  if (frontiers_.size() == 0){
    return false;
  }


  //we'll make sure that we set goals for the frontier at least the circumscribed
  //radius away from unknown space
  double goal_stepback = costmap.getCircumscribedRadius();

  WeightedFrontier goal;
  std::vector<WeightedFrontier> weightedFrontiers;
  weightedFrontiers.reserve(frontiers_.size());
  for (uint i=0; i < frontiers_.size(); i++) {
    Frontier& frontier = frontiers_[i];
    WeightedFrontier weightedFrontier;
    weightedFrontier.frontier = frontier;

    tf::Quaternion bt;
    tf::quaternionMsgToTF(frontier.pose.orientation, bt);

    double distance = 0.0;

    //we'll walk back along the potential function generated by the planner from the frontier point at least the inscribed radius
    while(distance < goal_stepback){
      geometry_msgs::Point check_point = frontier.pose.position;
      check_point.x -= costmapResolution_;
      check_point.y -= costmapResolution_;
      double best_potential = planner->getPointPotential(check_point);
      geometry_msgs::Point best_point = check_point;

      //look at the neighbors of the current point to find the one with the lowest potential
      for(unsigned int i = 0; i < 3; ++i){
        for(unsigned int j = 0; j < 3; ++j){
          check_point.x += costmapResolution_;
          double potential = planner->getPointPotential(check_point);

          //check if the potential is better, make sure that we don't check the frontier point itself
          if(potential < best_potential && !(i == 1 && j == 1)){
            best_potential = potential;
            best_point = check_point;
          }
        }
        check_point.x = frontier.pose.position.x - costmapResolution_;
        check_point.y += costmapResolution_;
      }

      //update the frontier position and add to the distance traveled
      frontier.pose.position.x = best_point.x;
      frontier.pose.position.y = best_point.y;
      distance += costmapResolution_;
    }

    //create a line using the vector we stored in the frontier
    //Line l(Point(frontier.pose.position.x, frontier.pose.position.y, frontier.pose.position.z), bt.getAngle());

    //step back the inscribed radius along that line to find a goal point
    //Point t = l.eval(goal_stepback);

    weightedFrontier.frontier.pose.position.x = frontier.pose.position.x;
    weightedFrontier.frontier.pose.position.y = frontier.pose.position.y;
    weightedFrontier.frontier.pose.position.z = frontier.pose.position.z;

    tf::Stamped<tf::Pose> robot_pose;
    costmap.getRobotPose(robot_pose);

    //compute the cost of the frontier
    weightedFrontier.cost = potential_scale * getFrontierCost(weightedFrontier.frontier) + orientation_scale * getOrientationChange(weightedFrontier.frontier, robot_pose) - gain_scale * getFrontierGain(weightedFrontier.frontier, costmapResolution_);

    weightedFrontiers.push_back(weightedFrontier);
  }

  goals.clear();
  goals.reserve(weightedFrontiers.size());
  std::sort(weightedFrontiers.begin(), weightedFrontiers.end());
  for (uint i = 0; i < weightedFrontiers.size(); i++) {
    goals.push_back(weightedFrontiers[i].frontier.pose);
  }
  return (goals.size() > 0);
}

void ExploreFrontier::findFrontiers(Costmap2DROS& costmap_) {
  frontiers_.clear();

  Costmap2D costmap;
  costmap_.getCostmapCopy(costmap);

  int idx;
  int w = costmap.getSizeInCellsX();
  int h = costmap.getSizeInCellsY();
  int size = (w * h);
  int pts = 0;

  map_.info.width = w;
  map_.info.height = h;
  map_.set_data_size(size);
  map_.info.resolution = costmap.getResolution();
  map_.info.origin.position.x = costmap.getOriginX();
  map_.info.origin.position.y = costmap.getOriginY();


  // Find all frontiers (open cells next to unknown cells).
  const unsigned char* map = costmap.getCharMap();
  for (idx = 0; idx < size; idx++) {
    //get the world point for the index
    unsigned int mx, my;
    costmap.indexToCells(idx, mx, my);
    geometry_msgs::Point p;
    costmap.mapToWorld(mx, my, p.x, p.y);

    //check if the point has valid potential and is next to unknown space
    //bool valid_point = planner_->validPointPotential(p);
    //bool valid_point = planner_->computePotential(p) && planner_->validPointPotential(p);
    //bool valid_point = (map[idx] < LETHAL_OBSTACLE);
    //bool valid_point = (map[idx] < INSCRIBED_INFLATED_OBSTACLE );
    bool valid_point = (map[idx] == FREE_SPACE);

    if ((valid_point && map) &&
        (((idx+1 < size) && (map[idx+1] == NO_INFORMATION)) ||
         ((idx-1 >= 0) && (map[idx-1] == NO_INFORMATION)) ||
         ((idx+w < size) && (map[idx+w] == NO_INFORMATION)) ||
         ((idx-w >= 0) && (map[idx-w] == NO_INFORMATION))))
    {
      map_.data[idx] = -128;
      //ROS_INFO("Candidate Point %d.", pts++ );
    } else {
      map_.data[idx] = -127;
    }
  }

  // Clean up frontiers detected on separate rows of the map
  //   I don't know what this means -- Travis.
  idx = map_.info.height - 1;
  for (unsigned int y=0; y < map_.info.width; y++) {
    map_.data[idx] = -127;
    idx += map_.info.height;
  }

  // Group adjoining map_ pixels
  int segment_id = 127;
  std::vector< std::vector<FrontierPoint> > segments;
  for (int i = 0; i < size; i++) {
    if (map_.data[i] == -128) {
      std::vector<int> neighbors, candidates;
      std::vector<FrontierPoint> segment;
      neighbors.push_back(i);

      int points_in_seg = 0;
      unsigned int mx, my;
      geometry_msgs::Point p_init, p;

      costmap.indexToCells(i, mx, my);
      costmap.mapToWorld(mx, my, p_init.x, p_init.y);

      // claim all neighbors
      while (neighbors.size() > 0) {
        int idx = neighbors.back();
        neighbors.pop_back();

        btVector3 tot(0,0,0);
        int c = 0;
        if ((idx+1 < size) && (map[idx+1] == NO_INFORMATION)) {
          tot += btVector3(1,0,0);
          c++;
        }
        if ((idx-1 >= 0) && (map[idx-1] == NO_INFORMATION)) {
          tot += btVector3(-1,0,0);
          c++;
        }
        if ((idx+w < size) && (map[idx+w] == NO_INFORMATION)) {
          tot += btVector3(0,1,0);
          c++;
        }
        if ((idx-w >= 0) && (map[idx-w] == NO_INFORMATION)) {
          tot += btVector3(0,-1,0);
          c++;
        }
        assert(c > 0);

	// Only adjust the map and add idx to segment when its near enough to start point.
	//    This should prevent greedy approach where all cells belong to one frontier!

	costmap.indexToCells(idx, mx, my);
	costmap.mapToWorld(mx, my, p.x, p.y);
	float dist;
	dist = sqrt( pow( p.x - p_init.x, 2 ) + pow( p.y - p_init.y, 2 ));

	if ( dist <= 0.8 ){
	  map_.data[idx] = segment_id;  // This used to be up before "assert" block above.
	  points_in_seg += 1;
	  //ROS_INFO( "Dist to start cell: %3.2f", dist );

	  segment.push_back(FrontierPoint(idx, tot / c));

	  // consider 8 neighborhood
	  if (((idx-1)>0) && (map_.data[idx-1] == -128))
	    neighbors.push_back(idx-1);
	  if (((idx+1)<size) && (map_.data[idx+1] == -128))
	    neighbors.push_back(idx+1);
	  if (((idx-map_.info.width)>0) && (map_.data[idx-map_.info.width] == -128))
	    neighbors.push_back(idx-map_.info.width);
	  if (((idx-map_.info.width+1)>0) && (map_.data[idx-map_.info.width+1] == -128))
	    neighbors.push_back(idx-map_.info.width+1);
	  if (((idx-map_.info.width-1)>0) && (map_.data[idx-map_.info.width-1] == -128))
	    neighbors.push_back(idx-map_.info.width-1);
	  if (((idx+(int)map_.info.width)<size) && (map_.data[idx+map_.info.width] == -128))
	    neighbors.push_back(idx+map_.info.width);
	  if (((idx+(int)map_.info.width+1)<size) && (map_.data[idx+map_.info.width+1] == -128))
	    neighbors.push_back(idx+map_.info.width+1);
	  if (((idx+(int)map_.info.width-1)<size) && (map_.data[idx+map_.info.width-1] == -128))
	    neighbors.push_back(idx+map_.info.width-1);
	}
      }

      segments.push_back(segment);
      ROS_INFO( "Segment %d has %d costmap cells", segment_id, points_in_seg );
      segment_id--;
      if (segment_id < -127)
        break;
    }
  }

  int num_segments = 127 - segment_id;
  ROS_INFO("Segments: %d", num_segments );
  if (num_segments <= 0)
    return;

  for (unsigned int i=0; i < segments.size(); i++) {
    Frontier frontier;
    std::vector<FrontierPoint>& segment = segments[i];
    uint size = segment.size();

    //we want to make sure that the frontier is big enough for the robot to fit through
    //  This seems like a goofy heuristic rather than fact.  What happens if we don't do this...?
    if (size * costmap.getResolution() < costmap.getInscribedRadius()){
      ROS_INFO( "Discarding segment... too small?" );
      //continue;
    }

    float x = 0, y = 0;
    btVector3 d(0,0,0);

    for (uint j=0; j<size; j++) {
      d += segment[j].d;
      int idx = segment[j].idx;
      //x += (idx % map_.info.width);
      //y += (idx / map_.info.width);
      x = (idx % map_.info.width);
      y = (idx / map_.info.width);
    }
    d = d / size;
    //frontier.pose.position.x = map_.info.origin.position.x + map_.info.resolution * (x / size);
    //frontier.pose.position.y = map_.info.origin.position.y + map_.info.resolution * (y / size);
    frontier.pose.position.x = map_.info.origin.position.x + map_.info.resolution * (x);
    frontier.pose.position.y = map_.info.origin.position.y + map_.info.resolution * (y);
    frontier.pose.position.z = 0.0;

    frontier.pose.orientation = tf::createQuaternionMsgFromYaw(btAtan2(d.y(), d.x()));
    frontier.size = size;

    geometry_msgs::Point p;
    p.x = map_.info.origin.position.x + map_.info.resolution * (x);  // frontier.pose.position
    p.y = map_.info.origin.position.y + map_.info.resolution * (y);
    if (!planner_->validPointPotential(p)){
      ROS_INFO( "Discarding segment... can't reach?" );
      //continue;
    }

    frontiers_.push_back(frontier);
  }

}

void ExploreFrontier::getVisualizationMarkers(std::vector<Marker>& markers)
{
  Marker m;
  m.header.frame_id = "map";
  m.header.stamp = ros::Time::now();
  m.id = 0;
  m.ns = "frontiers";
  m.type = Marker::ARROW;
  m.pose.position.x = 0.0;
  m.pose.position.y = 0.0;
  m.pose.position.z = 0.0;
  m.scale.x = 1.0;
  m.scale.y = 1.0;
  m.scale.z = 1.0;
  m.color.r = 0;
  m.color.g = 0;
  m.color.b = 255;
  m.color.a = 255;
  m.lifetime = ros::Duration(0);

  m.action = Marker::ADD;
  uint id = 0;
  for (uint i=0; i<frontiers_.size(); i++) {
    Frontier frontier = frontiers_[i];
    m.id = id;
    m.pose = frontier.pose;
    markers.push_back(Marker(m));
    id++;
  }

  ROS_INFO("Number of frontiers being published: %d. Removed: %d. Last count: %d, id: %d", (unsigned int)frontiers_.size(), lastMarkerCount_ - id, lastMarkerCount_, id);

  m.action = Marker::DELETE;
  for (; id < lastMarkerCount_; id++) {
    m.id = id;
    markers.push_back(Marker(m));
  }

  lastMarkerCount_ = markers.size();
}

}
