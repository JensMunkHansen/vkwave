Hi Gustav

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
