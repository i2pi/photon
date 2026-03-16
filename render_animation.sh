#!/bin/bash
# Render 360 frames with cylindrical lens element rotating 1 degree per frame
# Uses the scene's own quality settings — only changes the anamorphic angle
set -e

OUTDIR="anim_frames"
mkdir -p "$OUTDIR"

SCENE_TEMPLATE="scenes/glass_spheres.scene"
TEMP_SCENE="/tmp/photon_anim.scene"

for frame in $(seq 0 359); do
    angle=$frame
    printf "\r[Frame %03d/359] anamorphic=%d " "$frame" "$angle"

    # Generate scene file — only change the anamorphic angle, keep all other settings
    sed "s/anamorphic=[0-9]*/anamorphic=$angle/" "$SCENE_TEMPLATE" > "$TEMP_SCENE"

    # Render
    ./photon "$TEMP_SCENE" 2>/dev/null | tail -1

    # Move output frame
    mv frame00000001.ppm "$OUTDIR/frame_$(printf '%03d' $frame).ppm"

    # Convert to PNG
    sips -s format png "$OUTDIR/frame_$(printf '%03d' $frame).ppm" \
        --out "$OUTDIR/frame_$(printf '%03d' $frame).png" 2>/dev/null
done

echo ""
echo "All frames rendered. Assembling video..."

if command -v ffmpeg &>/dev/null; then
    ffmpeg -y -framerate 30 -i "$OUTDIR/frame_%03d.png" \
        -c:v libx264 -pix_fmt yuv420p -crf 18 \
        anim_rotation.mp4 2>/dev/null
    echo "Video saved: anim_rotation.mp4 (360 frames @ 30fps = 12s)"
else
    echo "ffmpeg not found. Frames saved in $OUTDIR/"
    echo "To create video: ffmpeg -framerate 30 -i $OUTDIR/frame_%03d.png -c:v libx264 -pix_fmt yuv420p anim_rotation.mp4"
fi
