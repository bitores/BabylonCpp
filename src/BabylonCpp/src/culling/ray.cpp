#include <babylon/culling/ray.h>

#include <babylon/collisions/intersection_info.h>
#include <babylon/culling/bounding_box.h>
#include <babylon/culling/bounding_sphere.h>
#include <babylon/math/plane.h>

namespace BABYLON {

const float Ray::smallnum = 0.00000001f;
const float Ray::rayl     = 10e8f;

Ray::Ray(const Vector3& _origin, const Vector3& _direction, float _length)
    : origin{_origin}
    , direction{_direction}
    , length{_length}
    , _vectorsSet{false}
{
}

Ray::Ray(const Ray& otherRay)
    : origin{otherRay.origin}
    , direction{otherRay.direction}
    , length{otherRay.length}
    , _vectorsSet{otherRay._vectorsSet}
    , _edge1{otherRay._edge1}
    , _edge2{otherRay._edge2}
    , _pvec{otherRay._pvec}
    , _tvec{otherRay._tvec}
    , _qvec{otherRay._qvec}
{
}

Ray::Ray(Ray&& otherRay)
{
  *this = std::move(otherRay);
}

Ray& Ray::operator=(const Ray& otherRay)
{
  if (&otherRay != this) {
    origin      = otherRay.origin;
    direction   = otherRay.direction;
    length      = otherRay.length;
    _vectorsSet = otherRay._vectorsSet;
    _edge1      = otherRay._edge1;
    _edge2      = otherRay._edge2;
    _pvec       = otherRay._pvec;
    _tvec       = otherRay._tvec;
    _qvec       = otherRay._qvec;
  }

  return *this;
}

Ray& Ray::operator=(Ray&& otherRay)
{
  if (&otherRay != this) {
    std::swap(origin, otherRay.origin);
    std::swap(direction, otherRay.direction);
    std::swap(length, otherRay.length);
    std::swap(_vectorsSet, otherRay._vectorsSet);
    std::swap(_edge1, otherRay._edge1);
    std::swap(_edge2, otherRay._edge2);
    std::swap(_pvec, otherRay._pvec);
    std::swap(_tvec, otherRay._tvec);
    std::swap(_qvec, otherRay._qvec);
  }

  return *this;
}

Ray::~Ray()
{
}

Ray* Ray::clone() const
{
  return new Ray(*this);
}

std::ostream& operator<<(std::ostream& os, const Ray& ray)
{
  os << "{\"Origin\":" << ray.origin << ",\"Direction\":" << ray.direction
     << ",\"Length\":" << ray.length << "}";
  return os;
}

// Methods
bool Ray::intersectsBoxMinMax(const Vector3& minimum,
                              const Vector3& maximum) const
{
  float d        = 0.f;
  float maxValue = std::numeric_limits<float>::max();
  float inv      = 0.f;
  float min      = 0.f;
  float max      = 0.f;
  if (std::abs(direction.x) < 0.0000001f) {
    if (origin.x < minimum.x || origin.x > maximum.x) {
      return false;
    }
  }
  else {
    inv = 1.f / direction.x;
    min = (minimum.x - origin.x) * inv;
    max = (maximum.x - origin.x) * inv;
    if (std_util::almost_equal(max, -std::numeric_limits<float>::infinity())) {
      max = std::numeric_limits<float>::infinity();
    }

    if (min > max) {
      std::swap(min, max);
    }

    d        = std::max(min, d);
    maxValue = std::min(max, maxValue);

    if (d > maxValue) {
      return false;
    }
  }

  if (std::abs(direction.y) < 0.0000001f) {
    if (origin.y < minimum.y || origin.y > maximum.y) {
      return false;
    }
  }
  else {
    inv = 1.f / direction.y;
    min = (minimum.y - origin.y) * inv;
    max = (maximum.y - origin.y) * inv;

    if (std_util::almost_equal(max, -std::numeric_limits<float>::infinity())) {
      max = std::numeric_limits<float>::infinity();
    }

    if (min > max) {
      std::swap(min, max);
    }

    d        = std::max(min, d);
    maxValue = std::min(max, maxValue);

    if (d > maxValue) {
      return false;
    }
  }

  if (std::abs(direction.z) < 0.0000001f) {
    if (origin.z < minimum.z || origin.z > maximum.z) {
      return false;
    }
  }
  else {
    inv = 1.f / direction.z;
    min = (minimum.z - origin.z) * inv;
    max = (maximum.z - origin.z) * inv;

    if (std_util::almost_equal(max, -std::numeric_limits<float>::infinity())) {
      max = std::numeric_limits<float>::infinity();
    }

    if (min > max) {
      std::swap(min, max);
    }

    d        = std::max(min, d);
    maxValue = std::min(max, maxValue);

    if (d > maxValue) {
      return false;
    }
  }
  return true;
}

bool Ray::intersectsBox(const BoundingBox& box) const
{
  return intersectsBoxMinMax(box.minimum, box.maximum);
}

bool Ray::intersectsSphere(const BoundingSphere& sphere) const
{
  float x    = sphere.center.x - origin.x;
  float y    = sphere.center.y - origin.y;
  float z    = sphere.center.z - origin.z;
  float pyth = (x * x) + (y * y) + (z * z);
  float rr   = sphere.radius * sphere.radius;

  if (pyth <= rr) {
    return true;
  }

  float dot = (x * direction.x) + (y * direction.y) + (z * direction.z);
  if (dot < 0.f) {
    return false;
  }

  float temp = pyth - (dot * dot);

  return temp <= rr;
}

std::unique_ptr<IntersectionInfo>
Ray::intersectsTriangle(const Vector3& vertex0, const Vector3& vertex1,
                        const Vector3& vertex2)
{
  if (!_vectorsSet) {
    _vectorsSet = true;
    _edge1      = Vector3::Zero();
    _edge2      = Vector3::Zero();
    _pvec       = Vector3::Zero();
    _tvec       = Vector3::Zero();
    _qvec       = Vector3::Zero();
  }

  vertex1.subtractToRef(vertex0, _edge1);
  vertex2.subtractToRef(vertex0, _edge2);
  Vector3::CrossToRef(direction, _edge2, _pvec);
  float det = Vector3::Dot(_edge1, _pvec);

  if (std_util::almost_equal(det, 0.f)) {
    return nullptr;
  }

  float invdet = 1.f / det;

  origin.subtractToRef(vertex0, _tvec);

  float bu = Vector3::Dot(_tvec, _pvec) * invdet;

  if (bu < 0.f || bu > 1.f) {
    return nullptr;
  }

  Vector3::CrossToRef(_tvec, _edge1, _qvec);

  float bv = Vector3::Dot(direction, _qvec) * invdet;

  if (bv < 0.f || bu + bv > 1.f) {
    return nullptr;
  }

  // check if the distance is longer than the predefined length.
  float distance = Vector3::Dot(_edge2, _qvec) * invdet;
  if (distance > length) {
    return nullptr;
  }

  return std_util::make_unique<IntersectionInfo>(bu, bv, distance);
}

std::unique_ptr<float> Ray::intersectsPlane(const Plane& plane)
{
  float distance;
  float result1 = Vector3::Dot(plane.normal, direction);
  if (std::abs(result1) < 9.99999997475243E-07f) {
    return nullptr;
  }
  else {
    float result2 = Vector3::Dot(plane.normal, origin);
    distance      = (-plane.d - result2) / result1;
    if (distance < 0.f) {
      if (distance < -9.99999997475243E-07f) {
        return nullptr;
      }
      else {
        return std_util::make_unique<float>(0.f);
      }
    }

    return std_util::make_unique<float>(distance);
  }
}

float Ray::intersectionSegment(const Vector3& sega, const Vector3& segb,
                               float threshold) const
{
  auto rsegb
    = origin.add(direction.multiplyByFloats(Ray::rayl, Ray::rayl, Ray::rayl));

  auto u   = segb.subtract(sega);
  auto v   = rsegb.subtract(origin);
  auto w   = sega.subtract(origin);
  float a  = Vector3::Dot(u, u); // always >= 0
  float b  = Vector3::Dot(u, v);
  float c  = Vector3::Dot(v, v); // always >= 0
  float d  = Vector3::Dot(u, w);
  float e  = Vector3::Dot(v, w);
  float D  = a * c - b * b;         // always >= 0
  float sc = 0.f, sN = 0.f, sD = D; // sc = sN / sD, default sD = D >= 0
  float tc = 0.f, tN = 0.f, tD = D; // tc = tN / tD, default tD = D >= 0

  // compute the line parameters of the two closest points
  if (D < Ray::smallnum) { // the lines are almost parallel
    sN = 0.f;              // force using point P0 on segment S1
    sD = 1.f;              // to prevent possible division by 0.0 later
    tN = e;
    tD = c;
  }
  else { // get the closest points on the infinite lines
    sN = (b * e - c * d);
    tN = (a * e - b * d);
    if (sN < 0.f) { // sc < 0 => the s=0 edge is visible
      sN = 0.f;
      tN = e;
      tD = c;
    }
    else if (sN > sD) { // sc > 1 => the s=1 edge is visible
      sN = sD;
      tN = e + b;
      tD = c;
    }
  }

  if (tN < 0.f) { // tc < 0 => the t=0 edge is visible
    tN = 0.f;
    // recompute sc for this edge
    if (-d < 0.f) {
      sN = 0.f;
    }
    else if (-d > a) {
      sN = sD;
    }
    else {
      sN = -d;
      sD = a;
    }
  }
  else if (tN > tD) { // tc > 1 => the t=1 edge is visible
    tN = tD;
    // recompute sc for this edge
    if ((-d + b) < 0.f) {
      sN = 0.f;
    }
    else if ((-d + b) > a) {
      sN = sD;
    }
    else {
      sN = (-d + b);
      sD = a;
    }
  }
  // finally do the division to get sc and tc
  sc = (std::abs(sN) < Ray::smallnum ? 0.f : sN / sD);
  tc = (std::abs(tN) < Ray::smallnum ? 0.f : tN / tD);

  // get the difference of the two closest points
  auto qtc = v.multiplyByFloats(tc, tc, tc);
  auto dP
    = w.add(u.multiplyByFloats(sc, sc, sc)).subtract(qtc); // = S1(sc) - S2(tc)

  // return intersection result
  bool isIntersected = (tc > 0) && (tc <= length)
                       && (dP.lengthSquared() < (threshold * threshold));

  if (isIntersected) {
    return qtc.length();
  }
  return -1;
}

Ray Ray::CreateNew(float x, float y, float viewportWidth, float viewportHeight,
                   Matrix& world, Matrix& view, Matrix& projection)
{
  Vector3 start = Vector3::Unproject(Vector3(x, y, 0.f), viewportWidth,
                                     viewportHeight, world, view, projection);
  Vector3 end = Vector3::Unproject(Vector3(x, y, 1.f), viewportWidth,
                                   viewportHeight, world, view, projection);

  Vector3 direction = end.subtract(start);
  direction.normalize();

  return Ray(start, direction);
}

Ray Ray::CreateNewFromTo(const Vector3& origin, const Vector3& end,
                         const Matrix& world)
{
  auto direction = end.subtract(origin);
  float length
    = std::sqrt((direction.x * direction.x) + (direction.y * direction.y)
                + (direction.z * direction.z));
  direction.normalize();

  return Ray::Transform(Ray(origin, direction, length), world);
}

Ray Ray::Transform(const Ray& ray, const Matrix& matrix)
{
  auto newOrigin    = Vector3::TransformCoordinates(ray.origin, matrix);
  auto newDirection = Vector3::TransformNormal(ray.direction, matrix);

  newDirection.normalize();

  return Ray(newOrigin, newDirection, ray.length);
}

} // end of namespace BABYLON
