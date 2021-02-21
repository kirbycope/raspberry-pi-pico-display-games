# Image Converter
Converts an image into a list of "changes". The I2C display API available through [pimoroni-pico](https://github.com/pimoroni/pimoroni-pico/tree/main/micropython/modules/pico_display) didn't have a load image method so I wrote this help to convert a BMP into a list of pixels. A pixel is any color that is not black as the disaply_clear() from the (Helpers.py](./helpers.py) set everything to `(0, 0, 0)`.

## Example

### BMP

![Screenshot](/desktop/screen.bmp)

### Generated .py
This is pretty-printed for the wiki, it is minified in the actual output. 
```python
img_array = [
    "0,0,0,255,0",
    "0,239,0,0,255",
    "2,3,255,255,255"
]
```
