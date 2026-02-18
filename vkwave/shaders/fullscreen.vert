#version 450

// Fullscreen triangle: 3 vertices cover the entire screen.
// No vertex buffer needed — positions derived from gl_VertexIndex.
// Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
// After clipping, this covers the [-1,1] x [-1,1] NDC range.

layout(location = 0) out vec2 fragUV;

void main()
{
    fragUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragUV * 2.0 - 1.0, 0.0, 1.0);
}
