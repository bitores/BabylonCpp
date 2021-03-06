#include <babylon/math/path2.h>

#include <babylon/math/arc2.h>
#include <babylon/math/vector2.h>

namespace BABYLON {

Path2::Path2(float x, float y) : closed{false}, _length{0.f}
{
  _points.emplace_back(Vector2(x, y));
}

Path2::Path2(const Path2& otherPath)
    : _points{otherPath._points}, _length{otherPath._length}
{
}

Path2::Path2(Path2&& otherPath)
    : _points{std::move(otherPath._points)}
    , _length{std::move(otherPath._length)}
{
}

Path2& Path2::operator=(const Path2& otherPath)
{
  if (&otherPath != this) {
    closed  = otherPath.closed;
    _points = otherPath._points;
    _length = otherPath._length;
  }

  return *this;
}

Path2& Path2::operator=(Path2&& otherPath)
{
  if (&otherPath != this) {
    std::swap(closed, otherPath.closed);
    std::swap(_points, otherPath._points);
    std::swap(_length, otherPath._length);
  }

  return *this;
}

Path2::~Path2()
{
}

Path2 Path2::copy() const
{
  return Path2(*this);
}

std::unique_ptr<Path2> Path2::clone() const
{
  return std_util::make_unique<Path2>(*this);
}

std::ostream& operator<<(std::ostream& os, const Path2& path)
{
  os << "{\"Points\":[";
  if (path._points.size() > 0) {
    for (unsigned int i = 0; i < path._points.size() - 1; ++i) {
      os << path._points[i] << ",";
    }
    os << path._points.back();
  }
  os << "],\"Length\":" << path._length << "}";
  return os;
}

Path2& Path2::addLineTo(float x, float y)
{
  if (closed) {
    return *this;
  }
  Vector2 newPoint(x, y);
  Vector2 previousPoint = _points.back();
  _points.emplace_back(newPoint);
  _length += newPoint.subtract(previousPoint).length();
  return *this;
}

Path2& Path2::addArcTo(float midX, float midY, float endX, float endY,
                       unsigned int numberOfSegments)
{
  if (closed) {
    return *this;
  }
  Vector2 startPoint = _points.back();
  Vector2 midPoint(midX, midY);
  Vector2 endPoint(endX, endY);

  Arc2 arc(startPoint, midPoint, endPoint);

  float increment = arc.angle.radians() / static_cast<float>(numberOfSegments);
  if (arc.orientation == Orientation::CW) {
    increment *= -1;
  }
  float currentAngle = arc.startAngle.radians() + increment;

  float x = 0.f, y = 0.f;
  for (unsigned int i = 0; i < numberOfSegments; ++i) {
    x = std::cos(currentAngle) * arc.radius + arc.centerPoint.x;
    y = std::sin(currentAngle) * arc.radius + arc.centerPoint.y;
    addLineTo(x, y);
    currentAngle += increment;
  }
  return *this;
}

Path2& Path2::close()
{
  closed = true;
  return *this;
}

float Path2::length() const
{
  float result = _length;

  if (!closed) {
    Vector2 lastPoint  = _points[_points.size() - 1];
    Vector2 firstPoint = _points[0];
    result += (firstPoint.subtract(lastPoint).length());
  }

  return result;
}

std::vector<Vector2>& Path2::getPoints()
{
  return _points;
}

const std::vector<Vector2>& Path2::getPoints() const
{
  return _points;
}

Vector2 Path2::getPointAtLengthPosition(float normalizedLengthPosition) const
{
  if (normalizedLengthPosition < 0 || normalizedLengthPosition > 1) {
    return Vector2::Zero();
  }

  float lengthPosition = normalizedLengthPosition * length();

  float previousOffset = 0;
  for (unsigned int i = 0; i < _points.size(); ++i) {
    size_t j = (i + 1) % _points.size();

    Vector2 a    = _points[i];
    Vector2 b    = _points[j];
    Vector2 bToA = b.subtract(a);

    float nextOffset = (bToA.length() + previousOffset);
    if (lengthPosition >= previousOffset && lengthPosition <= nextOffset) {
      Vector2 dir       = bToA.normalize();
      float localOffset = lengthPosition - previousOffset;

      return Vector2(a.x + (dir.x * localOffset), //
                     a.y + (dir.y * localOffset));
    }
    previousOffset = nextOffset;
  }

  return Vector2::Zero();
}

Path2 Path2::StartingAt(float x, float y)
{
  return Path2(x, y);
}

} // end of namespace BABYLON
