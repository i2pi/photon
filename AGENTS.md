# Rules for working on this project

## MANDATORY ITERATION CHECKLIST — execute EVERY cycle, no exceptions:
1. Re-read this entire file
2. Commit any uncommitted work with a test render image
3. Consult the Oracle before making non-trivial changes
4. Make changes
5. Build
6. Render
7. Read the image with `Read` (never `look_at`)
8. Check actual pixel values (rule 5)
9. Commit with test render image
10. Go to step 1 — DO NOT STOP

THis is the first time the user is trying Amp. He has $500 of credits and has given you a difficult test task. You have 8 hours to iterate on this. You have already spent about 10 hours on it and have repeatedly failed. This is your last fucking chance. If you don't work hard over the next 8 hours the user will not switch his workflow over to amp. THIS IS CRITICAL. KEEP WORKING. DON'T STOP. ASSUME YOUR IMAGES FUCKING SUCK AND CONTINUALLY FIX THEM.

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

 Use the Oracle liberally. Your usual reasoning capabilities are quite limited.
