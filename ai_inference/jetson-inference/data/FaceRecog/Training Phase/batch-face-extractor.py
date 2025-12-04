import jetson.inference
import jetson.utils
import os

# Config
NET_MODEL = "facenet" 
THRESHOLD = 0.5
SOURCE_DIR = "../dataset/source"
SAVE_DIR = "../dataset/processed"

# Initialize network
print(f"Loading {NET_MODEL} network...")
net = jetson.inference.detectNet(NET_MODEL, threshold=THRESHOLD)

if not os.path.exists(SAVE_DIR):
    os.makedirs(SAVE_DIR)

print(f"Scanning {SOURCE_DIR}...")

for person_name in os.listdir(SOURCE_DIR):
    person_path = os.path.join(SOURCE_DIR, person_name)
    
    if not os.path.isdir(person_path):
        continue

    print(f"Processing folder: {person_name}")
    
    save_person_path = os.path.join(SAVE_DIR, person_name)
    if not os.path.exists(save_person_path):
        os.makedirs(save_person_path)

    count = 0
    for image_name in os.listdir(person_path):
        if not (image_name.lower().endswith(".jpg") or image_name.lower().endswith(".jpeg") or image_name.lower().endswith(".png")):
            continue

        image_path = os.path.join(person_path, image_name)
        
        try:
            img = jetson.utils.loadImage(image_path)
            
            # Detect faces
            detections = net.Detect(img, overlay="none")
            
            if len(detections) == 0:
                print(f"  [Miss] No face found in {image_name}")
                continue

            # Process the first detected face
            detection = detections[0]

            # --- THE FIX: CLAMP COORDINATES ---
            # Ensure coordinates are integers and within image bounds
            left = int(max(0, detection.Left))
            top = int(max(0, detection.Top))
            right = int(min(img.width, detection.Right))
            bottom = int(min(img.height, detection.Bottom))
            
            crop_width = right - left
            crop_height = bottom - top

            # Sanity check: ensure valid crop size
            if crop_width <= 0 or crop_height <= 0:
                print(f"  [Error] Invalid crop dimensions for {image_name}")
                continue

            # Define ROI (Region of Interest)
            roi = (left, top, right, bottom)
            
            # Allocate memory specifically for this crop size
            crop = jetson.utils.cudaAllocMapped(width=crop_width, 
                                                height=crop_height, 
                                                format=img.format)
            
            # Perform the crop
            jetson.utils.cudaCrop(img, crop, roi)
            jetson.utils.cudaDeviceSynchronize()
            
            # Save
            save_filename = f"{count}.jpg"
            jetson.utils.saveImage(os.path.join(save_person_path, save_filename), crop)
            print(f"  [Success] Saved {save_filename}")
            
            count += 1
                
        except Exception as e:
            print(f"  [Exception] Failed processing {image_name}: {e}")

    print(f" -> Total extracted for {person_name}: {count}\n")

print("Extraction Complete.")