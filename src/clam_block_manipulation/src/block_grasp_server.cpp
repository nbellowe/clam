/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2013, University of Colorado, Boulder
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
 *   * Neither the name of the Univ of CO, Boulder nor the names of its
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
 *********************************************************************/

/* Author: Dave Coleman
   Desc:   Generates grasps for a cube
*/

// ROS
#include <ros/ros.h>
#include <tf/tf.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_listener.h>
#include <tf_conversions/tf_eigen.h>
#include <actionlib/server/simple_action_server.h>
#include <geometry_msgs/PoseArray.h>
#include <geometry_msgs/PoseStamped.h>
#include <manipulation_msgs/Grasp.h>
#include <Eigen/Core>
#include <Eigen/Geometry>

// Rviz
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>

// MoveIt
#include <moveit/planning_scene_monitor/planning_scene_monitor.h>
#include <moveit/robot_state/robot_state.h>
#include <moveit/robot_interaction/robot_interaction.h>

// C++
#include <boost/thread.hpp>
#include <math.h>
#define _USE_MATH_DEFINES

namespace clam_block_manipulation
{

static const std::string ROBOT_DESCRIPTION="robot_description";
static const std::string EE_LINK = "gripper_roll_link";
static const std::string EE_GROUP = "gripper_group";
static const std::string EE_NAME = "end_effector";
static const std::string GROUP_NAME = "arm";
static const std::string MARKER_NAME = "/end_effector_marker";
static const double RAD2DEG = 57.2957795;

// Class
class BlockGraspServer
{
private:
  // A shared node handle
  ros::NodeHandle nh_;

  // A ROS publisher
  ros::Publisher marker_pub_;

  // Action Servers and Clients
  /*  actionlib::SimpleActionServer<clam_msgs::PickPlaceAction> action_server_;

  // Action messages
  clam_msgs::PickPlaceFeedback     feedback_;
  clam_msgs::PickPlaceResult       result_;
  clam_msgs::PickPlaceGoalConstPtr goal_;
  */

  // MoveIt Components
  boost::shared_ptr<tf::TransformListener> tf_;
  planning_scene_monitor::PlanningSceneMonitorPtr planning_scene_monitor_;

  // Parameters from goal
  std::string base_link_;
  std::string block_link_;

  // TF Frame Transform stuff
  boost::thread tf_frame_thread_;
  tf::TransformBroadcaster tf_broadcaster_;
  tf::Transform transform_;
  bool transform_is_set_;
  boost::mutex transform_mutex_;

  // Grasp axis orientation
  enum grasp_axis_t {X_AXIS, Y_AXIS, Z_AXIS};
  enum grasp_direction_t {UP, DOWN};

  // End Effector Markers
  bool ee_marker_is_loaded_; // determines if we have loaded the marker or not
  visualization_msgs::MarkerArray marker_array_;
  tf::Pose tf_root_to_link_;
  geometry_msgs::Pose grasp_pose_to_eef_pose_;
  std::vector<geometry_msgs::Pose> marker_poses_;

public:

