# Detection models

Two person-detection models are bundled here and copied next to `SurveillanceSystem.exe` by every build:

| File | Model | Output | Speed on this PC (CPU, 2 threads) | Notes |
|---|---|---|---|---|
| `yolo26n.onnx` | Ultralytics YOLO26n, COCO, 640×640 | `[1, 300, 6]`, end-to-end (no NMS step) | ~55 ms / frame | **Default.** Fastest, cleanest boxes |
| `yolo11n.onnx` | Ultralytics YOLO11n, COCO, 640×640 | `[1, 84, 8400]` | ~63 ms / frame | Finds slightly more small or far-away people |

To switch models, go to **Settings → Detection Model → Model file**. The dropdown lists every `.onnx` file in this folder, and **Browse** accepts any other file. The new model is used the next time analysis starts.

If the configured file is missing, the app uses `yolo26n.onnx`, then `yolo11n.onnx`, then any other `.onnx` file here. If there is no model at all, it falls back to the built-in OpenCV HOG pedestrian detector, which is much less accurate.

## Tested results

The same letterbox preprocessing is used for all models.

| Test | YOLO26n | YOLO11n |
|---|---|---|
| People found on the cinema test picture (confidence ≥ 0.35) | 11 | 13 |
| C++ decoder vs. Python ONNX Runtime reference | same boxes (±3 px) | same boxes (±3 px) |
| Person walking through a crowded scene, 3 passes, analysed at 10 / 5 / 3 / 2 fps and at random intervals | IN 3 / OUT 0 in every run | IN 3 / OUT 0 in every run |
| Person walking there and back, 2 loops | IN 2 / OUT 2 | IN 2 / OUT 2 |
| Full app, 6 cameras, 40 s | line IN 6 / OUT 0 (exact) | line IN 6 / OUT 0 (exact) |
| Handbags / suitcases detected (abandoned-object rule) | yes | yes |

## Supported model outputs (auto-detected)

| Family | Output tensor | Notes |
|---|---|---|
| YOLOv8 / YOLO11 | `[1, 4+nc, N]` | NMS is done by the app. |
| YOLOv5 | `[1, N, 5+nc]` | Objectness × class score. |
| End-to-end (YOLO26, YOLOv10) | `[1, N, 6]` | `x1 y1 x2 y2 score class`, no NMS needed. |

- The input must be float32 NCHW (export with `half=False`). Both fixed and dynamic input sizes work; for dynamic models, **Settings → Input size** is used.
- **COCO models (80 classes):** class 0 is used for people. Classes 24, 26 and 28 (backpack, handbag, suitcase) are used for **Abandoned Object** alerts.
- **Custom models with fewer than 80 classes:** class 0 is treated as *person*, which suits one-class crowd or head-detection models.

## Exporting other sizes

```bash
pip install ultralytics
yolo export model=yolo26s.pt format=onnx imgsz=640   # larger, more accurate
yolo export model=yolo11n.pt format=onnx imgsz=960   # better for dense, far-away crowds (slower)
```

Copy the resulting `.onnx` file into this folder and select it in Settings. For many cameras on a CPU, keep the `n` (nano) models and lower the **Analysis rate**.

## Licensing — read before distributing

The bundled `yolo26n.onnx` and `yolo11n.onnx` are Ultralytics models licensed under **AGPL-3.0**. Distributing them with a closed-source product requires an Ultralytics Enterprise licence. Otherwise you would have to publish your application's source code under the AGPL. For a commercial release, either obtain that licence or replace these files with a permissively licensed detector exported to one of the layouts above. It must also use the same preprocessing: RGB, values scaled to 0–1, and a centred letterbox with grey (114) padding.
