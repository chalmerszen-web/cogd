# AOQ camera ownership regressions

Run `bash tests/aoq_camera/run.sh` on Linux/WSL with g++ and ASan/UBSan.

The harness compiles the production `p4_video_capture.cpp`. It checks bounded
RGB565 resizing input, one-frame-per-second admission, current frame timestamps,
sequence advancement, stale-frame rejection, output capacity, and revocation of
the previous cache after failed/oversized encoding. Generation replacement and
stop are injected during encoding to prove that an obsolete worker cannot
publish into a new camera session or cancel that session afterwards.

The JPEG hardware driver, allocation, clock and RTOS mutex are deterministic
stand-ins. These checks do not establish sensor capture, real JPEG correctness,
concurrent FreeRTOS scheduling, board timing or native video interoperability.
Those require a separately identified P4 image and hardware run.