  // Constructor
  BlockGraspServer(const std::string name):
    nh_("~"),
    ee_marker_is_loaded_(false),
    transform_is_set_(false)
    //    action_server_(name, false),
  {
    base_link_ = "base_link";
    block_link_ = "block_link";

    // -----------------------------------------------------------------------------------------------
    // Rviz Visualizations
    marker_pub_ = nh_.advertise<visualization_msgs::Marker>(MARKER_NAME, 1);


    // ---------------------------------------------------------------------------------------------
    // Create planning scene monitor
    tf_.reset(new tf::TransformListener());
    planning_scene_monitor_.reset(new planning_scene_monitor::PlanningSceneMonitor(ROBOT_DESCRIPTION, tf_));

    // ---------------------------------------------------------------------------------------------
    // Check planning scene monitor
    if (planning_scene_monitor_->getPlanningScene() && planning_scene_monitor_->getPlanningScene()->isConfigured())
    {
      /*
        planning_scene_monitor_->startWorldGeometryMonitor();
        planning_scene_monitor_->startSceneMonitor("/move_group/monitored_planning_scene");
        planning_scene_monitor_->startStateMonitor("/joint_states", "/attached_collision_object");
      */
    }
    else
    {
      ROS_ERROR_STREAM_NAMED("pick_place","Planning scene not configured");
    }

    // ---------------------------------------------------------------------------------------------
    // Create a TF transform and start publishing in a seperate thread

    // Make the frame just be at the origin to start with
    boost::thread tf_frame_thread_(boost::bind(&BlockGraspServer::tfFrameThread, this));

    // ---------------------------------------------------------------------------------------------
    // Test pose
    double angle = M_PI / 1.5;

    geometry_msgs::Pose block_pose;
    block_pose.position.x = 0.4;
    block_pose.position.y = 0.0;
    block_pose.position.z = 0.02;

    Eigen::Quaterniond quat(Eigen::AngleAxis<double>(double(angle), Eigen::Vector3d::UnitZ()));
    block_pose.orientation.x = quat.x();
    block_pose.orientation.y = quat.y();
    block_pose.orientation.z = quat.z();
    block_pose.orientation.w = quat.w();

    // ---------------------------------------------------------------------------------------------
    // Create the grasps
    generateGrasps(block_pose);

    // ---------------------------------------------------------------------------------------------
    // Register the goal and preempt callbacks
    /*    action_server_.registerGoalCallback(boost::bind(&BlockGraspServer::goalCB, this));
          action_server_.registerPreemptCallback(boost::bind(&BlockGraspServer::preemptCB, this));
          action_server_.start();
    */

    // Announce state
    //    ROS_INFO_STREAM_NAMED("pick_place", "Server ready.");
    //    ROS_INFO_STREAM_NAMED("pick_place", "Waiting for pick command...");
  }

  // Destructor
  ~BlockGraspServer()
  {
    // join the thread back before exiting
    tf_frame_thread_.join();
  }

  // Action server sends goals here
  void goalCB()
  {
  }

  // Cancel the action
  void preemptCB()
  {
    ROS_INFO_STREAM_NAMED("pick_place","Preempted");
    //    action_server_.setPreempted();
  }

  // Publish our custom TF frame asyncronously
  void tfFrameThread()
  {
    ros::Rate rate(0.5);
    while (nh_.ok())
    {
      if( transform_is_set_ ) // do not start publishing until we have an intial transform loaded
      {
        // Do not access transform_ if it is being updated
        {
          boost::mutex::scoped_lock transform_lock(transform_mutex_);
          // Send Transform
          tf_broadcaster_.sendTransform(tf::StampedTransform(transform_, ros::Time::now(), base_link_, block_link_));
        }
      }

      rate.sleep();
    }
  }

  // Create all possible grasp positions for a block
  void generateGrasps( geometry_msgs::Pose& block_pose )
  {
    // List of possible block grasps
    std::vector<manipulation_msgs::Grasp> possible_grasps;

    // ---------------------------------------------------------------------------------------------
    // Update the published frame to now be at the block's location
    tf::Pose tf_block_pose;
    tf::poseMsgToTF(block_pose, tf_block_pose);

    // Do not access transform_ if it is being updated
    {
      boost::mutex::scoped_lock transform_lock(transform_mutex_);
      transform_ = tf_block_pose;
      transform_is_set_ = true; // do not start publishing until we have an intial transform loaded
    }

    // Calculate grasps in two axis
    generateAxisGrasps( possible_grasps, X_AXIS, DOWN );
    generateAxisGrasps( possible_grasps, X_AXIS, UP );
    generateAxisGrasps( possible_grasps, Y_AXIS, DOWN );
    generateAxisGrasps( possible_grasps, Y_AXIS, UP );

    // Visualize results
    visualizeGrasps(possible_grasps, block_pose);
  }

