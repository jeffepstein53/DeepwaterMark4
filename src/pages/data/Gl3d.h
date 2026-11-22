#pragma once
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>
#include <QVector>
#include <QPointF>
#include <QSize>
#include <memory>
#include <functional>
#include <QWidget>
#include <QPainter>

// Shared 3D pieces of the Facts views (landscape, macro cube): thousands of boxes drawn in one instanced call on
// OpenGL 3.3 core (Metal underneath on macOS), simple directional light, an opaque and a see-through pass, an
// optional "slice" that keeps one plane of cells bright and fades the others, and an orbit camera with picking rays.
namespace macrodata3d {

// The 3D views' context: OpenGL ES 3.0 on iOS, 3.3 core elsewhere (depth 24, 4 samples).
QSurfaceFormat surfaceFormat();

struct Instance {
  float px, py, pz;      // centre of the base (x, z) and base height (y)
  float sx, sy, sz;      // size
  float r, g, b, a;      // colour (a < 0.9: drawn in the see-through pass)
  float gi, gj, gk;      // grid coordinates the slice tests (e.g. month, country, indicator)
  float span;            // extent along gi (months covered: 12 for an annual value)
};

class BoxRenderer : protected QOpenGLExtraFunctions {
public:
  ~BoxRenderer();
  bool init();                                   // in initializeGL
  void setInstances(const QVector<Instance>& v); // uploads (call with the context current)
  int count() const { return m_count; }
  // slice: axis -1 none, 0 gi, 1 gj, 2 gk; value; alpha kept outside the slice (1 = no fading);
  // cut: cells beyond the slice (higher coordinate, the camera's side) are not drawn
  void draw(const QMatrix4x4& viewProj, int sliceAxis = -1, float sliceValue = 0, float ghost = 1.0f, bool cut = false);
  void release();
private:
  void pass(int which);
  std::unique_ptr<QOpenGLShaderProgram> m_prog;
  unsigned m_vao = 0, m_mesh = 0, m_inst = 0;
  int m_count = 0;
  bool m_ready = false;
};

// Labels over a 3D view: a transparent child widget (QPainter inside paintGL is not reliable over a core-profile
// frame), repainted with the view.
class Overlay : public QWidget {
public:
  std::function<void(QPainter&)> paint;
  explicit Overlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents); setAttribute(Qt::WA_NoSystemBackground); setAttribute(Qt::WA_TranslucentBackground);
  }
protected:
  void paintEvent(QPaintEvent*) override { QPainter p(this); p.setRenderHint(QPainter::Antialiasing); if (paint) paint(p); }
};

struct OrbitCamera {
  QVector3D target{0, 0, 0};
  float yaw = -35, pitch = 32, dist = 120;     // degrees, world units
  float fov = 40;
  QVector3D eye() const;
  QMatrix4x4 view() const;
  QMatrix4x4 proj(const QSize& s) const;
  // ray through a widget position: origin and normalised direction
  void ray(const QPointF& pos, const QSize& s, QVector3D* origin, QVector3D* dir) const;
  // screen position of a world point (false when behind the camera)
  bool project(const QVector3D& p, const QSize& s, QPointF* out) const;
};
}  // namespace macrodata3d
