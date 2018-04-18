sudo insmod mp3.ko
cat /proc/mp3/status
echo "R 358" > /proc/mp3/status
cat /proc/mp3/status
echo "R 922" > /proc/mp3/status
cat /proc/mp3/status
echo "U 358" > /proc/mp3/status
echo "U 922" > /proc/mp3/status
cat /proc/mp3/status
echo "R 392" > /proc/mp3/status
cat /proc/mp3/status
echo "U 355" > /proc/mp3/status
cat /proc/mp3/status
echo "U 392" > /proc/mp3/status
dmesg -T | tail -50
