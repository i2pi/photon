photon
======
A spectral path tracer with Metal GPU acceleration.

## Features

- **Metal compute shaders** for GPU-accelerated ray tracing on macOS
- **Spectral rendering** — samples random wavelengths per ray (380–780nm) with proper wavelength-to-RGB conversion
- **Cauchy dispersion** — wavelength-dependent refractive index (`n = A + B/λ²`) produces chromatic aberration and rainbow caustics
- **BVH acceleration** — bounding volume hierarchy for sphere intersection
- **Adaptive sampling** — early termination when pixel luminance converges
- **Camera lens simulation** — spherical lens with depth of field and chromatic dispersion
- **Multithreaded CPU fallback** — 32-thread render path when Metal is unavailable
- **HDR output** — saves both 8-bit PPM and 32-bit float framebuffers for post-processing

## Build

```
make          # Metal GPU (default, macOS)
make cpu      # CPU-only fallback
```

## Gallery

![img](https://raw.github.com/jdigittl/photon/master/images/photon001.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon002.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon003.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon004.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon005.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon006.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon007.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon008.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon009.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon010.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon011.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon012.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon013.png)
![img](https://raw.github.com/jdigittl/photon/master/images/photon014.png)
