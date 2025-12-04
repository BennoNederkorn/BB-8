import jetson_inference
import jetson_utils
import torch
import torch.nn as nn
import torch.nn.functional as F
import torchvision.transforms as transforms
import torchvision.datasets as datasets
import torchvision.models as models
import cv2
from PIL import Image
import numpy as np
import collections
import argparse
import time

#########################################
# **       Parser Config             ** #
#########################################

parser = argparse.ArgumentParser(description="Locate objects in a live camera stream using an object detection DNN.", 
                                 formatter_class=argparse.RawTextHelpFormatter)
parser.add_argument("--input", type=str, default="/dev/video0", nargs='?', help="URI of the input stream")
parser.add_argument("--save-dir", type=str, default="", help="directory to save cropped faces")
parser.add_argument("--save-result", type=int, default=0, help="Would you like to save the image processing result?")

args = parser.parse_args()

#########################################
# ** Model functions and definitions ** #
#########################################

# Define transformations for face images
transform = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225])
])

dataset = datasets.ImageFolder('/my-detection/Project/training-results', transform=transform)

# Preprocess the image for the model
def preprocess_image(image):
    image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    image = Image.fromarray(image)
    image = transform(image).unsqueeze(0).to(torch.device('cuda' if torch.cuda.is_available() else 'cpu'))
    return image

# Load the model
model = models.resnet18(pretrained=True)
num_features = model.fc.in_features
model.fc = nn.Linear(num_features, len(dataset.classes))  # Adjusting the final layer according to the number of classes
del num_features
model.load_state_dict(torch.load('face_recognition_model.pth'))
model.to(torch.device('cuda' if torch.cuda.is_available() else 'cpu'))
model.eval()

# Predict the identity of the person in the image
def predict(image):
    with torch.no_grad():
        image_tensor = preprocess_image(image)
        outputs = model(image_tensor)
        probabilities = F.softmax(outputs, dim=1)
        max_prob, preds = torch.max(probabilities, 1)
        if max_prob.item() < 0.35:
            return "Unknown"
        return dataset.classes[preds[0]]

#########################################
# ** Initialization and Main Loop    ** #
#########################################

camera = jetson_utils.videoSource('/dev/video0')
display = jetson_utils.videoOutput()
faceDetector = jetson_inference.detectNet(network="facedetect", threshold = 0.8)
print("Finished importing faceDetector\n\n\n\n")

tracked_faces = {}
face_counter = 0

# FPS meaurement
fps = 0
frame_count = 0
start_time = time.time()

def calculate_centroid(left, top, right, bottom):
    return ((left + right) // 2, (top + bottom) // 2)

def find_matching_face(tracked_faces, new_centroid, distance_threshold=30):
    for face_id, data in tracked_faces.items():
        old_centroid = data['centroid']
        if np.linalg.norm(np.array(new_centroid) - np.array(old_centroid)) < distance_threshold:
            return face_id
    return None

while display.IsStreaming():
    frame = camera.Capture()
    print(type(frame))
    if frame is None:
        continue
    detections = faceDetector.Detect(frame, overlay="none")
    np_img = jetson_utils.cudaToNumpy(frame)
    cv_img = cv2.cvtColor(np_img, cv2.COLOR_RGBA2BGR)

    for detection in detections:
        left, top, right, bottom = int(detection.Left), int(detection.Top), int(detection.Right), int(detection.Bottom)
        centroid = calculate_centroid(left, top, right, bottom)

        matching_face_id = find_matching_face(tracked_faces, centroid)
        if matching_face_id is None:
            face_id = face_counter
            tracked_faces[face_id] = {'centroid': centroid, 'predictions': collections.deque(maxlen=5)}
            face_counter += 1
        else:
            face_id = matching_face_id
            tracked_faces[face_id]['centroid'] = centroid
        
        extracted_face = cv_img[top:bottom, left:right]
        recognized_person = predict(extracted_face)
        tracked_faces[face_id]['predictions'].append(recognized_person)

        most_common_person, count = collections.Counter(tracked_faces[face_id]['predictions']).most_common(1)[0]
        display_label = most_common_person if count >= 3 else "Analyzing..."

        cv2.rectangle(cv_img, (left, top), (right, bottom), (255, 0, 0), 2)
        cv2.putText(cv_img, display_label, (left, top - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 0, 0), 2)
    
    bgr_img = jetson_utils.cudaFromNumpy(cv_img, isBGR=True)
    rgb_img = jetson_utils.cudaAllocMapped(width=bgr_img.width, height=bgr_img.height, format='rgb8')
    jetson_utils.cudaConvertColor(bgr_img, rgb_img)
    display.Render(rgb_img)

    frame_count += 1
    if frame_count >= 30:
        end_time = time.time()
        fps = frame_count / (end_time - start_time)
        frame_count = 0
        start_time = time.time()

    display.SetStatus("Face Recognition - {:.0f} FPS | # faces :{:d} | True FPS {:.3f}".format(faceDetector.GetNetworkFPS(), len(detections), fps))

