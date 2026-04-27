Each frequency level maps to a descriptor set index:
  - Set 0: Per-frame data (ring-buffered, one descriptor set per swapchain image)
  - Set 1: Per-material (bound once per material change)
  - Set 2: Per-scene globals (bound once per frame)
