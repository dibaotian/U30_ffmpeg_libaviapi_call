ffmpeg -c:v mpsoc_vcu_h264 -i /home/xilinx/Documents/1080p.264 -vf xvbm_convert -pix_fmt yuv420p -y /tmp/xil_dec_out.yuv
# ffmpeg -c:v mpsoc_vcu_h264 -i /home/xilinx/Documents/1080p.264 -y /tmp/xil_dec_out.yuv
