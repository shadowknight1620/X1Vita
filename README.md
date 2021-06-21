# X1Vita

**Download**: https://github.com/Ibrahim778/X1Vita/releases

**Enable the plugin:**

1. Add X1Vita.skprx to taiHEN's config (ux0:/tai/config.txt) or (ur0:/tai/config.txt):
	```
	*KERNEL
	ux0:tai/X1Vita.skprx
	```
2. You need to refresh the config.txt by rebooting or through VitaShell.

**Using it for the first time (pairing the controller):**

1. Go to Settings -> Devices -> Bluetooth Devices
2. Press the pair on the controller for about 3-4 seconds, until the logo blinks very quickly
3. The controller will then connect and be paired (You may get a message saying "Do you want to connect to (blank)" just press ok and ignore the error)

**Using it once paired (see above):**
1. Just press the xbox button and it will connect to the Vita

**Note**: If you are using multiple controllers the controller ports will be set in the order you connect (if available), so first connection -> port 1 second connection -> port 2 etc, simply powering off the controller (ie removing batteries) will not trigger a disconnect on vita. You need to hold the Xbox Button till it turns off.  
**Note**: If you use Mai, don't put the plugin inside ux0:/plugins because Mai will load all stuff you put in there...  

This plugin is **not** compatible with the other ds vita plugins yet!!!! (but you can use ds3/4 on vita TV or use minivitaTV)  

Made for the kyuhen homebrew contest.  
Based on ds4vita  

# FAQ
1. My controller connects but doesn't do anything!  
There can several reasons but here are the most common:  
1. Your using a 3rd party controller. (see note below)
2. Your using an official controller other than the Xbox One (For e.g. Xbox One Eliete, Eliete 2) (see note below)
3. Your using the new Xbox one controllers, ie the ones with the screenshot button (including the series controllers) are **not** suppourted, this is simply because the vita's bluetooth version does not suppourt them so they simply cannot connect no matter what.
4. You installed the plugin incorrectly (install and open the companion to see if X1Vita is detected)

## Note about controllers not working
For now only official Xbox One controllers are suppourted all others will be ignored. If you want your controller to be added install the companion open it and go to Debug Bluetooth then connect the controller and note down the PID or VID. Then send them to me somehow along with controller name. (My discord is in the companion)


