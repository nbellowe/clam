#!/usr/bin/env python
import copy
import sys, rospy
from moveit_commander import RobotCommander, PlanningSceneInterface, roscpp_initialize, roscpp_shutdown
from geometry_msgs.msg import PoseStamped
class ClamPickPlace:
	robot = RobotCommander()
	def __init__(self):
		self.scene = PlanningSceneInterface()
		self.size = (0.15, 0.1, 0.3)
	def add_box(self, stamp=None):
		if stamp is None:
			stamp = PoseStamped()
			stamp.pose.position.x = 0.6
			stamp.pose.position.y = -0.7
			stamp.pose.position.z = 0.5
		self.scene.add_box("box", stamp, self.size)
		self.last_sent = stamp
	def pick(self):
		rospy.sleep(2) # bug why sleep?
		robot.arm.pick("box")
	def place(self, loc):
		robot.arm.place("box", loc)
	def demo_stack(self, loc=None):
	#	scene.clear?
		if loc is None:
			loc = PoseStamped()
			loc.pose.position.x = 0.6
			loc.pose.position.y = 0.5
			loc.pose.position.z = 0.8
		self.add_box(loc)
		second_loc = copy.deepcopy(loc)		
		second_loc.pose.position.z += .5


if __name__ == '__main__':
	roscpp_initialize(sys.argv)
	rospy.init_node('pick_place', anonymous=True)
	a = ClamPickPlace()
	a.demo_stack()
	rospy.spin()
	roscpp_shutdown()
