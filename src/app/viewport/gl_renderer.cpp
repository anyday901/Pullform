// OpenGL 3.3 core implementation of the Renderer interface. Every GL object is
// owned by an RAII handle (gl_handles.hpp) and released when the renderer is
// destroyed, which happens before the GL context is torn down.
#include "app/viewport/gl_handles.hpp"
#include "app/viewport/renderer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace pf {

namespace {

// Shaders are compiled into the binary; nothing is loaded from disk at runtime.
constexpr const char* kMeshVertex = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMvp;
out vec3 vNormal;
void main() {
    vNormal = aNormal;
    gl_Position = uMvp * vec4(aPosition, 1.0);
}
)";

constexpr const char* kMeshFragment = R"(#version 330 core
in vec3 vNormal;
uniform vec4 uColor;
uniform vec3 uLightDir;
uniform float uLit;
out vec4 fragColor;
void main() {
    vec3 n = normalize(vNormal);
    // Key light from the camera side plus a soft sky term so all faces stay readable.
    float key = max(dot(n, uLightDir), 0.0);
    float fill = max(dot(n, normalize(vec3(0.6, 0.5, 0.2))), 0.0);
    float light = 0.52 + 0.42 * key + 0.12 * fill;
    vec3 rgb = mix(uColor.rgb, uColor.rgb * light, uLit);
    fragColor = vec4(rgb, uColor.a);
}
)";

constexpr const char* kLineVertex = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
uniform mat4 uMvp;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uMvp * vec4(aPosition, 1.0);
}
)";

constexpr const char* kLineFragment = R"(#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

