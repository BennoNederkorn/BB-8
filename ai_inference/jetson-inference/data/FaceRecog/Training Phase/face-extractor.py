from jetson_inference import detectNet
from jetson_utils import videoSource, videoOutput, Log, cudaToNumpy
import sys
import argparse
import os
import cv2
import numpy as np

savedir = "results/"

# parse the command line
parser = argparse.ArgumentParser(description="Locate objects in a live camera stream using an object detection DNN.", 
                                 formatter_class=argparse.RawTextHelpFormatter, 
                                 epilog=detectNet.Usage() + videoSource.Usage() + videoOutput.Usage() + Log.Usage())
parser.add_argument("input", type=str, default="", nargs='?', help="URI of the input stream")
parser.add_argument("--save-dir", type=str, default=savedir, help="directory to save cropped faces")
parser.add_argument("--network", type=str, default="facedetect", help="pre-trained model to load (see below for options)")
parser.add_argument("--overlay", type=str, default="none", help="detection overlay flags (e.g. --overlay=box,labels,conf)\nvalid combinations are:  'box', 'labels', 'conf', 'none'")
parser.add_argument("--threshold", type=float, default=0.85, help="minimum detection threshold to use") 


is_headless = ["--headless"] if sys.argv[0].find('console.py') != -1 else [""]

try:
    args = parser.parse_known_args()[0]
except:
    print("")
    parser.print_help()
    sys.exit(0)

# create video sources and outputs
input = videoSource(args.input, argv=sys.argv)

# load the object detection network
net = detectNet(args.network, threshold = 0.80)

# ensure save directory exists
if not os.path.exists(args.save_dir):
    os.makedirs(args.save_dir)

frame_count = 0

# process frames until EOS or the user exits
while True:
    # capture the next image
    img = input.Capture()

    if img is None: # timeout
        continue  
    
    # detect objects in the image (with overlay)
    detections = net.Detect(img, overlay=args.overlay)

    # print the detections
    print("detected {:d} objects in image".format(len(detections)))

    # convert image to numpy array
    np_img = cudaToNumpy(img)
    cv_img = cv2.cvtColor(np_img, cv2.COLOR_RGBA2BGR)

    for i, detection in enumerate(detections):
        print(detection)
        
        # get the bounding box coordinates
        left = int(detection.Left)
        top = int(detection.Top)
        right = int(detection.Right)
        bottom = int(detection.Bottom)

        # crop the face from the image
        face = cv_img[top:bottom, left:right]

        # save the cropped face
        cv2.imwrite(os.path.join(args.save_dir, f"face_{frame_count}_{i}.jpg"), face)

    frame_count += 1

    # print out performance info
    net.PrintProfilerTimes()

    # exit on input EOS
    if not input.IsStreaming():
        break
