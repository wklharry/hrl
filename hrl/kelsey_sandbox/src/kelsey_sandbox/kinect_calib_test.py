#!/usr/bin/python

import copy
import pygame
import pygame.image 
import numpy as np

import roslib
roslib.load_manifest('UI_segment_object')
roslib.load_manifest('camera_calibration')
roslib.load_manifest('pixel_2_3d')
roslib.load_manifest('hrl_generic_arms')

import cv

import rospy
from sensor_msgs.msg import Image
from cv_bridge import CvBridge, CvBridgeError
import tf

from camera_calibration.calibrator import ChessboardInfo, Calibrator
from pixel_2_3d.srv import Pixel23d
from hrl_generic_arms.pose_converter import PoseConverter

class ImageListener:
    def __init__(self, topic):
        self.bridge = CvBridge()
        self.image_sub = rospy.Subscriber(topic, Image, self.callback)
        self.msg_img = None

    def callback(self, data):
        try:
            self.msg_img = data
        except CvBridgeError, e:
            print e
            return

    def get_cv_img(self):
        if self.msg_img is None:
            return None
        cv_img = self.bridge.imgmsg_to_cv(self.msg_img, "rgb8")
        return cv_img

    def get_pg_img(self, cv_img):
        return pygame.image.frombuffer(cv_img.tostring(), 
                                       cv.GetSize(cv_img), "RGB")
    
def umeyama_method(A, B):
    ux = np.mean(A, 1)
    uy = np.mean(B, 1)
    A_demean = A - ux
    B_demean = B - uy
    U, D, VT = np.linalg.svd(A_demean * B_demean.T)
    R = VT.T * U.T
    t = uy - R * ux
    return t, R

def main():
    gray = (100,100,100)
    corner_len = 5
    chessboard = ChessboardInfo()
    chessboard.n_cols = 6
    chessboard.n_rows = 7
    chessboard.dim = 0.02273
    cboard_frame = "kinect_cb_corner"
    kinect_tracker_frame = "kinect"

    rospy.init_node("kinect_calib_test")
    img_list = ImageListener("/kinect_head/rgb/image_color")
    pix3d_srv = rospy.ServiceProxy("/pixel_2_3d", Pixel23d)
    tf_list = tf.TransformListener()
    pygame.init()
    clock = pygame.time.Clock()
    screen = pygame.display.set_mode((640, 480))
    calib = Calibrator([chessboard])
    done = False
    corner_list = np.ones((2, corner_len)) * -1000.0
    corner_i = 0
    saved_corners_2d, saved_corners_3d, cb_locs = [], [], []
    while not rospy.is_shutdown():
        try:
            cb_pos, cb_quat = tf_list.lookupTransform(kinect_tracker_frame, 
                                                      cboard_frame, 
                                                      rospy.Time())
        except:
            rospy.sleep(0.001)
            continue
        cv_img = img_list.get_cv_img()
        if cv_img is not None:
            has_corners, corners, chess = calib.get_corners(cv_img)
            for corner2d in saved_corners_2d:
                cv.Circle(cv_img, corner2d, 4, [0, 255, 255])
            if has_corners:
                corner_i += 1
                corner = corners[0]
                for event in pygame.event.get():
                    if event.type == pygame.KEYDOWN:
                        print event.dict['key'], pygame.K_d
                        if event.dict['key'] == pygame.K_d:
                            done = True
                        if event.dict['key'] == pygame.K_q:
                            return
                if done:
                    break
                corner_list[:, corner_i % corner_len] = corner
                if np.linalg.norm(np.var(corner_list, 1)) < 1.0:
                    corner_avg = np.mean(corner_list, 1)
                    corner_avg_tuple = tuple(corner_avg.round().astype(int).tolist())
                    cv.Circle(cv_img, corner_avg_tuple, 4, [0, 255, 0])
                    pix3d_resp = pix3d_srv(*corner_avg_tuple)
                    if pix3d_resp.error_flag == pix3d_resp.SUCCESS:
                        corner_3d_tuple = (pix3d_resp.pixel3d.pose.position.x,
                                           pix3d_resp.pixel3d.pose.position.y,
                                           pix3d_resp.pixel3d.pose.position.z)
                        if len(saved_corners_3d) == 0:
			    cb_locs.append(cb_pos)
                            saved_corners_2d.append(corner_avg_tuple)
                            saved_corners_3d.append(corner_3d_tuple)
                        else:
                            diff_arr = np.array(np.mat(saved_corners_3d) - np.mat(corner_3d_tuple))
                            if np.min(np.sqrt(np.sum(diff_arr ** 2, 1))) >= 0.1:
                                cb_locs.append(cb_pos)
                                saved_corners_2d.append(corner_avg_tuple)
                                saved_corners_3d.append(corner_3d_tuple)
                else:
                    cv.Circle(cv_img, corner, 4, [255, 0, 0])
            else:
                corner_list = np.ones((2, corner_len)) * -1000.0
        if cv_img is None:
            screen.fill(gray)
        else:
            screen.blit(img_list.get_pg_img(cv_img), (0, 0))
        pygame.display.flip()
        rospy.sleep(0.001)
    A = np.mat(saved_corners_3d).T
    B = np.mat(cb_locs).T
    print A, B
    t, R = umeyama_method(A, B)
    print A, B, R, t
    print "-" * 60
    print "Transformation Parameters:"
    pos, quat = PoseConverter.to_pos_quat(t, R)
    print '%f %f %f %f %f %f %f /%s /%s 100' % tuple(pos + quat + 
                                                     [cboard_frame, kinect_tracker_frame])
    
if __name__ == '__main__':
    main()