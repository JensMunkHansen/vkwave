Hi Gustav

Some numbers:

Complexity:

Our GSO:
 # parameters: 6*nSubscans
 # data:       nVertices x nSubscansPerVertex(~40)
 # algorithm: 
  - Allows Newton-Rhapson
  - No cross-terms, it is block-diagonal

nVertices is at its maximum 300k, nSubscan is typically around 2.5k.

Our Colors
 # parameters:  (slope + intercept) * nVertrices (x4 if meshcolors)
 # data:        nVertices x nSubscansPerVertex(~40) x (color + angle) x (x4 if MeshColors)
 # algorithm:
  - We need this for R, G and B (x3)
  - Does not allow Newton-Rhapson (neither does my fast scalar version)
  - We have cross-terms, it is not block-diagonal

Quality:
 # Edge-term
  - High value: Nice smoothness, BLURRING, BLEEDING. Developers tried
    segmentation, but it only covers gingiva/teeth boundaries.
  - Low value: Slopes causes unwanted halos.
  - We developed weighting schemes, but issue is still there
  - Convergence is slow

Optimization (done):
  - Stencil solver cut 20s of the long post-processing. Concept is
    independent of cost function. Otherwise, I would not have
    suggested it for C++ code. We could keep the C# version - still
    saves us a lot of time.

Optimization (known, but not completed):
  - Solver in C++ (2 MM), 600ms becomes 480 ms. Not worth it.
  - RaySearch. Our RaySearch is 300M Ray/subscans per second. Our bottleneck is everything around it. Ellen showed that when she reused the results, it actually got slower.

My best guess is that the 1.2s for SPP (Ellen's result) could be
1.2s + 150ms (fast scalar solver) + 250ms extra time spend when
computing multipoints, but even this is 3 MM. And perhaps, this extra
cost is also too much.

Honest recommendation: Proper algorithm work with more focus on
complexity and performance (for the solver). Perhaps one could
investigate what the smallest change to our existing averaging colors
could be. For SPP, we could use the local linear fits and potentially
a solver, if we can find one that is sufficiently fast. For the LPP,
we should use some sort of solver, but it cannot be our current
edge-aware solver. The cost function is invalid.









First I heard, 2 dev, 3 months. Now, I hear 2 dev, 24 man months. I think we need to pursue this differently

If I should be a part of this, I would prefer to be system architect and NOT a software developer, who is
- The tech-lead
- The de-facto architect (responsibility > influence)
- The person who needs to review everything
- The DevEx department
- The teacher for using foreign technologies (c, c++, arm, cmake, vtk, qemu, valgrind, cachegrind, qt,… the list is very long) 

I don’t want to go through that again. Before 3Shape, I was a principal system engineer – working on algorithms and 



We need a detailed plan
•	Important constraints: As an example: If we use an SDK-driven approach, we don’t need an application for showing (it took me many months to convince management and architects in the WebCAD project about this). Instead of a monolithic test application, you can in a few lines setup a part of your pipeline, visualization and interaction. You can use it for a demo, you can even record event and in a test replay them. If we replicate the architecture from KitWare (VTK), there is a rather steep learning curve, but we get many things for free. Other constrain
