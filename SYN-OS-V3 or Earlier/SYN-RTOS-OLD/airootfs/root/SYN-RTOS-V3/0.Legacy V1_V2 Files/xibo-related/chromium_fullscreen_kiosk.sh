# https://raspberrypi.stackexchange.com/questions/69204/open-chromium-full-screen-on-start-up
xset s off
xset -dpms
xset s noblank
chromium --kiosk http://calendar.google.com/  # load chromium after boot and open the website in full screen mode
