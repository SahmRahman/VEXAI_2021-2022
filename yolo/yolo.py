
import pyrealsense2 as rs
import numpy as np
import cv2

import torch
from utils.experimental import attempt_load
from utils.general import non_max_suppression, scale_coords
from utils.plots import Annotator, colors
from utils.serial_test import Coms
from utils.data import return_data, determine_color, determine_depth, degree
from utils.camera import CameraSwitcher
import time
import os

PATH = os.getcwd() + "/models/best.pt"
do_depth_ring = False

import argparse

parser = argparse.ArgumentParser()
parser.add_argument("--display", metavar="display", type=int, default=1)
args = parser.parse_args()
    
        
model = attempt_load(PATH)
device = torch.device("cuda" if torch.cuda.is_available() else 'cpu')
model.to(device)
names = model.module.names if hasattr(model, 'module') else model.names

camera = CameraSwitcher(['f1181409', '048522072643'])
camera.start()

comm = Coms()
try:
    comm.open()
except:
    pass

try:
    while True:
        frames = camera.pipeline.wait_for_frames()
        depth_frame = frames.get_depth_frame()
        color_frame = frames.get_color_frame()
        if not depth_frame or not color_frame:
            continue

        # Convert images to numpy arrays
        depth_image = np.asanyarray(depth_frame.get_data())
        color_image = np.asanyarray(color_frame.get_data())

        # If depth and color resolutions are different, resize color image to match depth image for display
        if depth_image.shape != color_image.shape:
            color_image = cv2.resize(color_image, dsize=(depth_image.shape[1], depth_image.shape[0]), interpolation=cv2.INTER_AREA)

        color_image_t = torch.moveaxis(torch.cuda.FloatTensor(color_image), 2, 0)[None] / 255.0

        pred = model(color_image_t)[0]
        pred = non_max_suppression(pred, .5)[0]
        
        pred[:,:4] = scale_coords(color_image_t.shape[2:], pred[:, :4], color_image.shape).round()

        for i, det in enumerate(pred):
            if(det[5] == 0): # COLOR
                pred[i, 5] = determine_color(det, color_image)
            else:
                pred[i, 5] = 3
            pred[i, 4] = determine_depth(det, depth_image) * depth_frame.get_units()

        if int(pred.shape[0]) > 0:
            det = return_data(pred, find="close", colors=[-1, 0, 1])

            data = [-1,-1]
            if len(det) > 0:
                turn_angle = degree(det)
                if not turn_angle == None:
                    data = [float(det[4]), float(turn_angle)]
        
        try:
            print("Depth: {}, Turn angle: {}".format(data[0], data[1]))
            comm.send("mogo", data)
            if (comm.read("stop")): 
                camera.switch_camera()
                while (not comm.read("continue")): 
                    pass
        except:
            try:
                comm.open()
            except Exception as e:
                print(e)
        cv2.waitKey(1)

finally:
    camera.pipeline.stop()

