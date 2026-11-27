# tinybvh

Bounding volume hierarchy construction and ray traversal over plain triangle arrays.

- Upstream: [jbikker/tinybvh](https://github.com/jbikker/tinybvh) at commit `769343f44d3cd1fe9e0effafd570b9be064b048c`. The project publishes no release tags
- Files: `tiny_bvh.h`, `tiny_bvh_base.h` and the four `tiny_bvh_*_float.h` and `tiny_bvh_*_double.h` kernels, verbatim
- License: MIT, `LICENSE` from upstream

`CMakeLists.txt` and `tinybvh.cpp` are ours, MIT like the library. They build the implementation once as a static library.