  // Create grasp positions in one axis
  bool generateAxisGrasps( std::vector<manipulation_msgs::Grasp>& possible_grasps, grasp_axis_t axis, grasp_direction_t direction )
  {

    // ---------------------------------------------------------------------------------------------
    // manipulation_msgs:Grasp parameters

    // Create re-usable approach motion
    manipulation_msgs::GripperTranslation gripper_approach;
    gripper_approach.direction.header.stamp = ros::Time::now();
    gripper_approach.direction.vector.x = 0;
    gripper_approach.direction.vector.y = 0;
    gripper_approach.direction.vector.z = -1; // Approach direction (negative z axis)
    gripper_approach.desired_distance = .050; // The distance the origin of a robot link needs to travel
    gripper_approach.min_distance = .025; // half of the desired? Untested.

    // Create re-usable retreat motion
    manipulation_msgs::GripperTranslation gripper_retreat;
    gripper_retreat.direction.header.stamp = ros::Time::now();
    gripper_retreat.direction.vector.x = 0;
    gripper_retreat.direction.vector.y = 0;
    gripper_retreat.direction.vector.z = 1; // Retreat direction (pos z axis)
    gripper_retreat.desired_distance = .050; // The distance the origin of a robot link needs to travel
    gripper_retreat.min_distance = .025; // half of the desired? Untested.

    // Create re-usable blank pose
    geometry_msgs::PoseStamped grasp_pose;
    grasp_pose.header.stamp = ros::Time::now();

    // ---------------------------------------------------------------------------------------------
    // Variables needed for calculations
    double radius = 0.15;
    double xb;
    double yb = 0.0; // stay in the y plane of the block
    double zb;
    double angle_resolution = 8.0;
    double theta1 = 0.0; // Where the point is located around the block
    double theta2 = 0.0; // UP 'direction'

    // Gripper direction (UP/DOWN) rotation. UP set by default
    if( direction == DOWN )
    {
      theta2 = M_PI;
    }

    // Create angles 180 degrees around the chosen axis at given resolution
    for(int i = 0; i <= angle_resolution; ++i)
    {
      ROS_DEBUG_STREAM_NAMED("grasp", "Generating grasp " << i );

      // Calculate grasp
      xb = radius*cos(theta1);
      zb = radius*sin(theta1);

      //ROS_DEBUG_STREAM_NAMED("grasp","Theta1: " << theta1*RAD2DEG);

      Eigen::Matrix3d rotation_matrix;
      switch(axis)
      {
      case X_AXIS:
        grasp_pose.pose.position.x = yb;
        grasp_pose.pose.position.y = xb;
        grasp_pose.pose.position.z = zb;

        rotation_matrix = Eigen::AngleAxisd(theta1, Eigen::Vector3d::UnitX())
          * Eigen::AngleAxisd(-0.5*M_PI, Eigen::Vector3d::UnitZ())
          * Eigen::AngleAxisd(theta2, Eigen::Vector3d::UnitX()); // Flip 'direction'

        break;
      case Y_AXIS:
        grasp_pose.pose.position.x = xb;
        grasp_pose.pose.position.y = yb;
        grasp_pose.pose.position.z = zb;

        rotation_matrix =
          Eigen::AngleAxisd(M_PI - theta1, Eigen::Vector3d::UnitY())
          *Eigen::AngleAxisd(theta2, Eigen::Vector3d::UnitX()); // Flip 'direction'

        break;
      case Z_AXIS:
        ROS_ERROR_STREAM_NAMED("grasp","Z Axis not implemented!");
        return false;

        break;
      }

      Eigen::Quaterniond quat(rotation_matrix);
      grasp_pose.pose.orientation.x = quat.x();
      grasp_pose.pose.orientation.y = quat.y();
      grasp_pose.pose.orientation.z = quat.z();
      grasp_pose.pose.orientation.w = quat.w();

      // Calculate the theta1 for next time
      theta1 += M_PI / angle_resolution;

      // ---------------------------------------------------------------------------------------------
      // Create a Grasp message
      manipulation_msgs::Grasp new_grasp;

      // A name for this grasp
      static int grasp_id = 0;
      new_grasp.id = "Grasp" + boost::lexical_cast<std::string>(grasp_id);

      // The internal posture of the hand for the pre-grasp
      // only positions are used
      //sensor_msgs/JointState pre_grasp_posture

      // The internal posture of the hand for the grasp
      // positions and efforts are used
      //sensor_msgs/JointState grasp_posture

      // The position of the end-effector for the grasp relative to a reference frame
      // (that is always specified elsewhere, not in this message)
      new_grasp.grasp_pose = grasp_pose;

      // The estimated probability of success for this grasp, or some other
      // measure of how "good" it is.
      new_grasp.grasp_quality = 1;

      // The approach motion
      new_grasp.approach = gripper_approach;

      // The retreat motion
      new_grasp.retreat = gripper_retreat;

      // the maximum contact force to use while grasping (<=0 to disable)
      new_grasp.max_contact_force = 0;

      // an optional list of obstacles that we have semantic information about
      // and that can be touched/pushed/moved in the course of grasping
      //string[] allowed_touch_objects

      // Add to vector
      possible_grasps.push_back(new_grasp);
    }


    ROS_INFO_STREAM_NAMED("grasp", "Generated " << possible_grasps.size() << " grasps sucessfully." );
    return true;
  }

