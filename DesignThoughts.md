❯ What is your honest opinion about moving to objects with zero argument constructors. I just foresee that adding functionality will be too complex unless we streamline the architecture. Adding debug for        
  shader compilation the ad-hoc way required 8 class/functions modified to take different amount of arguments.                                                                                                     
                                                                                                                                                                                                                   
● Your instinct is right. The problem you hit with shader debugging is the textbook symptom: a new boolean needs to flow through 8 constructors because every layer between the config and the object that uses it 
  has to thread the parameter through its signature. That's O(depth) changes for O(1) new functionality.                                                                                                           
                                                                                                                                                                                                                   
  With setters, adding shader_debug would have been: add one member, add one setter, set it at the one place that knows the value. Zero intermediate layers touched.
                                                                                                                                                                                                                   
  The tradeoff is real — two-phase init means objects can exist in an unconfigured state. But the m_modified assert catches that at the first use, which in practice is as good as a compile error since it fires
  immediately in debug. And Vulkan objects are inherently two-phase anyway (you fill a CreateInfo, then call vkCreate). The setter pattern just mirrors what the API already does.

  The deeper architectural benefit: it makes the dependency graph between configuration and construction explicit. Right now, adding a feature means answering "which constructors do I need to modify to get this
  value from A to B?" With setters, you just set it where you have it. The object doesn't care how the value arrived.

  The one thing I'd watch for: don't let it drift into "set 20 things in random order across 5 files." Keep the configure-then-init sequence local — ideally in one function or one constructor body, like
  Engine's. If the setter calls start spreading across the codebase, you've traded one problem for another.


 I completely agree. State should be held local and if an object own other objects that needs to be update, it should call update on those.                                                                       
                                                                                                                                             
● Exactly. The owner knows what changed, so it calls update() or reinit() on the things it owns. The children don't need to know why they're being updated — they just reconfigure from their current state. That's
   also what makes the m_modified flag powerful: if a child's setter is called, it knows it needs reinit, and the parent can batch multiple changes before calling init() once.
                                                                                                                                                                                                                   
  This also aligns with the graph-owns-resources principle already in CLAUDE.md. The graph owns the resource sets, passes just use them. Same pattern one level up: Engine owns Instance/Device/Swapchain,         
  configures them, and propagates changes downward. No lateral dependencies, no threading configuration sideways through unrelated objects.                                                                        
                                                            
────────────────────────────────────────────────────────────────────────
