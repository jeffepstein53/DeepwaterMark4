#include "pages/data/Gl3d.h"
#include <QOpenGLContext>
#include <QtMath>
#include <cmath>

namespace macrodata3d {
namespace {
const char* kVert = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 iPos;
layout(location = 3) in vec3 iSize;
layout(location = 4) in vec4 iColor;
layout(location = 5) in vec3 iGrid;
layout(location = 6) in float iSpan;
uniform mat4 uVP;
uniform vec4 uSlice;   // axis, value, ghost alpha, cut (1: hide the cells beyond the slice, between it and the camera)
out vec4 vColor;
void main() {
  vec3 p = iPos + aPos * iSize;
  gl_Position = uVP * vec4(p, 1.0);
  vec3 L = normalize(vec3(-0.45, 0.85, -0.35));
  float diff = max(dot(aNormal, L), 0.0);
  float shade = 0.42 + 0.58 * diff;
  vec4 c = vec4(iColor.rgb * shade, iColor.a);
  if (uSlice.x > -0.5) {
    bool inside;
    if (uSlice.x < 0.5) inside = uSlice.y >= iGrid.x && uSlice.y < iGrid.x + max(iSpan, 1.0);
    else if (uSlice.x < 1.5) inside = abs(iGrid.y - uSlice.y) < 0.5;
    else inside = abs(iGrid.z - uSlice.y) < 0.5;
    float g = uSlice.x < 0.5 ? iGrid.x : (uSlice.x < 1.5 ? iGrid.y : iGrid.z);
    if (inside) c.rgb = min(c.rgb * 1.30 + 0.06, vec3(1.0));
    else if (uSlice.w > 0.5 && g > uSlice.y) c.a = 0.0;
    else c.a *= uSlice.z;
  }
  vColor = c;
}
)";
const char* kFrag = R"(
in vec4 vColor;
uniform int uPass;     // 0 opaque, 1 see-through
out vec4 frag;
void main() {
  if (vColor.a < 0.004) discard;
  if (uPass == 0 && vColor.a < 0.9) discard;
  if (uPass == 1 && vColor.a >= 0.9) discard;
  frag = vColor;
}
)";
// unit box: x, z in [-0.5, 0.5], y in [0, 1]; 6 faces × 2 triangles, position + normal
QVector<float> boxMesh() {
  QVector<float> v;
  auto quad = [&](QVector3D a, QVector3D b, QVector3D c, QVector3D d, QVector3D n) {
    for (const QVector3D& p : {a, b, c, a, c, d}) v << p.x() << p.y() << p.z() << n.x() << n.y() << n.z();
  };
  const float l = -0.5f, r = 0.5f, bo = 0.f, t = 1.f, f = 0.5f, k = -0.5f;
  quad({l, bo, f}, {r, bo, f}, {r, t, f}, {l, t, f}, {0, 0, 1});
  quad({r, bo, k}, {l, bo, k}, {l, t, k}, {r, t, k}, {0, 0, -1});
  quad({r, bo, f}, {r, bo, k}, {r, t, k}, {r, t, f}, {1, 0, 0});
  quad({l, bo, k}, {l, bo, f}, {l, t, f}, {l, t, k}, {-1, 0, 0});
  quad({l, t, f}, {r, t, f}, {r, t, k}, {l, t, k}, {0, 1, 0});
  quad({l, bo, k}, {r, bo, k}, {r, bo, f}, {l, bo, f}, {0, -1, 0});
  return v;
}
}  // namespace

BoxRenderer::~BoxRenderer() = default;

// The GLSL version line for the current context: OpenGL ES 3.0 on iOS (iPhone and iPad have no desktop OpenGL; the
// "#version 330 core" shaders were refused there and the 3D views stayed empty), 3.3 core elsewhere.
static QByteArray glslHeader() {
  const QOpenGLContext* c = QOpenGLContext::currentContext();
  if (c && c->isOpenGLES()) return "#version 300 es\nprecision highp float;\nprecision highp int;\n";
  return "#version 330 core\n";
}

QSurfaceFormat surfaceFormat() {
  QSurfaceFormat f;
#ifdef Q_OS_IOS
  f.setRenderableType(QSurfaceFormat::OpenGLES);
  f.setVersion(3, 0);
#else
  f.setVersion(3, 3); f.setProfile(QSurfaceFormat::CoreProfile);
#endif
  f.setDepthBufferSize(24); f.setSamples(4);
  return f;
}

