"""可选的 JPG/JPEG 转 PNG 工具；主程序已经可以直接读取 JPG。"""

import cv2
import os

# 默认在原输入目录中生成同名 PNG，不会覆盖原 JPG。
input_dir = "/data/04RKNN_GIthub_related/rknn_play/MobileSAM_RK3588/inputimage"
output_dir = "/data/04RKNN_GIthub_related/rknn_play/MobileSAM_RK3588/inputimage"

os.makedirs(output_dir, exist_ok=True)

for filename in os.listdir(input_dir):
    if filename.lower().endswith((".jpg", ".jpeg")):
        img_path = os.path.join(input_dir, filename)
        img = cv2.imread(img_path)

        if img is not None:
            output_path = os.path.join(output_dir, os.path.splitext(filename)[0] + ".png")
            if cv2.imwrite(output_path, img):
                print(f"Converted: {filename} -> {output_path}")
            else:
                print(f"Failed to write: {output_path}")
        else:
            print(f"Failed to read: {filename}")

print("All images converted!")