  // Show all grasps in Rviz
  void visualizeGrasps(std::vector<manipulation_msgs::Grasp> possible_grasps,
                       geometry_msgs::Pose block_pose)
  {
    ROS_INFO_STREAM_NAMED("grasp","Visualizing all generating grasp poses on topic " << MARKER_NAME);
    ros::Rate rate(1.0);

    for(std::vector<manipulation_msgs::Grasp>::const_iterator grasp_it = possible_grasps.begin();
        grasp_it < possible_grasps.end(); ++grasp_it)
    {
      ROS_DEBUG_STREAM_NAMED("grasp","Visualizing grasp pose");
      geometry_msgs::Pose grasp_pose = grasp_it->grasp_pose.pose;
      publishSphere(grasp_pose);
      publishArrow(grasp_pose);
      publishEEMarkers(grasp_pose);
      publishBlock(block_pose, 0.04 - 0.001);
      rate.sleep();
    }
  }

  // *********************************************************************************************************
  // Helper Function
  // *********************************************************************************************************

  // Call this once at begining to load the robot marker
  void loadEEMarker()
  {
    // -----------------------------------------------------------------------------------------------
    // Get end effector group

    // Create color to use for EE markers
    std_msgs::ColorRGBA marker_color;
    marker_color.r = 1.0;
    marker_color.g = 1.0;
    marker_color.b = 1.0;
    marker_color.a = 0.5;

    // Get robot state
    robot_state::RobotState robot_state = planning_scene_monitor_->getPlanningScene()->getCurrentState();

    // Get link names that are in end effector
    const std::vector<std::string>
      &ee_link_names = robot_state.getJointStateGroup(EE_GROUP)->getJointModelGroup()->getLinkModelNames();
    ROS_DEBUG_STREAM_NAMED("grasp","Number of links in group " << EE_GROUP << ": " << ee_link_names.size());

    // Robot Interaction - finds the end effector associated with a planning group
    robot_interaction::RobotInteraction robot_interaction( planning_scene_monitor_->getRobotModel() );

    // Decide active end effectors
    robot_interaction.decideActiveEndEffectors(GROUP_NAME);

    // Get active EE
    std::vector<robot_interaction::RobotInteraction::EndEffector>
      active_eef = robot_interaction.getActiveEndEffectors();
    ROS_DEBUG_STREAM_NAMED("grasp","Number of active end effectors: " << active_eef.size());
    if( !active_eef.size() )
    {
      ROS_ERROR_STREAM_NAMED("grasp","No active end effectors found! Make sure kinematics.yaml is loaded in this node's namespace!");
      return;
    }

    // Just choose the first end effector TODO: better logic?
    robot_interaction::RobotInteraction::EndEffector eef = active_eef[0];

    // -----------------------------------------------------------------------------------------------
    // Get EE link markers for Rviz
    robot_state.getRobotMarkers(marker_array_, ee_link_names, marker_color, eef.eef_group, ros::Duration());
    ROS_DEBUG_STREAM_NAMED("grasp","Number of rviz markers in end effector: " << marker_array_.markers.size());

    // Change pose from Eigen to TF
    try{
      tf::poseEigenToTF(robot_state.getLinkState(eef.parent_link)->getGlobalLinkTransform(), tf_root_to_link_);
    }
    catch(...)
    {
      ROS_ERROR_STREAM_NAMED("grasp","Didn't find link state for " << eef.parent_link);
    }

    // Offset from gasp_pose to end effector
    static const double X_OFFSET = 0.0; //0.15;

    // Allow a transform from our pose to the end effector position
    grasp_pose_to_eef_pose_.position.x = X_OFFSET;
    grasp_pose_to_eef_pose_.position.y = 0;
    grasp_pose_to_eef_pose_.position.z = 0;
    grasp_pose_to_eef_pose_.orientation.x = 0;
    grasp_pose_to_eef_pose_.orientation.y = 0;
    grasp_pose_to_eef_pose_.orientation.z = 0;
    grasp_pose_to_eef_pose_.orientation.w = 1;


    // Copy original marker poses to a vector
    for (std::size_t i = 0 ; i < marker_array_.markers.size() ; ++i)
    {
      marker_poses_.push_back( marker_array_.markers[i].pose );
    }

    // Record that we have loaded the gripper
    ee_marker_is_loaded_ = true;
  }

