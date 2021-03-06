#include <babylon/particles/solid_particle_system.h>

#include <babylon/cameras/camera.h>
#include <babylon/cameras/target_camera.h>
#include <babylon/core/random.h>
#include <babylon/culling/bounding_info.h>
#include <babylon/engine/scene.h>
#include <babylon/math/axis.h>
#include <babylon/math/color4.h>
#include <babylon/math/tmp.h>
#include <babylon/mesh/mesh.h>
#include <babylon/mesh/mesh_builder.h>
#include <babylon/mesh/vertex_buffer.h>
#include <babylon/mesh/vertex_data.h>
#include <babylon/mesh/vertex_data_options.h>
#include <babylon/particles/model_shape.h>

namespace BABYLON {

SolidParticleSystem::SolidParticleSystem(
  const std::string& iName, Scene* scene,
  const SolidParticleSystemOptions& options)
    : nbParticles{0}
    , billboard{false}
    , recomputeNormals{true}
    , counter{0}
    , name{iName}
    , mesh{nullptr}
    , _bSphereOnly{options.boundingSphereOnly}
    , _bSphereRadiusFactor{options.bSphereRadiusFactor}
    , _scene{scene}
    , _index{0}
    , _updatable{options.updatable}
    , _pickable{options.isPickable}
    , _isVisibilityBoxLocked{false}
    , _alwaysVisible{false}
    , _shapeCounter{0}
    , _copy{new SolidParticle(0, 0, nullptr, 0, 0, nullptr)}
    , _color{new Color4(0.f, 0.f, 0.f, 0.f)}
    , _computeParticleColor{true}
    , _computeParticleTexture{true}
    , _computeParticleRotation{true}
    , _computeParticleVertex{false}
    , _computeBoundingBox{false}
    , _cam_axisZ{Vector3::Zero()}
    , _cam_axisY{Vector3::Zero()}
    , _cam_axisX{Vector3::Zero()}
    , _axisX{Axis::X}
    , _axisY{Axis::Y}
    , _axisZ{Axis::Z}
    , _camera{dynamic_cast<TargetCamera*>(scene->activeCamera)}
    , _camDir{Vector3::Zero()}
    , _rotated{Vector3::Zero()}
    , _vertex{Vector3::Zero()}
    , _normal{Vector3::Zero()}
    , _yaw{0.f}
    , _pitch{0.f}
    , _roll{0.f}
    , _halfroll{0.f}
    , _halfpitch{0.f}
    , _halfyaw{0.f}
    , _sinRoll{0.f}
    , _cosRoll{0.f}
    , _sinPitch{0.f}
    , _cosPitch{0.f}
    , _sinYaw{0.f}
    , _cosYaw{0.f}
    , _w{0.f}
    , _minimum{Tmp::Vector3Array[0]}
    , _maximum{Tmp::Vector3Array[1]}
    , _scale{Tmp::Vector3Array[2]}
    , _translation{Tmp::Vector3Array[3]}
    , _minBbox{Tmp::Vector3Array[4]}
    , _maxBbox{Tmp::Vector3Array[5]}
    , _particlesIntersect{options.particleIntersection}
{
}

SolidParticleSystem::~SolidParticleSystem()
{
}

Mesh* SolidParticleSystem::buildMesh()
{
  if (nbParticles == 0) {
    DiscOptions options;
    options.radius       = 1.f;
    options.tessellation = 3;
    Mesh* triangle       = MeshBuilder::CreateDisc("", options, _scene);
    // addShape(triangle, 1);
    triangle->dispose();
  }

  _positions32 = Float32Array(_positions);
  _uvs32       = Float32Array(_uvs);
  _colors32    = Float32Array(_colors);
  if (recomputeNormals) {
    VertexData::ComputeNormals(_positions32, _indices, _normals);
  }
  _normals32             = Float32Array(_normals);
  _fixedNormal32         = Float32Array(_normals);
  VertexData* vertexData = new VertexData();
  vertexData->set(_positions32, VertexBuffer::PositionKind);
  vertexData->indices = _indices;
  vertexData->set(_normals32, VertexBuffer::NormalKind);
  if (!_uvs32.empty()) {
    vertexData->set(_uvs32, VertexBuffer::UVKind);
  }
  if (!_colors32.empty()) {
    vertexData->set(_colors32, VertexBuffer::ColorKind);
  }
  auto _mesh = Mesh::New(name, _scene);
  vertexData->applyToMesh(_mesh, _updatable);
  mesh             = _mesh;
  mesh->isPickable = _pickable;

  // free memory
  _positions.clear();
  _normals.clear();
  _uvs.clear();
  _colors.clear();

  if (!_updatable) {
    particles.clear();
  }

  return _mesh;
}

SolidParticleSystem&
SolidParticleSystem::digest(Mesh* _mesh,
                            const SolidParticleSystemDigestOptions& options)
{
  size_t size          = options.facetNb;
  size_t number        = options.number;
  int delta            = options.delta;
  Float32Array meshPos = _mesh->getVerticesData(VertexBuffer::PositionKind);
  Uint32Array meshInd  = _mesh->getIndices();
  Float32Array meshUV  = _mesh->getVerticesData(VertexBuffer::UVKind);
  Float32Array meshCol = _mesh->getVerticesData(VertexBuffer::ColorKind);
  Float32Array meshNor = mesh->getVerticesData(VertexBuffer::NormalKind);

  size_t f = 0; // facet counter
  // a facet is a triangle, so 3 indices
  size_t totalFacets = meshInd.size() / 3;
  // compute size from number
  if (number) {
    number = (number > totalFacets) ? totalFacets : number;
    size   = static_cast<size_t>(
      std::round(static_cast<float>(totalFacets) / static_cast<float>(number)));
    delta = 0;
  }
  else {
    size = (size > totalFacets) ? totalFacets : size;
  }

  Float32Array facetPos; // submesh positions
  Uint32Array facetInd;  // submesh indices
  Float32Array facetUV;  // submesh UV
  Float32Array facetCol; // submesh colors
  Vector3 barycenter = Tmp::Vector3Array[0];
  size_t sizeO       = size;

  while (f < totalFacets) {
    size = sizeO + static_cast<size_t>(std::floor(
                     (1.f + static_cast<float>(delta)) * Math::random()));
    if (f > totalFacets - size) {
      size = totalFacets - f;
    }
    // reset temp arrays
    facetPos.clear();
    facetInd.clear();
    facetUV.clear();
    facetCol.clear();

    // iterate over "size" facets
    int fi = 0;
    for (size_t j = f * 3; j < (f + size) * 3; ++j) {
      facetInd.emplace_back(fi);
      unsigned int i = static_cast<unsigned int>(meshInd[j]);
      std_util::concat(
        facetPos, {meshPos[i * 3], meshPos[i * 3 + 1], meshPos[i * 3 + 2]});
      if (!meshUV.empty()) {
        std_util::concat(facetUV, {meshUV[i * 2], meshUV[i * 2 + 1]});
      }
      if (!meshCol.empty()) {
        std_util::concat(facetCol, {meshCol[i * 4 + 0], meshCol[i * 4 + 1],
                                    meshCol[i * 4 + 2], meshCol[i * 4 + 3]});
      }
      ++fi;
    }

    // create a model shape for each single particle
    unsigned int idx           = nbParticles;
    std::vector<Vector3> shape = _posToShape(facetPos);
    Float32Array shapeUV       = _uvsToShapeUV(facetUV);

    // compute the barycenter of the shape
    unsigned int v;
    for (v = 0; v < shape.size(); ++v) {
      barycenter.addInPlace(shape[v]);
    }
    barycenter.scaleInPlace(1.f / static_cast<float>(shape.size()));

    // shift the shape from its barycenter to the origin
    for (v = 0; v < shape.size(); ++v) {
      shape[v].subtractInPlace(barycenter);
    }
    BoundingInfo* bInfo = nullptr;
    if (_particlesIntersect) {
      bInfo = new BoundingInfo(barycenter, barycenter);
    }
    ModelShape* modelShape
      = new ModelShape(_shapeCounter, shape, shapeUV, nullptr, nullptr);

    // add the particle in the SPS
    //_meshBuilder(_index, shape, _positions, facetInd, _indices, facetUV, _uvs,
    //             facetCol, _colors, meshNor, _normals, idx, 0, nullptr);
    _addParticle(idx, static_cast<unsigned int>(_positions.size()), modelShape,
                 _shapeCounter, 0, bInfo);
    // initialize the particle position
    particles[nbParticles]->position.addInPlace(barycenter);

    _index += static_cast<unsigned int>(shape.size());
    ++idx;
    ++nbParticles;
    ++_shapeCounter;
    f += size;
  }
  return *this;
}

// reset copy
void SolidParticleSystem::_resetCopy()
{
  _copy->position.x         = 0.f;
  _copy->position.y         = 0.f;
  _copy->position.z         = 0.f;
  _copy->rotation.x         = 0.f;
  _copy->rotation.y         = 0.f;
  _copy->rotation.z         = 0.f;
  _copy->rotationQuaternion = nullptr;
  _copy->scaling.x          = 1.f;
  _copy->scaling.y          = 1.f;
  _copy->scaling.z          = 1.f;
  _copy->uvs.x              = 0.f;
  _copy->uvs.y              = 0.f;
  _copy->uvs.z              = 1.f;
  _copy->uvs.w              = 1.f;
  _copy->color              = nullptr;
}

// _meshBuilder : inserts the shape model in the global SPS mesh
void SolidParticleSystem::_meshBuilder(
  unsigned int p, const std::vector<Vector3>& shape, Float32Array& positions,
  Uint32Array& meshInd, Uint32Array& indices, const Float32Array& meshUV,
  Float32Array& uvs, const Float32Array& meshCol, Float32Array& colors,
  const Float32Array& meshNor, const Float32Array& /*normals*/,
  unsigned int idx, unsigned int idxInShape,
  const SolidParticleSystemMeshBuilderOptions& options)
{
  size_t i = 0;
  size_t u = 0;
  size_t c = 0;
  size_t n = 0;

  _resetCopy();
  if (options.positionFunction) { // call to custom
    options.positionFunction(_copy, idx, idxInShape);
  }

  if (_copy->rotationQuaternion) {
    _quaternion.copyFrom(*_copy->rotationQuaternion);
  }
  else {
    _yaw   = _copy->rotation.y;
    _pitch = _copy->rotation.x;
    _roll  = _copy->rotation.z;
    _quaternionRotationYPR();
  }
  _quaternionToRotationMatrix();

  for (unsigned int si = 0; si < shape.size(); ++si) {
    _vertex.x = shape[si].x;
    _vertex.y = shape[si].y;
    _vertex.z = shape[si].z;

    if (options.vertexFunction) {
      options.vertexFunction(_copy, _vertex, si);
    }

    _vertex.x *= _copy->scaling.x;
    _vertex.y *= _copy->scaling.y;
    _vertex.z *= _copy->scaling.z;

    Vector3::TransformCoordinatesToRef(_vertex, _rotMatrix, _rotated);
    std_util::concat(positions, {_copy->position.x + _rotated.x,
                                 _copy->position.y + _rotated.y,
                                 _copy->position.z + _rotated.z});
    if (!meshUV.empty()) {
      std_util::concat(
        uvs, {(_copy->uvs.z - _copy->uvs.x) * meshUV[u] + _copy->uvs.x,
              (_copy->uvs.w - _copy->uvs.y) * meshUV[u + 1] + _copy->uvs.y});
      u += 2;
    }

    if (_copy->color) {
      _color = _copy->color;
    }
    else if (!meshCol.empty() && (c + 3 < meshCol.size())) {
      _color->r = meshCol[c];
      _color->g = meshCol[c + 1];
      _color->b = meshCol[c + 2];
      _color->a = meshCol[c + 3];
    }
    else {
      _color->r = 1.f;
      _color->g = 1.f;
      _color->b = 1.f;
      _color->a = 1.f;
    }
    std_util::concat(colors, {_color->r, _color->g, _color->b, _color->a});
    c += 4;

    if (!recomputeNormals && (n + 3 < meshNor.size())) {
      _normal.x = meshNor[n];
      _normal.y = meshNor[n + 1];
      _normal.z = meshNor[n + 2];
      Vector3::TransformCoordinatesToRef(_normal, _rotMatrix, _normal);
      // std_util::concat(normals, {_normal.x, _normal.y, _normal.z});
      n += 3;
    }
  }

  for (i = 0; i < meshInd.size(); i++) {
    indices.emplace_back(p + meshInd[i]);
  }

  if (_pickable) {
    size_t nbfaces = meshInd.size() / 3;
    for (i = 0; i < nbfaces; ++i) {
      PickedParticle pp{idx, static_cast<unsigned int>(i)};
      pickedParticles.emplace_back(pp);
    }
  }
}

// returns a shape array from positions array
std::vector<Vector3>
SolidParticleSystem::_posToShape(const Float32Array& positions)
{
  std::vector<Vector3> shape;
  for (size_t i = 0; i < positions.size(); i += 3) {
    shape.emplace_back(
      Vector3(positions[i], positions[i + 1], positions[i + 2]));
  }
  return shape;
}

// returns a shapeUV array from a Vector4 uvs
Float32Array SolidParticleSystem::_uvsToShapeUV(const Float32Array& uvs)
{
  Float32Array shapeUV;
  if (!uvs.empty()) {
    for (auto uv : uvs) {
      shapeUV.emplace_back(uv);
    }
  }
  return shapeUV;
}

// adds a new particle object in the particles array
void SolidParticleSystem::_addParticle(unsigned int idx, unsigned int idxpos,
                                       ModelShape* model, int shapeId,
                                       unsigned int idxInShape,
                                       BoundingInfo* bInfo)
{
  particles.emplace_back(
    new SolidParticle(idx, idxpos, model, shapeId, idxInShape, this, bInfo));
}

int SolidParticleSystem::addShape(
  Mesh* iMesh, size_t nb, const SolidParticleSystemMeshBuilderOptions& options)
{
  Float32Array meshPos = iMesh->getVerticesData(VertexBuffer::PositionKind);
  Uint32Array meshInd  = iMesh->getIndices();
  Float32Array meshUV  = iMesh->getVerticesData(VertexBuffer::UVKind);
  Float32Array meshCol = iMesh->getVerticesData(VertexBuffer::ColorKind);
  Float32Array meshNor = iMesh->getVerticesData(VertexBuffer::NormalKind);
  BoundingInfo* bbInfo = nullptr;
  if (_particlesIntersect) {
    bbInfo = iMesh->getBoundingInfo();
  }

  std::vector<Vector3> shape = _posToShape(meshPos);
  Float32Array shapeUV       = _uvsToShapeUV(meshUV);

  auto& posfunc = options.positionFunction;
  auto& vtxfunc = options.vertexFunction;

  ModelShape* modelShape
    = new ModelShape(_shapeCounter, shape, shapeUV, posfunc, vtxfunc);

  // particles
  unsigned int idx = nbParticles;
  for (unsigned int i = 0; i < nb; ++i) {
    _meshBuilder(_index, shape, _positions, meshInd, _indices, meshUV, _uvs,
                 meshCol, _colors, meshNor, _normals, idx, i, options);
    if (_updatable) {
      _addParticle(idx, static_cast<unsigned int>(_positions.size()),
                   modelShape, _shapeCounter, i, bbInfo);
    }
    _index += static_cast<unsigned int>(shape.size());
    idx++;
  }
  nbParticles += static_cast<unsigned int>(nb);
  _shapeCounter++;
  return _shapeCounter - 1;
}

void SolidParticleSystem::_rebuildParticle(SolidParticle* particle)
{
  _resetCopy();
  if (particle->_model
        ->_positionFunction) { // recall to stored custom positionFunction
    particle->_model->_positionFunction(_copy, particle->idx,
                                        particle->idxInShape);
  }

  if (_copy->rotationQuaternion) {
    _quaternion.copyFrom(*_copy->rotationQuaternion);
  }
  else {
    _yaw   = _copy->rotation.y;
    _pitch = _copy->rotation.x;
    _roll  = _copy->rotation.z;
    _quaternionRotationYPR();
  }
  _quaternionToRotationMatrix();

  _shape = particle->_model->_shape;
  for (unsigned int pt = 0; pt < _shape.size(); ++pt) {
    _vertex.x = _shape[pt].x;
    _vertex.y = _shape[pt].y;
    _vertex.z = _shape[pt].z;

    if (particle->_model->_vertexFunction) {
      // recall to stored vertexFunction
      particle->_model->_vertexFunction(_copy, _vertex, pt);
    }

    _vertex.x *= _copy->scaling.x;
    _vertex.y *= _copy->scaling.y;
    _vertex.z *= _copy->scaling.z;

    Vector3::TransformCoordinatesToRef(_vertex, _rotMatrix, _rotated);

    _positions32[particle->_pos + pt * 3]     = _copy->position.x + _rotated.x;
    _positions32[particle->_pos + pt * 3 + 1] = _copy->position.y + _rotated.y;
    _positions32[particle->_pos + pt * 3 + 2] = _copy->position.z + _rotated.z;
  }
  particle->position.x         = 0.f;
  particle->position.y         = 0.f;
  particle->position.z         = 0.f;
  particle->rotation.x         = 0.f;
  particle->rotation.y         = 0;
  particle->rotation.z         = 0.f;
  particle->rotationQuaternion = nullptr;
  particle->scaling.x          = 1.f;
  particle->scaling.y          = 1.f;
  particle->scaling.z          = 1.f;
}

void SolidParticleSystem::rebuildMesh()
{
  for (auto& particle : particles) {
    _rebuildParticle(particle);
  }
  mesh->updateVerticesData(VertexBuffer::PositionKind, _positions32, false,
                           false);
}

void SolidParticleSystem::setParticles(unsigned int start, unsigned int end,
                                       bool update)
{
  unsigned int _end
    = (end < start) ? nbParticles - 1 : static_cast<unsigned>(end);

  if (!_updatable) {
    return;
  }

  // custom beforeUpdate
  beforeUpdateParticles(start, _end, update);

  _cam_axisX.x = 1.f;
  _cam_axisX.y = 0.f;
  _cam_axisX.z = 0.f;

  _cam_axisY.x = 0.f;
  _cam_axisY.y = 1.f;
  _cam_axisY.z = 0.f;

  _cam_axisZ.x = 0.f;
  _cam_axisZ.y = 0.f;
  _cam_axisZ.z = 1.f;

  // if the particles will always face the camera
  if (billboard) {
    // compute the camera position and un-rotate it by the current mesh rotation
    if (mesh->_worldMatrix->decompose(_scale, _quaternion, _translation)) {
      _quaternionToRotationMatrix();
      _rotMatrix.invertToRef(_invertMatrix);
      _camera->_currentTarget.subtractToRef(_camera->globalPosition(), _camDir);
      Vector3::TransformCoordinatesToRef(_camDir, _invertMatrix, _cam_axisZ);
      _cam_axisZ.normalize();
      // set two orthogonal vectors (_cam_axisX and and _cam_axisY) to the
      // rotated camDir axis (_cam_axisZ)
      Vector3::CrossToRef(_cam_axisZ, _axisX, _cam_axisY);
      Vector3::CrossToRef(_cam_axisY, _cam_axisZ, _cam_axisX);
      _cam_axisY.normalize();
      _cam_axisX.normalize();
    }
  }

  Matrix::IdentityToRef(_rotMatrix);
  unsigned int idx        = 0;
  unsigned int index      = 0;
  unsigned int colidx     = 0;
  unsigned int colorIndex = 0;
  unsigned int uvidx      = 0;
  unsigned int uvIndex    = 0;
  unsigned int pt         = 0;

  if (_computeBoundingBox) {
    Vector3::FromFloatsToRef(std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max(), _minimum);
    Vector3::FromFloatsToRef(-std::numeric_limits<float>::max(),
                             -std::numeric_limits<float>::max(),
                             -std::numeric_limits<float>::max(), _maximum);
  }

  // particle loop
  _end = (_end > nbParticles - 1) ? nbParticles - 1 : _end;
  for (unsigned int p = start; p <= _end; p++) {
    _particle = particles[p];
    _shape    = _particle->_model->_shape;
    _shapeUV  = _particle->_model->_shapeUV;

    // call to custom user function to update the particle properties
    updateParticle(_particle);

    if (_particle->isVisible) {

      // particle rotation matrix
      if (billboard) {
        _particle->rotation.x = 0.f;
        _particle->rotation.y = 0.f;
      }
      if (_computeParticleRotation || billboard) {
        if (_particle->rotationQuaternion) {
          _quaternion.copyFrom(*_particle->rotationQuaternion);
        }
        else {
          _yaw   = _particle->rotation.y;
          _pitch = _particle->rotation.x;
          _roll  = _particle->rotation.z;
          _quaternionRotationYPR();
        }
        _quaternionToRotationMatrix();
      }

      // particle vertex loop
      for (pt = 0; pt < _shape.size(); ++pt) {
        idx    = index + pt * 3;
        colidx = colorIndex + pt * 4;
        uvidx  = uvIndex + pt * 2;

        _vertex.x = _shape[pt].x;
        _vertex.y = _shape[pt].y;
        _vertex.z = _shape[pt].z;

        if (_computeParticleVertex) {
          updateParticleVertex(_particle, _vertex, pt);
        }

        // positions
        _vertex.x *= _particle->scaling.x;
        _vertex.y *= _particle->scaling.y;
        _vertex.z *= _particle->scaling.z;

        _w = (_vertex.x * _rotMatrix.m[3]) + (_vertex.y * _rotMatrix.m[7])
             + (_vertex.z * _rotMatrix.m[11]) + _rotMatrix.m[15];
        _rotated.x
          = ((_vertex.x * _rotMatrix.m[0]) + (_vertex.y * _rotMatrix.m[4])
             + (_vertex.z * _rotMatrix.m[8]) + _rotMatrix.m[12])
            / _w;
        _rotated.y
          = ((_vertex.x * _rotMatrix.m[1]) + (_vertex.y * _rotMatrix.m[5])
             + (_vertex.z * _rotMatrix.m[9]) + _rotMatrix.m[13])
            / _w;
        _rotated.z
          = ((_vertex.x * _rotMatrix.m[2]) + (_vertex.y * _rotMatrix.m[6])
             + (_vertex.z * _rotMatrix.m[10]) + _rotMatrix.m[14])
            / _w;

        _positions32[idx] = _particle->position.x + _cam_axisX.x * _rotated.x
                            + _cam_axisY.x * _rotated.y
                            + _cam_axisZ.x * _rotated.z;
        _positions32[idx + 1]
          = _particle->position.y + _cam_axisX.y * _rotated.x
            + _cam_axisY.y * _rotated.y + _cam_axisZ.y * _rotated.z;
        _positions32[idx + 2]
          = _particle->position.z + _cam_axisX.z * _rotated.x
            + _cam_axisY.z * _rotated.y + _cam_axisZ.z * _rotated.z;

        if (_computeBoundingBox) {
          if (_positions32[idx] < _minimum.x) {
            _minimum.x = _positions32[idx];
          }
          if (_positions32[idx] > _maximum.x) {
            _maximum.x = _positions32[idx];
          }
          if (_positions32[idx + 1] < _minimum.y) {
            _minimum.y = _positions32[idx + 1];
          }
          if (_positions32[idx + 1] > _maximum.y) {
            _maximum.y = _positions32[idx + 1];
          }
          if (_positions32[idx + 2] < _minimum.z) {
            _minimum.z = _positions32[idx + 2];
          }
          if (_positions32[idx + 2] > _maximum.z) {
            _maximum.z = _positions32[idx + 2];
          }
        }

        // normals : if the particles can't be morphed then just rotate the
        // normals, what if much more faster than ComputeNormals()
        if (!_computeParticleVertex) {
          _normal.x = _fixedNormal32[idx];
          _normal.y = _fixedNormal32[idx + 1];
          _normal.z = _fixedNormal32[idx + 2];

          _w = (_normal.x * _rotMatrix.m[3]) + (_normal.y * _rotMatrix.m[7])
               + (_normal.z * _rotMatrix.m[11]) + _rotMatrix.m[15];
          _rotated.x
            = ((_normal.x * _rotMatrix.m[0]) + (_normal.y * _rotMatrix.m[4])
               + (_normal.z * _rotMatrix.m[8]) + _rotMatrix.m[12])
              / _w;
          _rotated.y
            = ((_normal.x * _rotMatrix.m[1]) + (_normal.y * _rotMatrix.m[5])
               + (_normal.z * _rotMatrix.m[9]) + _rotMatrix.m[13])
              / _w;
          _rotated.z
            = ((_normal.x * _rotMatrix.m[2]) + (_normal.y * _rotMatrix.m[6])
               + (_normal.z * _rotMatrix.m[10]) + _rotMatrix.m[14])
              / _w;

          _normals32[idx] = _cam_axisX.x * _rotated.x
                            + _cam_axisY.x * _rotated.y
                            + _cam_axisZ.x * _rotated.z;
          _normals32[idx + 1] = _cam_axisX.y * _rotated.x
                                + _cam_axisY.y * _rotated.y
                                + _cam_axisZ.y * _rotated.z;
          _normals32[idx + 2] = _cam_axisX.z * _rotated.x
                                + _cam_axisY.z * _rotated.y
                                + _cam_axisZ.z * _rotated.z;
        }

        if (_computeParticleColor) {
          _colors32[colidx]     = _particle->color->r;
          _colors32[colidx + 1] = _particle->color->g;
          _colors32[colidx + 2] = _particle->color->b;
          _colors32[colidx + 3] = _particle->color->a;
        }

        if (_computeParticleTexture) {
          _uvs32[uvidx]
            = _shapeUV[pt * 2] * (_particle->uvs.z - _particle->uvs.x)
              + _particle->uvs.x;
          _uvs32[uvidx + 1]
            = _shapeUV[pt * 2 + 1] * (_particle->uvs.w - _particle->uvs.y)
              + _particle->uvs.y;
        }
      }
    }
    // particle not visible : scaled to zero and positioned to the camera
    // position
    else {
      for (pt = 0; pt < _shape.size(); ++pt) {
        idx                   = index + pt * 3;
        colidx                = colorIndex + pt * 4;
        uvidx                 = uvIndex + pt * 2;
        _positions32[idx]     = _camera->position.x;
        _positions32[idx + 1] = _camera->position.y;
        _positions32[idx + 2] = _camera->position.z;
        _normals32[idx]       = 0.f;
        _normals32[idx + 1]   = 0.f;
        _normals32[idx + 2]   = 0.f;
        if (_computeParticleColor) {
          _colors32[colidx]     = _particle->color->r;
          _colors32[colidx + 1] = _particle->color->g;
          _colors32[colidx + 2] = _particle->color->b;
          _colors32[colidx + 3] = _particle->color->a;
        }
        if (_computeParticleTexture) {
          _uvs32[uvidx]
            = _shapeUV[pt * 2] * (_particle->uvs.z - _particle->uvs.x)
              + _particle->uvs.x;
          _uvs32[uvidx + 1]
            = _shapeUV[pt * 2 + 1] * (_particle->uvs.w - _particle->uvs.y)
              + _particle->uvs.y;
        }
      }
    }

    // if the particle intersections must be computed : update the bbInfo
    if (_particlesIntersect) {
      BoundingInfo* bInfo     = _particle->_boundingInfo;
      BoundingBox& bBox       = bInfo->boundingBox;
      BoundingSphere& bSphere = bInfo->boundingSphere;
      if (!_bSphereOnly) {
        // place, scale and rotate the particle bbox within the SPS local
        // system, then update it
        for (size_t b = 0; b < bBox.vectors.size(); ++b) {
          _vertex.x = _particle->_modelBoundingInfo->boundingBox.vectors[b].x
                      * _particle->scaling.x;
          _vertex.y = _particle->_modelBoundingInfo->boundingBox.vectors[b].y
                      * _particle->scaling.y;
          _vertex.z = _particle->_modelBoundingInfo->boundingBox.vectors[b].z
                      * _particle->scaling.z;
          _w = (_vertex.x * _rotMatrix.m[3]) + (_vertex.y * _rotMatrix.m[7])
               + (_vertex.z * _rotMatrix.m[11]) + _rotMatrix.m[15];
          _rotated.x
            = ((_vertex.x * _rotMatrix.m[0]) + (_vertex.y * _rotMatrix.m[4])
               + (_vertex.z * _rotMatrix.m[8]) + _rotMatrix.m[12])
              / _w;
          _rotated.y
            = ((_vertex.x * _rotMatrix.m[1]) + (_vertex.y * _rotMatrix.m[5])
               + (_vertex.z * _rotMatrix.m[9]) + _rotMatrix.m[13])
              / _w;
          _rotated.z
            = ((_vertex.x * _rotMatrix.m[2]) + (_vertex.y * _rotMatrix.m[6])
               + (_vertex.z * _rotMatrix.m[10]) + _rotMatrix.m[14])
              / _w;
          bBox.vectors[b].x = _particle->position.x + _cam_axisX.x * _rotated.x
                              + _cam_axisY.x * _rotated.y
                              + _cam_axisZ.x * _rotated.z;
          bBox.vectors[b].y = _particle->position.y + _cam_axisX.y * _rotated.x
                              + _cam_axisY.y * _rotated.y
                              + _cam_axisZ.y * _rotated.z;
          bBox.vectors[b].z = _particle->position.z + _cam_axisX.z * _rotated.x
                              + _cam_axisY.z * _rotated.y
                              + _cam_axisZ.z * _rotated.z;
        }
        bBox._update(*mesh->_worldMatrix);
      }
      // place and scale the particle bouding sphere in the SPS local system,
      // then update it
      _minBbox.x
        = _particle->_modelBoundingInfo->minimum.x * _particle->scaling.x;
      _minBbox.y
        = _particle->_modelBoundingInfo->minimum.y * _particle->scaling.y;
      _minBbox.z
        = _particle->_modelBoundingInfo->minimum.z * _particle->scaling.z;
      _maxBbox.x
        = _particle->_modelBoundingInfo->maximum.x * _particle->scaling.x;
      _maxBbox.y
        = _particle->_modelBoundingInfo->maximum.y * _particle->scaling.y;
      _maxBbox.z
        = _particle->_modelBoundingInfo->maximum.z * _particle->scaling.z;
      bSphere.center.x
        = _particle->position.x + (_minBbox.x + _maxBbox.x) * 0.5f;
      bSphere.center.y
        = _particle->position.y + (_minBbox.y + _maxBbox.y) * 0.5f;
      bSphere.center.z
        = _particle->position.z + (_minBbox.z + _maxBbox.z) * 0.5f;
      bSphere.radius
        = _bSphereRadiusFactor * 0.5f
          * std::sqrt((_maxBbox.x - _minBbox.x) * (_maxBbox.x - _minBbox.x)
                      + (_maxBbox.y - _minBbox.y) * (_maxBbox.y - _minBbox.y)
                      + (_maxBbox.z - _minBbox.z) * (_maxBbox.z - _minBbox.z));
      bSphere._update(*mesh->_worldMatrix);
    }

    // increment indexes for the next particle
    index      = idx + 3;
    colorIndex = colidx + 4;
    uvIndex    = uvidx + 2;
  }

  // if the VBO must be updated
  if (update) {
    if (_computeParticleColor) {
      mesh->updateVerticesData(VertexBuffer::ColorKind, _colors32, false,
                               false);
    }
    if (_computeParticleTexture) {
      mesh->updateVerticesData(VertexBuffer::UVKind, _uvs32, false, false);
    }
    mesh->updateVerticesData(VertexBuffer::PositionKind, _positions32, false,
                             false);
    if (!mesh->areNormalsFrozen()) {
      if (_computeParticleVertex) {
        // recompute the normals only if the particles can be morphed, update
        // then also the normal reference array _fixedNormal32[]
        VertexData::ComputeNormals(_positions32, _indices, _normals32);
        for (size_t i = 0; i < _normals32.size(); ++i) {
          _fixedNormal32[i] = _normals32[i];
        }
      }
      mesh->updateVerticesData(VertexBuffer::NormalKind, _normals32, false,
                               false);
    }
  }
  if (_computeBoundingBox) {
    mesh->_boundingInfo.reset(new BoundingInfo(_minimum, _maximum));
    // mesh->_boundingInfo->update(mesh->_worldMatrix);
  }
  afterUpdateParticles(start, _end, update);
}

void SolidParticleSystem::_quaternionRotationYPR()
{
  _halfroll  = _roll * 0.5f;
  _halfpitch = _pitch * 0.5f;
  _halfyaw   = _yaw * 0.5f;
  _sinRoll   = std::sin(_halfroll);
  _cosRoll   = std::cos(_halfroll);
  _sinPitch  = std::sin(_halfpitch);
  _cosPitch  = std::cos(_halfpitch);
  _sinYaw    = std::sin(_halfyaw);
  _cosYaw    = std::cos(_halfyaw);
  _quaternion.x
    = (_cosYaw * _sinPitch * _cosRoll) + (_sinYaw * _cosPitch * _sinRoll);
  _quaternion.y
    = (_sinYaw * _cosPitch * _cosRoll) - (_cosYaw * _sinPitch * _sinRoll);
  _quaternion.z
    = (_cosYaw * _cosPitch * _sinRoll) - (_sinYaw * _sinPitch * _cosRoll);
  _quaternion.w
    = (_cosYaw * _cosPitch * _cosRoll) + (_sinYaw * _sinPitch * _sinRoll);
}

void SolidParticleSystem::_quaternionToRotationMatrix()
{
  _rotMatrix.m[0]
    = 1.f
      - (2.f * (_quaternion.y * _quaternion.y + _quaternion.z * _quaternion.z));
  _rotMatrix.m[1]
    = 2.f * (_quaternion.x * _quaternion.y + _quaternion.z * _quaternion.w);
  _rotMatrix.m[2]
    = 2.f * (_quaternion.z * _quaternion.x - _quaternion.y * _quaternion.w);
  _rotMatrix.m[3] = 0.f;
  _rotMatrix.m[4]
    = 2.f * (_quaternion.x * _quaternion.y - _quaternion.z * _quaternion.w);
  _rotMatrix.m[5]
    = 1.f
      - (2.f * (_quaternion.z * _quaternion.z + _quaternion.x * _quaternion.x));
  _rotMatrix.m[6]
    = 2.f * (_quaternion.y * _quaternion.z + _quaternion.x * _quaternion.w);
  _rotMatrix.m[7] = 0.f;
  _rotMatrix.m[8]
    = 2.f * (_quaternion.z * _quaternion.x + _quaternion.y * _quaternion.w);
  _rotMatrix.m[9]
    = 2.f * (_quaternion.y * _quaternion.z - _quaternion.x * _quaternion.w);
  _rotMatrix.m[10]
    = 1.f
      - (2.f * (_quaternion.y * _quaternion.y + _quaternion.x * _quaternion.x));
  _rotMatrix.m[11] = 0.f;
  _rotMatrix.m[12] = 0.f;
  _rotMatrix.m[13] = 0.f;
  _rotMatrix.m[14] = 0.f;
  _rotMatrix.m[15] = 1.f;
}

void SolidParticleSystem::dispose(bool /*doNotRecurse*/)
{
  mesh->dispose();
  // drop references to internal big arrays for the GC
  _positions.clear();
  _indices.clear();
  _normals.clear();
  _uvs.clear();
  _colors.clear();
  _positions32.clear();
  _normals32.clear();
  _fixedNormal32.clear();
  _uvs32.clear();
  _colors32.clear();
  pickedParticles.clear();
}

void SolidParticleSystem::refreshVisibleSize()
{
  if (!_isVisibilityBoxLocked) {
    mesh->refreshBoundingInfo();
  }
}

void SolidParticleSystem::setVisibilityBox(float size)
{
  float vis = size / 2.f;
  mesh->_boundingInfo.reset(
    new BoundingInfo(Vector3(-vis, -vis, -vis), Vector3(vis, vis, vis)));
}

bool SolidParticleSystem::isAlwaysVisible() const
{
  return _alwaysVisible;
}

void SolidParticleSystem::setIsAlwaysVisible(bool val)
{
  _alwaysVisible                 = val;
  mesh->alwaysSelectAsActiveMesh = val;
}

void SolidParticleSystem::setIsVisibilityBoxLocked(bool val)
{
  _isVisibilityBoxLocked = val;
  mesh->getBoundingInfo()->setIsLocked(val);
}

bool SolidParticleSystem::SolidParticleSystem::isVisibilityBoxLocked() const
{
  return _isVisibilityBoxLocked;
}

void SolidParticleSystem::setComputeParticleRotation(bool val)
{
  _computeParticleRotation = val;
}

void SolidParticleSystem::setComputeParticleColor(bool val)
{
  _computeParticleColor = val;
}

void SolidParticleSystem::setComputeParticleTexture(bool val)
{
  _computeParticleTexture = val;
}

void SolidParticleSystem::setComputeParticleVertex(bool val)
{
  _computeParticleVertex = val;
}

void SolidParticleSystem::setComputeBoundingBox(bool val)
{
  _computeBoundingBox = val;
}

bool SolidParticleSystem::computeParticleRotation() const
{
  return _computeParticleRotation;
}

bool SolidParticleSystem::computeParticleColor() const
{
  return _computeParticleColor;
}

bool SolidParticleSystem::computeParticleTexture() const
{
  return _computeParticleTexture;
}

bool SolidParticleSystem::computeParticleVertex() const
{
  return _computeParticleVertex;
}

bool SolidParticleSystem::computeBoundingBox() const
{
  return _computeBoundingBox;
}

void SolidParticleSystem::initParticles()
{
}

SolidParticle* SolidParticleSystem::recycleParticle(SolidParticle* particle)
{
  return particle;
}

SolidParticle* SolidParticleSystem::updateParticle(SolidParticle* particle)
{
  return particle;
}

Vector3 SolidParticleSystem::updateParticleVertex(SolidParticle* /*particle*/,
                                                  const Vector3& vertex,
                                                  unsigned int /*pt*/)
{
  return vertex;
}

void SolidParticleSystem::beforeUpdateParticles(unsigned int /*start*/,
                                                unsigned int /*stop*/,
                                                bool /*update*/)
{
}

void SolidParticleSystem::afterUpdateParticles(unsigned int /*start*/,
                                               unsigned int /*stop*/,
                                               bool /*update*/)
{
}

} // end of namespace BABYLON
