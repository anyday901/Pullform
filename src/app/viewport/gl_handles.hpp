#pragma once

#include <glad/gl.h>

#include <utility>

namespace pf::gl {

/// Move-only RAII owner of one OpenGL object name. `Deleter` releases it.
template <class Deleter>
class Handle {
public:
    Handle() = default;
    explicit Handle(GLuint id) : id_(id) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : id_(std::exchange(other.id_, 0)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            id_ = std::exchange(other.id_, 0);
        }
        return *this;
    }
    [[nodiscard]] GLuint get() const { return id_; }
    explicit operator bool() const { return id_ != 0; }
    void reset() {
        if (id_ != 0) Deleter{}(id_);
        id_ = 0;
    }

private:
    GLuint id_ = 0;
};

struct BufferDeleter {
    void operator()(GLuint id) const { glDeleteBuffers(1, &id); }
};
struct VertexArrayDeleter {
    void operator()(GLuint id) const { glDeleteVertexArrays(1, &id); }
};
struct ProgramDeleter {
    void operator()(GLuint id) const { glDeleteProgram(id); }
};
struct ShaderDeleter {
    void operator()(GLuint id) const { glDeleteShader(id); }
};

using Buffer = Handle<BufferDeleter>;
using VertexArray = Handle<VertexArrayDeleter>;
using Program = Handle<ProgramDeleter>;
using Shader = Handle<ShaderDeleter>;

inline Buffer makeBuffer() {
    GLuint id = 0;
    glGenBuffers(1, &id);
    return Buffer(id);
}

inline VertexArray makeVertexArray() {
    GLuint id = 0;
    glGenVertexArrays(1, &id);
    return VertexArray(id);
}

} // namespace pf::gl