bool BoxRenderer::init() {
  initializeOpenGLFunctions();
  m_prog = std::make_unique<QOpenGLShaderProgram>();
  const QByteArray head = glslHeader();
  if (!m_prog->addShaderFromSourceCode(QOpenGLShader::Vertex, head + kVert) ||
      !m_prog->addShaderFromSourceCode(QOpenGLShader::Fragment, head + kFrag) || !m_prog->link()) {
    qWarning("[facts3d] shaders: %s", qPrintable(m_prog->log()));
    return false;
  }
  glGenVertexArrays(1, &m_vao);
  glBindVertexArray(m_vao);
  const QVector<float> mesh = boxMesh();
  glGenBuffers(1, &m_mesh);
  glBindBuffer(GL_ARRAY_BUFFER, m_mesh);
  glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(float), mesh.constData(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
  glGenBuffers(1, &m_inst);
  glBindBuffer(GL_ARRAY_BUFFER, m_inst);
  const int stride = sizeof(Instance);
  auto attr = [&](int loc, int n, int offsetFloats) {
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, n, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(qintptr(offsetFloats * sizeof(float))));
    glVertexAttribDivisor(loc, 1);
  };
  attr(2, 3, 0); attr(3, 3, 3); attr(4, 4, 6); attr(5, 3, 10); attr(6, 1, 13);
  glBindVertexArray(0);
  m_ready = true;
  return true;
}

void BoxRenderer::setInstances(const QVector<Instance>& v) {
  if (!m_ready) return;
  glBindBuffer(GL_ARRAY_BUFFER, m_inst);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Instance), v.constData(), GL_STATIC_DRAW);
  m_count = int(v.size());
}

void BoxRenderer::pass(int which) {
  m_prog->setUniformValue("uPass", which);
  glDrawArraysInstanced(GL_TRIANGLES, 0, 36, m_count);
}

void BoxRenderer::draw(const QMatrix4x4& vp, int axis, float value, float ghost, bool cut) {
  if (!m_ready || m_count == 0) return;
  m_prog->bind();
  m_prog->setUniformValue("uVP", vp);
  m_prog->setUniformValue("uSlice", QVector4D(float(axis), value, ghost, cut ? 1.0f : 0.0f));
  glBindVertexArray(m_vao);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  pass(0);
  // see-through cells after the opaque ones, without writing depth
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  pass(1);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glBindVertexArray(0);
  m_prog->release();
  // QPainter draws the labels next, on the default state: no depth test
  glDisable(GL_DEPTH_TEST);
}

void BoxRenderer::release() {
  if (!m_ready) return;
  glDeleteBuffers(1, &m_inst); glDeleteBuffers(1, &m_mesh); glDeleteVertexArrays(1, &m_vao);
  m_prog.reset();
  m_ready = false;
}

// ------------------------------------------------------------------------------------------------ camera
QVector3D OrbitCamera::eye() const {
  const float y = qDegreesToRadians(yaw), p = qDegreesToRadians(pitch);
  return target + dist * QVector3D(std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y));
}
QMatrix4x4 OrbitCamera::view() const { QMatrix4x4 m; m.lookAt(eye(), target, {0, 1, 0}); return m; }
QMatrix4x4 OrbitCamera::proj(const QSize& s) const {
  QMatrix4x4 m; m.perspective(fov, float(s.width()) / qMax(1, s.height()), qMax(0.05f, dist * 0.01f), dist * 40 + 5000);
  return m;
}
void OrbitCamera::ray(const QPointF& pos, const QSize& s, QVector3D* origin, QVector3D* dir) const {
  const QMatrix4x4 inv = (proj(s) * view()).inverted();
  const float x = float(2.0 * pos.x() / s.width() - 1.0), y = float(1.0 - 2.0 * pos.y() / s.height());
  const QVector3D a = inv.map(QVector3D(x, y, -1)), b = inv.map(QVector3D(x, y, 1));
  *origin = a; *dir = (b - a).normalized();
}
bool OrbitCamera::project(const QVector3D& p, const QSize& s, QPointF* out) const {
  const QVector4D c = proj(s) * view() * QVector4D(p, 1);
  if (c.w() <= 0.001f) return false;
  *out = QPointF((c.x() / c.w() + 1) * 0.5 * s.width(), (1 - c.y() / c.w()) * 0.5 * s.height());
  return true;
}
}  // namespace macrodata3d
