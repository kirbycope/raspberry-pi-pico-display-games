# Image Converter
Converts an image into a list of "changes". The I2C display API available through [pimoroni-pico](https://github.com/pimoroni/pimoroni-pico/tree/main/micropython/modules/pico_display) didn't have a load image method so I wrote this help to convert a BMP into a list of pixels. A pixel is any color that is not black as the display_clear() from the [Helpers.py](/helpers.py) set everything to `(0, 0, 0)`.

## Known Limitations
   - ~11KB memory limit. Going over this also cause Thonny to act sluggish when viewing the generated .py file.
   - ?? pixel limit. Going over a certain number seems to cause noise and eventually crashes.

## Example

### BMP

![Screenshot](/desktop/screen.bmp)
![Screenshot](/desktop/screen_2.bmp)

### Generated .py
This is pretty-printed for the wiki, it is minified in the actual output. 
```python
img_array = [
    "0,0,0,255,0", # Pixel at (0, 0) has RGB of (0, 255, 0)
    "0,239,0,0,255",
    "2,3,255,255,255"
]
```

### Loaded Image on Pico Display
![Screenshot](/desktop/screen.jpg)
![Screenshot](/desktop/screen_2.jpg)
