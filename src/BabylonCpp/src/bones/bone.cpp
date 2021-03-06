#include <babylon/bones/bone.h>

#include <babylon/animations/animation.h>
#include <babylon/bones/skeleton.h>
#include <babylon/math/tmp.h>
#include <babylon/mesh/abstract_mesh.h>

namespace BABYLON {

Bone::Bone(const std::string& _name, Skeleton* skeleton, Bone* parentBone,
           const Matrix& matrix)
    : Bone(_name, skeleton, parentBone, matrix, matrix)
{
}

Bone::Bone(const std::string& _name, Skeleton* skeleton, Bone* parentBone,
           const Matrix& matrix, const Matrix& restPose)
    : Node{_name, skeleton->getScene()}
    , _matrix{matrix}
    , length{-1}
    , _skeleton{skeleton}
    , _restPose{restPose}
    , _baseMatrix{_matrix}
    , _worldTransform{std_util::make_unique<Matrix>()}
    , _invertedAbsoluteTransform{std_util::make_unique<Matrix>()}
    , _scaleMatrix{Matrix::Identity()}
    , _scaleVector{Vector3(1.f, 1.f, 1.f)}
    , _negateScaleChildren{Vector3(1.f, 1.f, 1.f)}
    , _scalingDeterminant{1.f}
{
  if (parentBone) {
    _parent = parentBone;
    parentBone->children.emplace_back(this);
  }
  else {
    _parent = nullptr;
  }

  _updateDifferenceMatrix();

  if (getAbsoluteTransform().determinant() < 0.f) {
    _scalingDeterminant *= -1.f;
  }
}

Bone::~Bone()
{
}

IReflect::Type Bone::type() const
{
  return IReflect::Type::BONE;
}

void Bone::addToSkeleton(std::unique_ptr<Bone>&& newBone)
{
  _skeleton->bones.emplace_back(std::move(newBone));
}

// Members
Bone* Bone::getParent()
{
  return _parent;
}

Matrix& Bone::getLocalMatrix()
{
  return _matrix;
}

const Matrix& Bone::getLocalMatrix() const
{
  return _matrix;
}

Matrix& Bone::getBaseMatrix()
{
  return _baseMatrix;
}

Matrix& Bone::getRestPose()
{
  return _restPose;
}

void Bone::returnToRest()
{
  updateMatrix(_restPose);
}

Matrix* Bone::getWorldMatrix()
{
  return _worldTransform.get();
}

Matrix& Bone::getInvertedAbsoluteTransform()
{
  return *_invertedAbsoluteTransform;
}

Matrix& Bone::getAbsoluteTransform()
{
  return _absoluteTransform;
}

const Matrix& Bone::getAbsoluteTransform() const
{
  return _absoluteTransform;
}

// Methods
std::vector<Animation*> Bone::getAnimations()
{
  return animations;
}

void Bone::updateMatrix(const Matrix& matrix, bool updateDifferenceMatrix)
{
  _baseMatrix = matrix;
  _matrix     = matrix;

  _skeleton->_markAsDirty();

  if (updateDifferenceMatrix) {
    _updateDifferenceMatrix();
  }
}

void Bone::_updateDifferenceMatrix()
{
  _updateDifferenceMatrix(_baseMatrix);
}

void Bone::_updateDifferenceMatrix(Matrix& rootMatrix)
{
  if (_parent) {
    rootMatrix.multiplyToRef(_parent->getAbsoluteTransform(),
                             _absoluteTransform);
  }
  else {
    _absoluteTransform.copyFrom(rootMatrix);
  }

  _absoluteTransform.invertToRef(*_invertedAbsoluteTransform);

  for (auto& child : children) {
    child->_updateDifferenceMatrix();
  }
}

void Bone::markAsDirty(const std::string& /*property*/)
{
  ++_currentRenderId;
  _skeleton->_markAsDirty();
}

bool Bone::copyAnimationRange(Bone* source, const std::string& rangeName,
                              int frameOffset, bool rescaleAsRequired,
                              const Vector3& skelDimensionsRatio,
                              bool hasSkelDimensionsRatio)
{
  // all animation may be coming from a library skeleton, so may need to create
  // animation
  /*if (animations.empty()) {
    animations.emplace_back(std::make_shared<Animation>(
      name, "_matrix", source->animations[0]->framePerSecond,
      Animation::ANIMATIONTYPE_MATRIX, 0));
  }*/

  // get animation info / verify there is such a range from the source bone
  if (source->animations.empty() && !source->animations[0]) {
    return false;
  }

  const auto& sourceRange = source->animations[0]->getRange(rangeName);
  int from                = sourceRange.from;
  int to                  = sourceRange.to;
  const auto& sourceKeys  = source->animations[0]->getKeys();

  // rescaling prep
  int sourceBoneLength   = source->length;
  auto sourceParent      = source->getParent();
  auto parent            = getParent();
  bool parentScalingReqd = rescaleAsRequired && sourceParent
                           && sourceBoneLength > 0 && length > 0
                           && sourceBoneLength != length;
  float parentRatio = parentScalingReqd ?
                        static_cast<float>(parent->length)
                          / static_cast<float>(sourceParent->length) :
                        0.f;

  bool dimensionsScalingReqd
    = rescaleAsRequired && !parent && hasSkelDimensionsRatio
      && (!std_util::almost_equal(skelDimensionsRatio.x, 1.f)
          || !std_util::almost_equal(skelDimensionsRatio.y, 1.f)
          || !std_util::almost_equal(skelDimensionsRatio.z, 1.f));

  auto& destKeys = animations[0]->getKeys();

  // loop vars declaration
  Vector3 origTranslation;
  Matrix mat;

  for (const auto& orig : sourceKeys) {
    if (orig.frame >= from && orig.frame <= to) {
      if (rescaleAsRequired) {
        mat = orig.value.matrixData;

        // scale based on parent ratio, when bone has parent
        if (parentScalingReqd) {
          origTranslation = mat.getTranslation();
          mat.setTranslation(origTranslation.scaleInPlace(parentRatio));

          // scale based on skeleton dimension ratio when root bone, and value
          // is passed
        }
        else if (dimensionsScalingReqd) {
          origTranslation = mat.getTranslation();
          mat.setTranslation(
            origTranslation.multiplyInPlace(skelDimensionsRatio));

          // use original when root bone, and no data for skelDimensionsRatio
        }
        else {
          mat = orig.value.matrixData;
        }
      }
      else {
        mat = orig.value.matrixData;
      }
      destKeys.emplace_back(AnimationKey(orig.frame + frameOffset, mat));
    }
  }
  animations[0]->createRange(rangeName, from + frameOffset, to + frameOffset);
  return true;
}

void Bone::translate(const Vector3& vec, Space space, AbstractMesh* mesh)
{
  auto& lm = getLocalMatrix();

  if (space == Space::LOCAL) {

    lm.m[12] += vec.x;
    lm.m[13] += vec.y;
    lm.m[14] += vec.z;
  }
  else {

    _skeleton->computeAbsoluteTransforms();
    auto& tmat = Tmp::MatrixArray[0];
    auto& tvec = Tmp::Vector3Array[0];

    if (mesh) {
      tmat.copyFrom(_parent->getAbsoluteTransform());
      tmat.multiplyToRef(*mesh->getWorldMatrix(), tmat);
    }
    else {
      tmat.copyFrom(_parent->getAbsoluteTransform());
    }

    tmat.m[12] = 0;
    tmat.m[13] = 0;
    tmat.m[14] = 0;

    tmat.invert();
    Vector3::TransformCoordinatesToRef(vec, tmat, tvec);

    lm.m[12] += tvec.x;
    lm.m[13] += tvec.y;
    lm.m[14] += tvec.z;
  }

  markAsDirty();
}

void Bone::setPosition(const Vector3& position, Space space, AbstractMesh* mesh)
{
  auto& lm = getLocalMatrix();

  if (space == Space::LOCAL) {

    lm.m[12] = position.x;
    lm.m[13] = position.y;
    lm.m[14] = position.z;
  }
  else {

    _skeleton->computeAbsoluteTransforms();

    auto& tmat = Tmp::MatrixArray[0];
    auto& vec  = Tmp::Vector3Array[0];

    if (mesh) {
      tmat.copyFrom(_parent->getAbsoluteTransform());
      tmat.multiplyToRef(*mesh->getWorldMatrix(), tmat);
    }
    else {
      tmat.copyFrom(_parent->getAbsoluteTransform());
    }

    tmat.invert();
    Vector3::TransformCoordinatesToRef(position, tmat, vec);

    lm.m[12] = vec.x;
    lm.m[13] = vec.y;
    lm.m[14] = vec.z;
  }

  markAsDirty();
}

void Bone::setAbsolutePosition(const Vector3& position, AbstractMesh* mesh)
{
  setPosition(position, Space::WORLD, mesh);
}

void Bone::setScale(float x, float y, float z, bool scaleChildren)
{
  if (!animations.empty() && animations[0] && !animations[0]->isStopped()) {
    if (!scaleChildren) {
      _negateScaleChildren.x = 1.f / x;
      _negateScaleChildren.y = 1.f / y;
      _negateScaleChildren.z = 1.f / z;
    }
    _syncScaleVector();
  }

  scale(x / _scaleVector.x, y / _scaleVector.y, z / _scaleVector.z,
        scaleChildren);
}

void Bone::scale(float x, float y, float z, bool scaleChildren)
{
  auto& locMat     = getLocalMatrix();
  auto& origLocMat = Tmp::MatrixArray[0];
  origLocMat.copyFrom(locMat);

  auto& origLocMatInv = Tmp::MatrixArray[1];
  origLocMatInv.copyFrom(origLocMat);
  origLocMatInv.invert();

  auto& scaleMat = Tmp::MatrixArray[2];
  Matrix::FromValuesToRef(x, 0, 0, 0, // M11-M14
                          0, y, 0, 0, // M21-M24
                          0, 0, z, 0, // M31-M34
                          0, 0, 0, 1, // M41-M44
                          scaleMat);
  _scaleMatrix.multiplyToRef(scaleMat, _scaleMatrix);
  _scaleVector.x *= x;
  _scaleVector.y *= y;
  _scaleVector.z *= z;

  locMat.multiplyToRef(origLocMatInv, locMat);
  locMat.multiplyToRef(scaleMat, locMat);
  locMat.multiplyToRef(origLocMat, locMat);

  auto parent = getParent();

  if (parent) {
    locMat.multiplyToRef(parent->getAbsoluteTransform(),
                         getAbsoluteTransform());
  }
  else {
    getAbsoluteTransform().copyFrom(locMat);
  }

  scaleMat.invert();

  for (auto& child : children) {
    auto& cm = child->getLocalMatrix();
    cm.multiplyToRef(scaleMat, cm);
    auto& lm = child->getLocalMatrix();
    lm.m[12] *= x;
    lm.m[13] *= y;
    lm.m[14] *= z;
  }

  computeAbsoluteTransforms();

  if (scaleChildren) {
    for (auto& child : children) {
      child->scale(x, y, z, scaleChildren);
    }
  }

  markAsDirty();
}

void Bone::setYawPitchRoll(float yaw, float pitch, float roll, Space space,
                           AbstractMesh* mesh)
{
  auto& rotMat = Tmp::MatrixArray[0];
  Matrix::RotationYawPitchRollToRef(yaw, pitch, roll, rotMat);

  auto& rotMatInv = Tmp::MatrixArray[1];

  _getNegativeRotationToRef(rotMatInv, space, mesh);

  rotMatInv.multiplyToRef(rotMat, rotMat);

  _rotateWithMatrix(rotMat, space, mesh);
}

void Bone::rotate(Vector3& axis, float amount, Space space, AbstractMesh* mesh)
{
  auto& rmat = Tmp::MatrixArray[0];
  rmat.m[12] = 0;
  rmat.m[13] = 0;
  rmat.m[14] = 0;

  Matrix::RotationAxisToRef(axis, amount, rmat);

  _rotateWithMatrix(rmat, space, mesh);
}

void Bone::setAxisAngle(Vector3& axis, float angle, Space space,
                        AbstractMesh* mesh)
{
  auto& rotMat = Tmp::MatrixArray[0];
  Matrix::RotationAxisToRef(axis, angle, rotMat);
  auto& rotMatInv = Tmp::MatrixArray[1];

  _getNegativeRotationToRef(rotMatInv, space, mesh);

  rotMatInv.multiplyToRef(rotMat, rotMat);
  _rotateWithMatrix(rotMat, space, mesh);
}

void Bone::setRotationMatrix(const Matrix& rotMat, Space space,
                             AbstractMesh* mesh)
{
  auto& rotMatInv = Tmp::MatrixArray[0];

  _getNegativeRotationToRef(rotMatInv, space, mesh);

  auto& rotMat2 = Tmp::MatrixArray[1];
  rotMat2.copyFrom(rotMat);

  rotMatInv.multiplyToRef(rotMat, rotMat2);

  _rotateWithMatrix(rotMat2, space, mesh);
}

void Bone::_rotateWithMatrix(const Matrix& rmat, Space space,
                             AbstractMesh* mesh)
{
  auto& lmat           = getLocalMatrix();
  float lx             = lmat.m[12];
  float ly             = lmat.m[13];
  float lz             = lmat.m[14];
  auto parent          = getParent();
  auto& parentScale    = Tmp::MatrixArray[3];
  auto& parentScaleInv = Tmp::MatrixArray[4];

  if (parent) {
    if (space == Space::WORLD) {
      if (mesh) {
        parentScale.copyFrom(*mesh->getWorldMatrix());
        parent->getAbsoluteTransform().multiplyToRef(parentScale, parentScale);
      }
      else {
        parentScale.copyFrom(parent->getAbsoluteTransform());
      }
    }
    else {
      parentScale = parent->_scaleMatrix;
    }
    parentScaleInv.copyFrom(parentScale);
    parentScaleInv.invert();
    lmat.multiplyToRef(parentScale, lmat);
    lmat.multiplyToRef(rmat, lmat);
    lmat.multiplyToRef(parentScaleInv, lmat);
  }
  else {
    if (space == Space::WORLD && mesh) {
      parentScale.copyFrom(*mesh->getWorldMatrix());
      parentScaleInv.copyFrom(parentScale);
      parentScaleInv.invert();
      lmat.multiplyToRef(parentScale, lmat);
      lmat.multiplyToRef(rmat, lmat);
      lmat.multiplyToRef(parentScaleInv, lmat);
    }
    else {
      lmat.multiplyToRef(rmat, lmat);
    }
  }

  lmat.m[12] = lx;
  lmat.m[13] = ly;
  lmat.m[14] = lz;

  computeAbsoluteTransforms();

  markAsDirty();
}

void Bone::_getNegativeRotationToRef(Matrix& rotMatInv, Space space,
                                     AbstractMesh* mesh)
{
  if (space == Space::WORLD) {
    auto& scaleMatrix = Tmp::MatrixArray[2];
    scaleMatrix.copyFrom(_scaleMatrix);
    rotMatInv.copyFrom(getAbsoluteTransform());

    if (mesh) {
      rotMatInv.multiplyToRef(*mesh->getWorldMatrix(), rotMatInv);
      auto& meshScale = Tmp::MatrixArray[3];
      Matrix::ScalingToRef(mesh->scaling().x, mesh->scaling().y,
                           mesh->scaling().z, meshScale);
      scaleMatrix.multiplyToRef(meshScale, scaleMatrix);
    }

    rotMatInv.invert();
    scaleMatrix.m[0] *= _scalingDeterminant;
    rotMatInv.multiplyToRef(scaleMatrix, rotMatInv);
  }
  else {
    rotMatInv.copyFrom(getLocalMatrix());
    rotMatInv.invert();
    auto& scaleMatrix = Tmp::MatrixArray[2];
    scaleMatrix.copyFrom(_scaleMatrix);

    if (_parent) {
      auto& pscaleMatrix = Tmp::MatrixArray[3];
      pscaleMatrix.copyFrom(_parent->_scaleMatrix);
      pscaleMatrix.invert();
      pscaleMatrix.multiplyToRef(rotMatInv, rotMatInv);
    }
    else {
      scaleMatrix.m[0] *= _scalingDeterminant;
    }

    rotMatInv.multiplyToRef(scaleMatrix, rotMatInv);
  }
}

Vector3 Bone::getScale() const
{
  return _scaleVector;
}

void Bone::getScaleToRef(Vector3& result) const
{
  result.copyFrom(_scaleVector);
}

Vector3 Bone::getPosition(Space space, AbstractMesh* mesh) const
{
  auto pos = Vector3::Zero();

  getPositionToRef(pos, space, mesh);

  return pos;
}

void Bone::getPositionToRef(Vector3& result, Space space,
                            AbstractMesh* mesh) const
{
  if (space == Space::LOCAL) {

    auto& lm = getLocalMatrix();

    result.x = lm.m[12];
    result.y = lm.m[13];
    result.z = lm.m[14];
  }
  else {

    _skeleton->computeAbsoluteTransforms();

    auto& tmat = Tmp::MatrixArray[0];

    if (mesh) {
      tmat.copyFrom(getAbsoluteTransform());
      tmat.multiplyToRef(*mesh->getWorldMatrix(), tmat);
    }
    else {
      tmat = getAbsoluteTransform();
    }

    result.x = tmat.m[12];
    result.y = tmat.m[13];
    result.z = tmat.m[14];
  }
}

Vector3 Bone::getAbsolutePosition(AbstractMesh* mesh) const
{
  auto pos = Vector3::Zero();

  getPositionToRef(pos, Space::WORLD, mesh);

  return pos;
}

void Bone::getAbsolutePositionToRef(AbstractMesh* mesh, Vector3& result) const
{
  getPositionToRef(result, Space::WORLD, mesh);
}

void Bone::computeAbsoluteTransforms()
{
  if (_parent) {
    _matrix.multiplyToRef(_parent->_absoluteTransform, _absoluteTransform);
  }
  else {
    _absoluteTransform.copyFrom(_matrix);

    auto poseMatrix = _skeleton->getPoseMatrix();

    if (poseMatrix) {
      _absoluteTransform.multiplyToRef(*poseMatrix, _absoluteTransform);
    }
  }

  for (auto& child : children) {
    child->computeAbsoluteTransforms();
  }
}

void Bone::_syncScaleVector()
{
  const auto& lm = getLocalMatrix();

  float xsq = (lm.m[0] * lm.m[0] + lm.m[1] * lm.m[1] + lm.m[2] * lm.m[2]);
  float ysq = (lm.m[4] * lm.m[4] + lm.m[5] * lm.m[5] + lm.m[6] * lm.m[6]);
  float zsq = (lm.m[8] * lm.m[8] + lm.m[9] * lm.m[9] + lm.m[10] * lm.m[10]);

  float xs = lm.m[0] * lm.m[1] * lm.m[2] * lm.m[3] < 0 ? -1 : 1;
  float ys = lm.m[4] * lm.m[5] * lm.m[6] * lm.m[7] < 0 ? -1 : 1;
  float zs = lm.m[8] * lm.m[9] * lm.m[10] * lm.m[11] < 0 ? -1 : 1;

  _scaleVector.x = xs * std::sqrt(xsq);
  _scaleVector.y = ys * std::sqrt(ysq);
  _scaleVector.z = zs * std::sqrt(zsq);

  if (_parent) {
    _scaleVector.x /= _parent->_negateScaleChildren.x;
    _scaleVector.y /= _parent->_negateScaleChildren.y;
    _scaleVector.z /= _parent->_negateScaleChildren.z;
  }

  Matrix::FromValuesToRef(_scaleVector.x, 0.f, 0.f, 0.f, // M11-M14
                          0.f, _scaleVector.y, 0.f, 0.f, // M21-M24
                          0.f, 0.f, _scaleVector.z, 0.f, // M31-M34
                          0.f, 0.f, 0.f, 1.f,            // M41-M44
                          _scaleMatrix);
}

Vector3 Bone::getDirection(const Vector3& localAxis, AbstractMesh* mesh)
{
  auto result = Vector3::Zero();

  getDirectionToRef(localAxis, result, mesh);

  return result;
}

void Bone::getDirectionToRef(const Vector3& localAxis, Vector3& result,
                             AbstractMesh* mesh)
{
  _skeleton->computeAbsoluteTransforms();

  auto& mat = Tmp::MatrixArray[0];

  mat.copyFrom(getAbsoluteTransform());

  if (mesh) {
    mat.multiplyToRef(*mesh->getWorldMatrix(), mat);
  }

  Vector3::TransformNormalToRef(localAxis, mat, result);

  result.normalize();
}

Quaternion Bone::getRotation(Space space, AbstractMesh* mesh)
{
  auto result = Quaternion::Identity();

  getRotationToRef(result, space, mesh);

  return result;
}

void Bone::getRotationToRef(Quaternion& result, Space space, AbstractMesh* mesh)
{
  if (space == Space::LOCAL) {

    getLocalMatrix().decompose(Tmp::Vector3Array[0], result,
                               Tmp::Vector3Array[1]);
  }
  else {

    auto& mat  = Tmp::MatrixArray[0];
    auto& amat = getAbsoluteTransform();

    if (mesh) {

      auto wmat = mesh->getWorldMatrix();
      amat.multiplyToRef(*wmat, mat);

      mat.decompose(Tmp::Vector3Array[0], result, Tmp::Vector3Array[1]);
    }
    else {

      amat.decompose(Tmp::Vector3Array[0], result, Tmp::Vector3Array[1]);
    }
  }
}

} // end of namespace BABYLON
