# Rules for working on this project

1. **Commit frequently** with test render images at each step.
2. **No hard-coded geometric hacks.** The scene will change. Everything must be general and data-driven. No screen-space cheats. No canned filters pretending to be physics.
3. **Use the `is_lens` flag** to distinguish lens elements from scene objects. Do not use fragile heuristics like checking transparency or phong values.
4. **Use `Read` for images.** Never trust the `look_at` tool — it lies.
5. **Data-driven analysis.** Check actual pixel values before changing parameters. Do not guess.
6. **No hallucinating.** Do not make up causes of artifacts. If you don't know, say so.
7. **Work autonomously** through iterative build/render/analyze cycles. Do not stop after one hour. Keep going.
8. **Reference image is `glass.png`.** That is the artistic target.
9. **No screen-space cheats.** Ghost streaks and lens flares must come from actual ray tracing through the real lens system. The anamorphic element shape must emerge naturally from the cylinder geometry, not from hardcoded horizontal line filters.
10. **Respect the lens system.** Adjust lens element positions, curvatures, reflectances, and spacing to achieve the desired optical effects. Do not bypass the optics with post-processing tricks.
11. **Re-read these rules before each iteration.**
