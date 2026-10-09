# Parameter `CamXclkFreqMhz`

**Camera Xclk Frequency**

Range (`8` .. `20`)

Default Value: `8`

See [here](../datasheets/Camera.ov2640_ds_1.8_.pdf) for the ov2640 camera datasheet.<br>
See [here](../datasheets/OV5640_datasheet.pdf) for the ov5640 camera datasheet.

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

    After changing this parameter you need to update your reference image and alignment markers!

!!! Note
    Camera Xclk Frequency in Mhz: `8` (lowest frequency) ... `20` (highest frequency)

	For some cameras, a higher frequency leads to problems when initializing the camera or with the WiFi connection.
	However, if the frequency is too low, the quality of the images may not be as good.
	Therefore, this parameter should be set carefully.