  void publishEEMarkers(geometry_msgs::Pose &grasp_pose)
  {
    //ROS_INFO_STREAM("Mesh (" << grasp_pose.position.x << ","<< grasp_pose.position.y << ","<< grasp_pose.position.z << ")");

    // -----------------------------------------------------------------------------------------------
    // Make sure EE Marker is loaded
    if( !ee_marker_is_loaded_ )
    {
      ROS_INFO_STREAM_NAMED("grasp","Loading end effector rviz markers");
      loadEEMarker();
    }

    // -----------------------------------------------------------------------------------------------
    // Process each link of the end effector
    for (std::size_t i = 0 ; i < marker_array_.markers.size() ; ++i)
    {
      // Header
      marker_array_.markers[i].header.frame_id = block_link_;
      marker_array_.markers[i].header.stamp = ros::Time::now();

      // Options
      //marker_array_.markers[i].lifetime = ros::Duration(30.0);

      // Options for meshes
      if( marker_array_.markers[i].type == visualization_msgs::Marker::MESH_RESOURCE )
      {
        marker_array_.markers[i].mesh_use_embedded_materials = true;
      }

      // -----------------------------------------------------------------------------------------------
      // Do some math for the offset
      // grasp_pose             - our generated grasp
      // markers[i].pose        - an ee link's pose relative to the whole end effector
      // grasp_pose_to_eef_pose_ - the offset from the grasp pose to eef_pose - probably nothing
      tf::Pose tf_root_to_marker;
      tf::Pose tf_root_to_mesh;
      tf::Pose tf_pose_to_eef;

      // Simple conversion from geometry_msgs::Pose to tf::Pose
      tf::poseMsgToTF(grasp_pose, tf_root_to_marker);
      tf::poseMsgToTF(marker_poses_[i], tf_root_to_mesh);
      tf::poseMsgToTF(grasp_pose_to_eef_pose_, tf_pose_to_eef);

      // Conversions
      tf::Pose tf_eef_to_mesh = tf_root_to_link_.inverse() * tf_root_to_mesh;
      tf::Pose tf_marker_to_mesh = tf_pose_to_eef * tf_eef_to_mesh;
      tf::Pose tf_root_to_mesh_new = tf_root_to_marker * tf_marker_to_mesh;
      tf::poseTFToMsg(tf_root_to_mesh_new, marker_array_.markers[i].pose);
      // -----------------------------------------------------------------------------------------------

      //ROS_INFO_STREAM("Marker " << i << ":\n" << marker_array_.markers[i]);

      marker_pub_.publish( marker_array_.markers[i] );
      ros::Duration(0.05).sleep();  // Sleep to prevent markers from being 'skipped' in rviz
    }

  }

