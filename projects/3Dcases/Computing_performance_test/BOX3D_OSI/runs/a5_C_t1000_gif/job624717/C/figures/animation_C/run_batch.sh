#!/usr/bin/env bash
# 本次补帧复现命令；八个独立单进程任务，避免跨 rank 场景加载等待。
set -euo pipefail
cd /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex
case_dir=projects/3Dcases/Computing_performance_test/BOX3D_OSI
driver_dir=$case_dir/runs/a5_B_three_plot3200/job622356/B/figures/animation_reexport_amrlevel
record_dir=$case_dir/runs/a5_C_t1000_gif/job624717/C/figures/animation_C
image_dir=$record_dir/frames_raw
pids=()
for span in 0:4 5:9 10:14 15:19 20:24 25:29 30:34 35:39; do
    first=${span%:*}
    last=${span#*:}
    env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONHOME -u DISPLAY \
        -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH -u PV_PLUGIN_PATH \
        QT_QPA_PLATFORM=offscreen OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 VTK_SMP_MAX_THREADS=2 \
        .tools/paraview-mpi/env/bin/pvbatch --force-offscreen-rendering \
        "$driver_dir/render_frames.py" --state "$record_dir/C_scene.pvsm" \
        --output "$image_dir" --start "$first" --stop "$last" --width 2110 --height 1066 --view-scale 1 \
        > "$record_dir/chunk_${first}_${last}.log" 2>&1 &
    pids+=("$!")
done
status=0
for task_pid in "${pids[@]}"; do
    if ! wait "$task_pid"; then status=1; fi
done
exit "$status"
