#include "vertex.hh"

std::vector<vertex> make_rectangle(glm::vec2 a, glm::vec2 b)  {
    std::vector<vertex> verts;
    verts.resize(4);
    auto* rect_verts = verts.data();

    ///
    /// Determine the corners of the rectangle such that rect_verts[0..3] are in anticlockwise order.
    ///
    rect_verts[0].pos = glm::vec3(a, 0.0f);
    rect_verts[2].pos = glm::vec3(b, 0.0f);

    glm::vec2 v = b - a;

    /// a         /// a
    ///           ///
    ///        b  ///        b
    if ((v.x > 0 && v.y > 0) || (v.x < 0 && v.y < 0)) {
        rect_verts[1].pos = glm::vec3(a.x, a.y + v.y, 0.0f);
        rect_verts[3].pos = glm::vec3(a.x + v.x, a.y, 0.0f);
    }

    ///        b  ///        a
    ///           ///
    /// a         /// b
    else if ((v.x > 0 && v.y < 0) || (v.x < 0 && v.y > 0)) {
        rect_verts[1].pos = glm::vec3(a.x + v.x, a.y, 0.0f);
        rect_verts[3].pos = glm::vec3(a.x, a.y + v.y, 0.0f);
    }

    /// a       b
    else {
        rect_verts[1].pos = rect_verts[2].pos;
        rect_verts[3].pos = rect_verts[0].pos;
    }

    return verts;
}