  void publishSphere(geometry_msgs::Pose &pose)
  {
    //ROS_INFO_STREAM("Sphere (" << pose.position.x << ","<< pose.position.y << ","<< pose.position.z << ")");

    visualization_msgs::Marker marker;
    // Set the frame ID and timestamp.  See the TF tutorials for information on these.
    marker.header.frame_id = block_link_;
    marker.header.stamp = ros::Time::now();

    // Set the namespace and id for this marker.  This serves to create a unique ID
    marker.ns = "Sphere";

    // Set the marker type.
    marker.type = visualization_msgs::Marker::SPHERE_LIST;

    // Set the marker action.  Options are ADD and DELETE
    marker.action = visualization_msgs::Marker::ADD;

    static int id = 0;
    marker.id = ++id;

    marker.pose.position.x = 0;
    marker.pose.position.y = 0;
    marker.pose.position.z = 0;

    marker.pose.orientation.x = 0.0;
    marker.pose.orientation.y = 0.0;
    marker.pose.orientation.z = 0.0;
    marker.pose.orientation.w = 1.0;

    marker.scale.x = 0.01;
    marker.scale.y = 0.01;
    marker.scale.z = 0.01;

    marker.color.r = 0.0;
    marker.color.g = 0.0;
    marker.color.b = 1.0;
    marker.color.a = 1.0;

    //marker.lifetime = ros::Duration(30.0);

    // Make line color
    std_msgs::ColorRGBA color;
    color.r = 1.0;
    color.g = 0.1;
    color.b = 0.1;
    color.a = 1.0;

    // Point
    geometry_msgs::Point point_a = pose.position;

    // Add the point pair to the line message
    marker.points.push_back( point_a );
    marker.colors.push_back( color );


    marker_pub_.publish( marker );
    ros::Duration(0.05).sleep(); // Sleep to prevent markers from being 'skipped' in rviz
  }

  void publishArrow(geometry_msgs::Pose &pose)
  {
    //ROS_INFO_STREAM("Arrow (" << pose.position.x << ","<< pose.position.y << ","<< pose.position.z << ")");

    visualization_msgs::Marker marker;
    // Set the frame ID and timestamp.  See the TF tutorials for information on these.
    marker.header.frame_id = block_link_;
    marker.header.stamp = ros::Time::now();

    // Set the namespace and id for this marker.  This serves to create a unique ID
    marker.ns = "Arrow";

    // Set the marker type.
    marker.type = visualization_msgs::Marker::ARROW;

    // Set the marker action.  Options are ADD and DELETE
    marker.action = visualization_msgs::Marker::ADD;

    static int id = 0;
    marker.id = ++id;

    marker.pose = pose;

    marker.scale.x = 0.05; //0.025; // arrow width - but i would call this the length
    marker.scale.y = 0.005; // arrow height
    marker.scale.z = 0.005; // arrow length

    marker.color.r = 0.0;
    marker.color.g = 0.0;
    marker.color.b = 1.0;
    marker.color.a = 1.0;

    //marker.lifetime = ros::Duration(30.0);

    marker_pub_.publish( marker );
    ros::Duration(0.05).sleep(); // Sleep to prevent markers from being 'skipped' in rviz
  }

  void publishBlock(geometry_msgs::Pose &pose, const double& block_size)
  {
    visualization_msgs::Marker marker;
    // Set the frame ID and timestamp.  See the TF tutorials for information on these.
    marker.header.frame_id = base_link_;
    marker.header.stamp = ros::Time::now();

    // Set the namespace and id for this marker.  This serves to create a unique ID
    marker.ns = "Block";
    marker.id = 1;

    // Set the marker action.  Options are ADD and DELETE
    marker.action = visualization_msgs::Marker::ADD;

    // Set the pose
    marker.pose = pose;

    // Set the marker type.
    marker.type = visualization_msgs::Marker::CUBE;

    // Set marker size
    marker.scale.x = block_size;
    marker.scale.y = block_size;
    marker.scale.z = block_size;

    // Set marker color
    marker.color.r = 1.0;
    marker.color.g = 0.0;
    marker.color.b = 0.0;
    marker.color.a = 0.5;

    marker_pub_.publish( marker );
    ros::Duration(0.05).sleep(); // Sleep to prevent markers from being 'skipped' in rviz
  }

}; // end of class

} // namespace

int main(int argc, char** argv)
{
  ros::init(argc, argv, "block_grasp_server");

  clam_block_manipulation::BlockGraspServer server("grap_gen");

  // Allow the action server to recieve and send ros messages
  //  ros::spin(); // keep the action server alive
  ros::AsyncSpinner spinner(4); // Use 4 threads
  spinner.start();
  ros::waitForShutdown();

  return 0;
}

