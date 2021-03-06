#include <babylon/lights/spot_light.h>

#include <babylon/cameras/camera.h>
#include <babylon/materials/effect.h>
#include <babylon/math/axis.h>

namespace BABYLON {

SpotLight::SpotLight(const std::string& iName, const Vector3& iPosition,
                     const Vector3& iDirection, float iAngle, float iExponent,
                     Scene* scene)
    : Light{iName, scene}
    , position{iPosition}
    , direction{iDirection}
    , angle{iAngle}
    , exponent{iExponent}
    , _transformedDirection{nullptr}
    , _worldMatrix{nullptr}
{
}

SpotLight::~SpotLight()
{
}

IReflect::Type SpotLight::type() const
{
  return IReflect::Type::SPOTLIGHT;
}

Scene* SpotLight::getScene()
{
  return Node::getScene();
}

Vector3 SpotLight::getAbsolutePosition()
{
  return transformedPosition ? *transformedPosition : position;
}

void SpotLight::setShadowProjectionMatrix(
  Matrix& matrix, const Matrix& /*viewMatrix*/,
  const std::vector<AbstractMesh*>& /*renderList*/)
{
  auto activeCamera = getScene()->activeCamera;
  Matrix::PerspectiveFovLHToRef(angle, 1.f, activeCamera->minZ,
                                activeCamera->maxZ, matrix);
}

bool SpotLight::needCube() const
{
  return false;
}

bool SpotLight::supportsVSM() const
{
  return true;
}

bool SpotLight::needRefreshPerFrame() const
{
  return false;
}

Vector3 SpotLight::getShadowDirection(unsigned int /*faceIndex*/)
{
  return direction;
}

Vector3& SpotLight::setDirectionToTarget(Vector3& target)
{
  direction = Vector3::Normalize(target.subtract(position));
  return direction;
}

bool SpotLight::computeTransformedPosition()
{
  if (parent() && parent()->getWorldMatrix()) {
    if (!transformedPosition) {
      transformedPosition = std_util::make_unique<Vector3>(Vector3::Zero());
    }

    Vector3::TransformCoordinatesToRef(position, *parent()->getWorldMatrix(),
                                       *transformedPosition);
    return true;
  }

  return false;
}

void SpotLight::transferToEffect(Effect* effect,
                                 const std::string& positionUniformName,
                                 const std::string& directionUniformName)
{
  auto normalizeDirection = Vector3::Zero();

  if (parent() && parent()->getWorldMatrix()) {
    if (!_transformedDirection) {
      transformedPosition = std_util::make_unique<Vector3>(Vector3::Zero());
    }

    computeTransformedPosition();

    Vector3::TransformNormalToRef(direction, *parent()->getWorldMatrix(),
                                  *_transformedDirection);

    effect->setFloat4(positionUniformName, transformedPosition->x,
                      transformedPosition->y, transformedPosition->z, exponent);
    normalizeDirection = Vector3::Normalize(*_transformedDirection);
  }
  else {
    effect->setFloat4(positionUniformName, position.x, position.y, position.z,
                      exponent);
    normalizeDirection = Vector3::Normalize(direction);
  }

  effect->setFloat4(directionUniformName, normalizeDirection.x,
                    normalizeDirection.y, normalizeDirection.z,
                    std::cos(angle * 0.5f));
}

Matrix* SpotLight::_getWorldMatrix()
{
  if (!_worldMatrix) {
    _worldMatrix = std_util::make_unique<Matrix>(Matrix::Identity());
  }

  Matrix::TranslationToRef(position.x, position.y, position.z, *_worldMatrix);

  return _worldMatrix.get();
}

unsigned int SpotLight::getTypeID() const
{
  return 2;
}

Vector3 SpotLight::getRotation()
{

  direction.normalize();

  Vector3 xaxis = Vector3::Cross(direction, Axis::Y);
  Vector3 yaxis = Vector3::Cross(xaxis, direction);

  return Vector3::RotationFromAxis(xaxis, yaxis, direction);
}

} // end of namespace BABYLON