gl::Shader compile(GLenum type, const char* source) {
    gl::Shader shader(glCreateShader(type));
    glShaderSource(shader.get(), 1, &source, nullptr);
    glCompileShader(shader.get());
    GLint ok = 0;
    glGetShaderiv(shader.get(), GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(shader.get(), sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Shader compilation failed: ") + log);
    }
    return shader;
}

gl::Program link(const char* vs, const char* fs) {
    const gl::Shader v = compile(GL_VERTEX_SHADER, vs);
    const gl::Shader f = compile(GL_FRAGMENT_SHADER, fs);
    gl::Program program(glCreateProgram());
    glAttachShader(program.get(), v.get());
    glAttachShader(program.get(), f.get());
    glLinkProgram(program.get());
    GLint ok = 0;
    glGetProgramiv(program.get(), GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetProgramInfoLog(program.get(), sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Shader link failed: ") + log);
    }
    glDetachShader(program.get(), v.get());
    glDetachShader(program.get(), f.get());
    return program;
}

struct GpuMesh {
    gl::VertexArray vao;
    gl::Buffer vertices;
    gl::Buffer indices;
    GLsizei indexCount = 0;
    Vec3 origin{0.0}; ///< positions are stored as float offsets from this point
};

struct MeshVertex {
    float position[3];
    float normal[3];
};

struct LineVertex {
    float position[3];
    float color[4];
};

class GlRenderer final : public Renderer {
public:
    GlRenderer()
        : meshProgram_(link(kMeshVertex, kMeshFragment)), lineProgram_(link(kLineVertex, kLineFragment)),
          lineVao_(gl::makeVertexArray()), lineVbo_(gl::makeBuffer()) {
        meshMvp_ = glGetUniformLocation(meshProgram_.get(), "uMvp");
        meshColor_ = glGetUniformLocation(meshProgram_.get(), "uColor");
        meshLight_ = glGetUniformLocation(meshProgram_.get(), "uLightDir");
        meshLit_ = glGetUniformLocation(meshProgram_.get(), "uLit");
        lineMvp_ = glGetUniformLocation(lineProgram_.get(), "uMvp");

        glBindVertexArray(lineVao_.get());
        glBindBuffer(GL_ARRAY_BUFFER, lineVbo_.get());
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, color)));
        glBindVertexArray(0);
    }

    void beginFrame(const FrameSetup& frame) override {
        frame_ = frame;
        viewProj_ = frame.projection * frame.view;
        glEnable(GL_SCISSOR_TEST);
        glViewport(frame.x, frame.y, frame.width, frame.height);
        glScissor(frame.x, frame.y, frame.width, frame.height);
        glClearColor(frame.clearColor.r, frame.clearColor.g, frame.clearColor.b, frame.clearColor.a);
        glClearDepth(1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE); // solids are closed and outward-wound; sheets are explicitly two-sided
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_MULTISAMPLE);
    }

    MeshHandle createMesh(const MeshData& mesh) override {
        GpuMesh gpu{gl::makeVertexArray(), gl::makeBuffer(), gl::makeBuffer(), static_cast<GLsizei>(mesh.indices.size()), Vec3{0.0}};
        Bounds b;
        for (const auto& p : mesh.positions) b.extend(p);
        gpu.origin = b.valid ? b.min : Vec3{0.0};
        std::vector<MeshVertex> vertices(mesh.positions.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const glm::vec3 p(mesh.positions[i] - gpu.origin); // double -> float at the render boundary
            const glm::vec3 n(mesh.normals[i]);
            vertices[i] = MeshVertex{{p.x, p.y, p.z}, {n.x, n.y, n.z}};
        }
        glBindVertexArray(gpu.vao.get());
        glBindBuffer(GL_ARRAY_BUFFER, gpu.vertices.get());
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(MeshVertex)), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.indices.get());
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
                     mesh.indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, normal)));
        glBindVertexArray(0);
        const MeshHandle handle = nextHandle_++;
        meshes_.emplace(handle, std::move(gpu));
        return handle;
    }

    void destroyMesh(MeshHandle handle) override { meshes_.erase(handle); }

    void drawMesh(MeshHandle handle, const glm::vec4& color) override {
        const auto it = meshes_.find(handle);
        if (it == meshes_.end()) return;
        useMeshProgram(it->second, color, 1.0f);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        glDepthMask(color.a >= 1.0f ? GL_TRUE : GL_FALSE);
        glBindVertexArray(it->second.vao.get());
        glDrawElements(GL_TRIANGLES, it->second.indexCount, GL_UNSIGNED_INT, nullptr);
        glDepthMask(GL_TRUE);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    void drawMeshOverlay(MeshHandle handle, std::uint32_t firstIndex, std::uint32_t indexCount, const glm::vec4& color) override {
        const auto it = meshes_.find(handle);
        if (it == meshes_.end()) return;
        useMeshProgram(it->second, color, 0.0f);
        glDepthMask(GL_FALSE); // overlay sits exactly on the face (drawn at LEQUAL, without the fill offset)
        glBindVertexArray(it->second.vao.get());
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
                       reinterpret_cast<void*>(static_cast<std::uintptr_t>(firstIndex) * sizeof(std::uint32_t)));
        glDepthMask(GL_TRUE);
    }

    void drawLines(const std::vector<LineSegment>& lines, bool depthTest) override {
        if (lines.empty()) return;
        // Lines are positioned relative to the eye for float precision.
        const Vec3 origin = frame_.eye;
        lineScratch_.clear();
        lineScratch_.reserve(lines.size() * 2);
        for (const auto& l : lines) {
            const glm::vec4 cb = l.colorB.a < 0.0f ? l.color : l.colorB;
            const glm::vec3 a(l.a - origin), b(l.b - origin);
            lineScratch_.push_back(LineVertex{{a.x, a.y, a.z}, {l.color.r, l.color.g, l.color.b, l.color.a}});
            lineScratch_.push_back(LineVertex{{b.x, b.y, b.z}, {cb.r, cb.g, cb.b, cb.a}});
        }
        glUseProgram(lineProgram_.get());
        const glm::mat4 mvp(viewProj_ * glm::translate(glm::dmat4(1.0), origin));
        glUniformMatrix4fv(lineMvp_, 1, GL_FALSE, glm::value_ptr(mvp));
        glBindVertexArray(lineVao_.get());
        glBindBuffer(GL_ARRAY_BUFFER, lineVbo_.get());
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lineScratch_.size() * sizeof(LineVertex)), lineScratch_.data(),
                     GL_STREAM_DRAW);
        if (!depthTest) glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineScratch_.size()));
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(0);
    }

    void endFrame() override {
        glBindVertexArray(0);
        glUseProgram(0);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
    }

private:
    void useMeshProgram(const GpuMesh& mesh, const glm::vec4& color, float lit) {
        glUseProgram(meshProgram_.get());
        const glm::mat4 mvp(viewProj_ * glm::translate(glm::dmat4(1.0), mesh.origin));
        glUniformMatrix4fv(meshMvp_, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform4f(meshColor_, color.r, color.g, color.b, color.a);
        // Fixed world-space key light (high, from the front-left) so the faces of
        // a box read as distinct shades from any viewpoint.
        const glm::vec3 light = glm::normalize(glm::vec3(-0.45f, -0.75f, 0.9f));
        glUniform3f(meshLight_, light.x, light.y, light.z);
        glUniform1f(meshLit_, lit);
    }

    gl::Program meshProgram_;
    gl::Program lineProgram_;
    gl::VertexArray lineVao_;
    gl::Buffer lineVbo_;
    GLint meshMvp_ = -1, meshColor_ = -1, meshLight_ = -1, meshLit_ = -1, lineMvp_ = -1;
    std::unordered_map<MeshHandle, GpuMesh> meshes_;
    MeshHandle nextHandle_ = 1;
    FrameSetup frame_;
    glm::dmat4 viewProj_{1.0};
    std::vector<LineVertex> lineScratch_;
};

} // namespace

std::unique_ptr<Renderer> createGlRenderer() { return std::make_unique<GlRenderer>(); }

} // namespace pf
