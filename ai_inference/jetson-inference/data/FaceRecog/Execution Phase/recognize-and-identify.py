# Complete code
# Step 1: Loading trained model.
# Step 2: Accessing Camera Stream.
# Step 3: Face detection via Facenet.
# Step 4: Face recognition via trained model.
# Note: Import Jetson lib. before the rest
# If not - the glDisplay window won't open

import jetson_inference
import jetson_utils
import torch
import torch.nn as nn
import torchvision.transforms as transforms
import torchvision.datasets as datasets
import torchvision.models as models
import cv2
from PIL import Image
import numpy as np
import time
import collections
import argparse
import os

"""
recognize-and-identify.py --headless webrtc://@:8554/input webrtc://@:8554/output
"""

#########################################
# **       Parser Config             ** #
#########################################

parser = argparse.ArgumentParser(description="Locate objects in a live camera stream using an object detection DNN.", 
                                 formatter_class=argparse.RawTextHelpFormatter)

parser.add_argument("--input", type=str, default="", nargs='?', help="URI of the input stream")
parser.add_argument("--output", type=str, default="", nargs='?', help="URI of the output stream")
parser.add_argument("--save-dir", type=str, default="", help="directory to save cropped faces")
parser.add_argument("--save-result", type=int, default=0, help="Would you like to save the image processing result?")

try:
    args = parser.parse_known_args()[0]
except:
    print("")
    parser.print_help()
    sys.exit(0)

#########################################
# ** Cameras and Display Config      ** #
#########################################

camera = jetson_utils.videoSource(args.input)
#display = jetson_utils.videoOutput("webrtc://@:8554/outputStream", argv=['--headless'])
display = jetson_utils.videoOutput(args.output)

#########################################
# ** Model functions and definitions ** #
#########################################

# Define transformations
transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225])
])

dataset = datasets.ImageFolder('../dataset/processed', transform=transform)

# Load the model
model = models.resnet18(pretrained=True)
num_features = model.fc.in_features
model.fc = nn.Linear(num_features, len(dataset.classes))

# Load the full checkpoint file
checkpoint = torch.load('../Training Phase/model_best.pth.tar')

# Load only the 'state_dict' part into the model
model.load_state_dict(checkpoint['state_dict'])

# Move the model to GPU if available
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
model.to(device)
model.eval()

# Preprocessing the image
def preprocess_image(image, transform):
    image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    image = Image.fromarray(image)
    image = transform(image).unsqueeze(0)
    return image


# Function to predict the person in the image
def predict(image, model, transform, device, threshold=0.35):
    image_tensor = preprocess_image(image, transform)
    image_tensor = image_tensor.to(device)
    with torch.no_grad():
        outputs = model(image_tensor)
        probabilities = nn.Softmax(dim=1)(outputs)
        max_prob, preds = torch.max(probabilities, 1)
        if round(max_prob.item(), 4) < threshold:
            return "Unknown"

    return dataset.classes[preds[0]]

#########################################
#           ** Main Loop **             #
#########################################	

print("Loading detection model...")
faceDetector = jetson_inference.detectNet("facedetect", threshold = 0.8)
print("Done.")

recognized_faces = collections.deque(maxlen=5)
pred_recognized_person = "Unknown"
predicted_person_label = "Unknown"

while display.IsStreaming():
	frame = camera.Capture()	
	face_detections = faceDetector.Detect(frame, overlay = "none")

	if frame is None:
		continue

	np_img = jetson_utils.cudaToNumpy(frame)
	cv_img = cv2.cvtColor(np_img, cv2.COLOR_RGBA2BGR)

	for i, detection in enumerate(face_detections):

		# Aquiring Bounding Box Coordinates
		left = int(detection.Left)
		top = int(detection.Top)
		right = int(detection.Right)
		bottom = int(detection.Bottom)
		
		# Cropping the face from the image
		extracted_face = cv_img[top:bottom, left:right]
		
		# Passing extracted face image to trained model
		model_threshold = 0.39
		pred_recognized_person = predict(extracted_face, model, transform, device, model_threshold)
		
		recognized_faces.append(pred_recognized_person)
		
		# Update the label only if was recognized in the last 5 frames
		if recognized_faces.count(pred_recognized_person) >= 5:
			predicted_person_label = pred_recognized_person		
		
		# Annotating image	
		cv2.rectangle(cv_img, (left, top), (right, bottom), (46, 139, 87), 2)
		#predicted_person_label
		cv2.putText(cv_img, pred_recognized_person, (left, bottom - 20), cv2.FONT_HERSHEY_SIMPLEX, 2, (46, 139, 87), 2)
		print("Identified: ", predicted_person_label)
	# Preparing image for rendering and display    
	bgr_img = jetson_utils.cudaFromNumpy(cv_img, isBGR=True)
	rgb_img = jetson_utils.cudaAllocMapped(width=bgr_img.width, height=bgr_img.height, format='rgb8')
	jetson_utils.cudaConvertColor(bgr_img, rgb_img)
	    
			
	
	if args.save_result == 1:
		cv2.imwrite(os.path.join(args.save_dir, f"result.jpg"), cv_img)
	else:
		# Displaying image
		display.Render(rgb_img)

	display.SetStatus("Facenet {:.0f} FPS | # faces :{:d}".format(faceDetector.GetNetworkFPS(), len(face_detections)))
