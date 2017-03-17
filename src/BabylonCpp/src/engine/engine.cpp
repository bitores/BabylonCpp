#include <babylon/engine/engine.h>

#include <babylon/babylon_version.h>
#include <babylon/cameras/camera.h>
#include <babylon/core/logging.h>
#include <babylon/core/string.h>
#include <babylon/core/time.h>
#include <babylon/engine/instancing_attribute_info.h>
#include <babylon/interfaces/icanvas.h>
#include <babylon/interfaces/igl_rendering_context.h>
#include <babylon/interfaces/iloading_screen.h>
#include <babylon/materials/effect.h>
#include <babylon/math/color3.h>
#include <babylon/math/color4.h>
#include <babylon/mesh/vertex_buffer.h>
#include <babylon/postprocess/post_process.h>
#include <babylon/states/_alpha_state.h>
#include <babylon/states/_depth_culling_state.h>
#include <babylon/states/_stencil_state.h>
#include <babylon/tools/tools.h>

namespace BABYLON {

constexpr unsigned int Engine::TEXTUREFORMAT_ALPHA;
constexpr unsigned int Engine::TEXTUREFORMAT_LUMINANCE;
constexpr unsigned int Engine::TEXTUREFORMAT_LUMINANCE_ALPHA;
constexpr unsigned int Engine::TEXTUREFORMAT_RGB;
constexpr unsigned int Engine::TEXTUREFORMAT_RGBA;

constexpr unsigned int Engine::TEXTURETYPE_UNSIGNED_INT;
constexpr unsigned int Engine::TEXTURETYPE_FLOAT;
constexpr unsigned int Engine::TEXTURETYPE_HALF_FLOAT;

std::string Engine::Version()
{
  return BABYLONCPP_VERSION;
}

float Engine::CollisionsEpsilon       = 0.001f;
std::string Engine::CodeRepository    = "src/";
std::string Engine::ShadersRepository = "src/shaders/";

Engine::Engine(ICanvas* canvas, const EngineOptions& options)
    : isFullscreen{false}
    , isPointerLock{false}
    , cullBackFaces{true}
    , renderEvenInBackground{true}
    , enableOfflineSupport{true}
    , _gl{nullptr}
    , _renderingCanvas{canvas}
    , _windowIsBackground{false}
    , _webGLVersion{"1.0"}
    , _badOS{false}
    , _alphaTest{false}
    , _videoTextureSupported{false}
    , _renderingQueueLaunched{false}
    , fpsRange{60}
    , fps{60.f}
    , deltaTime{std::chrono::microseconds(0)}
    , _depthCullingState{std_util::make_unique<Internals::_DepthCullingState>()}
    , _stencilState{std_util::make_unique<Internals::_StencilState>()}
    , _alphaState{std_util::make_unique<Internals::_AlphaState>()}
    , _alphaMode{Engine::ALPHA_DISABLE}
    , _maxTextureChannels{16}
    , _currentProgram{nullptr}
    , _cachedVertexBuffers{nullptr}
    , _cachedIndexBuffer{nullptr}
    , _cachedEffectForVertexBuffers{nullptr}
    , _currentRenderTarget{nullptr}
{
  // Checks if some of the format renders first to allow the use of webgl
  // inspector.
  auto renderToFullFloat = _canRenderToFloatTexture();
  auto renderToHalfFloat = _canRenderToHalfFloatTexture();

  // GL
  if (!_gl) {
    if (!canvas) {
      BABYLON_LOG_ERROR("Engine", "The provided canvas is null or undefined");
      return;
    }
    _gl = canvas->getContext3d(options);
  }

  if (!_gl) {
    BABYLON_LOG_ERROR("Engine", "GL not supported");
    return;
  }

  _onBlur = [this]() { _windowIsBackground = true; };

  _onFocus = [this]() { _windowIsBackground = false; };

  // Viewport
  _hardwareScalingLevel = options.adaptToDeviceRatio ? 1.f : 1.f;
  resize();

  // Caps
  _isStencilEnable            = options.stencil;
  _caps.maxTexturesImageUnits = _gl->getParameteri(GL::MAX_TEXTURE_IMAGE_UNITS);
  _caps.maxTextureSize        = _gl->getParameteri(GL::MAX_TEXTURE_SIZE);
  _caps.maxCubemapTextureSize
    = _gl->getParameteri(GL::MAX_CUBE_MAP_TEXTURE_SIZE);
  _caps.maxRenderTextureSize = _gl->getParameteri(GL::MAX_RENDERBUFFER_SIZE);
  _caps.maxVertexAttribs     = _gl->getParameteri(GL::MAX_VERTEX_ATTRIBS);

  // Infos
  _glVersion  = _gl->getString(GL::VERSION);
  _glRenderer = _gl->getString(GL::RENDERER);
  _glVendor   = _gl->getString(GL::VENDOR);

  if (_glVendor.empty()) {
    _glVendor = "Unknown vendor";
  }

  if (_glRenderer.empty()) {
    _glRenderer = "Unknown renderer";
  }

  // Extensions
  std::vector<std::string> extensionList
    = String::split(_gl->getString(GL::EXTENSIONS), ' ');
  std::set<std::string> extensions;
  for (auto& extension : extensionList) {
    extensions.insert(extension);
  }

  _caps.standardDerivatives = true;
  _caps.textureFloat = std_util::contains(extensions, "GL_ARB_texture_float");
  _caps.textureAnisotropicFilterExtension
    = std_util::contains(extensions, "GL_EXT_texture_filter_anisotropic");
  _caps.maxAnisotropy = _caps.textureAnisotropicFilterExtension ?
                          static_cast<unsigned>(_gl->getParameteri(
                            GL::MAX_TEXTURE_MAX_ANISOTROPY_EXT)) :
                          0;
  _caps.instancedArrays              = nullptr;
  _caps.uintIndices                  = true;
  _caps.fragmentDepthSupported       = true;
  _caps.highPrecisionShaderSupported = true;
  _caps.drawBuffersExtension
    = std_util::contains(extensions, "GL_ARB_draw_buffers");
  _caps.textureFloatLinearFiltering = true;
  _caps.textureLOD
    = std_util::contains(extensions, "GL_ARB_shader_texture_lod");
  _caps.textureFloatRender = renderToFullFloat;

  _caps.textureHalfFloat
    = std_util::contains(extensions, "OES_texture_half_float");
  _caps.textureHalfFloatLinearFiltering
    = std_util::contains(extensions, "OES_texture_half_float_linear");
  _caps.textureHalfFloatRender = renderToHalfFloat;

  GL::IGLShaderPrecisionFormat* highp
    = _gl->getShaderPrecisionFormat(GL::FRAGMENT_SHADER, GL::HIGH_FLOAT);
  _caps.highPrecisionShaderSupported = highp ? highp->precision != 0 : false;

  // Depth buffer
  setDepthBuffer(true);
  setDepthFunctionToLessOrEqual();
  setDepthWrite(true);

  // if (!Engine.audioEngine) {
  //  Engine::audioEngine = new AudioEngine();
  //}

  // Default loading screen
  //_loadingScreen
  //  = std_util::make_unique<DefaultLoadingScreen>(_renderingCanvas);

  // Load WebVR Devices
  // if (options.autoEnableWebVR) {
  //  initWebVR();
  //}

  BABYLON_LOGF_INFO("Engine", "BabylonCpp engine (v%s) launched",
                    Engine::Version().c_str());
}

Engine::~Engine()
{
}

std::vector<std::string>& Engine::texturesSupported()
{
  return _texturesSupported;
}

std::string Engine::textureFormatInUse() const
{
  return _textureFormatInUse;
}

std::string Engine::getWebGLVersion() const
{
  return _webGLVersion;
}

bool Engine::isStencilEnable() const
{
  return _isStencilEnable;
}

void Engine::resetTextureCache()
{
  for (unsigned int index = 0, ul = _maxTextureChannels; index < ul; ++index) {
    _activeTexturesCache[index] = nullptr;
  }
}

GL::GLInfo Engine::getGlInfo()
{
  return {_glVendor, _glRenderer, _glVersion};
}

float Engine::getAspectRatio(Camera* camera, bool useScreen)
{
  const Viewport& viewport = camera->viewport;
  return static_cast<float>(getRenderWidth(useScreen) * viewport.width)
         / static_cast<float>(getRenderHeight(useScreen) * viewport.height);
}

int Engine::getRenderWidth(bool useScreen)
{
  if (!useScreen && _currentRenderTarget) {
    return _currentRenderTarget->_width;
  }

  return _renderingCanvas->width;
}

int Engine::getRenderHeight(bool useScreen)
{
  if (!useScreen && _currentRenderTarget) {
    return _currentRenderTarget->_height;
  }

  return _renderingCanvas->height;
}

ICanvas* Engine::getRenderingCanvas()
{
  return _renderingCanvas;
}

ClientRect Engine::getRenderingCanvasClientRect()
{
  return _renderingCanvas->getBoundingClientRect();
}

void Engine::setHardwareScalingLevel(int level)
{
  _hardwareScalingLevel = level;
  resize();
}

int Engine::getHardwareScalingLevel() const
{
  return _hardwareScalingLevel;
}

std::vector<Engine::GLTexturePtr>& Engine::getLoadedTexturesCache()
{
  return _loadedTexturesCache;
}

EngineCapabilities& Engine::getCaps()
{
  return _caps;
}

size_t Engine::drawCalls() const
{
  return _drawCalls.current();
}

PerfCounter& Engine::drawCallsPerfCounter()
{
  return _drawCalls;
}

// Methods
void Engine::backupGLState()
{
  _gl->backupGLState();
}

void Engine::restoreGLState()
{
  _gl->restoreGLState();
}

int Engine::getDepthFunction() const
{
  return _depthCullingState->depthFunc();
}

void Engine::setDepthFunction(int depthFunc)
{
  _depthCullingState->setDepthFunc(depthFunc);
}

void Engine::setDepthFunctionToGreater()
{
  _depthCullingState->setDepthFunc(GL::GREATER);
}

void Engine::setDepthFunctionToGreaterOrEqual()
{
  _depthCullingState->setDepthFunc(GL::GEQUAL);
}

void Engine::setDepthFunctionToLess()
{
  _depthCullingState->setDepthFunc(GL::LESS);
}

void Engine::setDepthFunctionToLessOrEqual()
{
  _depthCullingState->setDepthFunc(GL::LEQUAL);
}

bool Engine::getStencilBuffer() const
{
  return _stencilState->stencilTest();
}

void Engine::setStencilBuffer(bool enable)
{
  _stencilState->setStencilTest(enable);
}

unsigned int Engine::getStencilMask() const
{
  return _stencilState->stencilMask();
}

void Engine::setStencilMask(unsigned int mask)
{
  _stencilState->setStencilMask(mask);
}

unsigned int Engine::getStencilFunction() const
{
  return _stencilState->stencilFunc();
}

int Engine::getStencilFunctionReference() const
{
  return _stencilState->stencilFuncRef();
}

unsigned int Engine::getStencilFunctionMask() const
{
  return _stencilState->stencilFuncMask();
}

void Engine::setStencilFunction(unsigned int stencilFunc)
{
  _stencilState->setStencilFunc(stencilFunc);
}

void Engine::setStencilFunctionReference(int reference)
{
  _stencilState->setStencilFuncRef(reference);
}

void Engine::setStencilFunctionMask(unsigned int mask)
{
  _stencilState->setStencilFuncMask(mask);
}

unsigned int Engine::getStencilOperationFail() const
{
  return _stencilState->stencilOpStencilFail();
}

unsigned int Engine::getStencilOperationDepthFail() const
{
  return _stencilState->stencilOpDepthFail();
}

unsigned int Engine::getStencilOperationPass() const
{
  return _stencilState->stencilOpStencilDepthPass();
}

void Engine::setStencilOperationFail(unsigned int operation)
{
  _stencilState->setStencilOpStencilFail(operation);
}

void Engine::setStencilOperationDepthFail(unsigned int operation)
{
  _stencilState->setStencilOpDepthFail(operation);
}

void Engine::setStencilOperationPass(unsigned int operation)
{
  _stencilState->setStencilOpStencilDepthPass(operation);
}

void Engine::stopRenderLoop(const FastFunc<void()>& renderFunction)
{
  if (renderFunction == nullptr) {
    _activeRenderLoops.clear();
    return;
  }

  _activeRenderLoops.erase(std::remove(_activeRenderLoops.begin(),
                                       _activeRenderLoops.end(),
                                       renderFunction),
                           _activeRenderLoops.end());
}

void Engine::_renderLoop()
{
  bool shouldRender = true;
  if (!renderEvenInBackground && _windowIsBackground) {
    shouldRender = false;
  }

  if (shouldRender) {
    // Start new frame
    beginFrame();

    for (auto& renderFunction : _activeRenderLoops) {
      renderFunction();
    }

    // Present
    endFrame();
  }

  if (_activeRenderLoops.size() > 0) {
    // Register new frame
  }
  else {
    _renderingQueueLaunched = false;
  }
}

void Engine::runRenderLoop(const FastFunc<void()>& renderFunction)
{
  if (std::find(_activeRenderLoops.begin(), _activeRenderLoops.end(),
                renderFunction)
      != _activeRenderLoops.end()) {
    return;
  }

  _activeRenderLoops.emplace_back(renderFunction);

  if (!_renderingQueueLaunched) {
    _renderingQueueLaunched = true;
  }
}

void Engine::renderFunction(const std::function<void()>& renderFunction)
{
  bool shouldRender = true;
  if (!renderEvenInBackground && _windowIsBackground) {
    shouldRender = false;
  }

  if (shouldRender) {
    // Backup GL state
    //backupGLState();
    // Start new frame
    beginFrame();
    // Render
    renderFunction();
    // Present
    endFrame();
    // Restore GL state
    //restoreGLState();
  }
}

void Engine::switchFullscreen(bool requestPointerLock)
{
  if (isFullscreen) {
    Tools::ExitFullscreen();
  }
  else {
    _pointerLockRequested = requestPointerLock;
    Tools::RequestFullscreen(_renderingCanvas);
  }
}

void Engine::clear(const Color3& color, bool backBuffer, bool depth,
                   bool stencil)
{
  applyStates();

  unsigned int mode = 0;
  if (backBuffer) {
    _gl->clearColor(color.r, color.g, color.b, 1.f);
    mode |= GL::COLOR_BUFFER_BIT;
  }

  if (depth) {
    _gl->clearDepth(1.f);
    mode |= GL::DEPTH_BUFFER_BIT;
  }

  if (stencil) {
    _gl->clearStencil(0);
    mode |= GL::STENCIL_BUFFER_BIT;
  }

  _gl->clear(mode);
}

void Engine::clear(const Color4& color, bool backBuffer, bool depth,
                   bool stencil)
{
  unsigned int mode = 0;
  if (backBuffer) {
    _gl->clearColor(color.r, color.g, color.b, color.a);
    mode |= GL::COLOR_BUFFER_BIT;
  }

  if (depth) {
    _gl->clearDepth(1.f);
    mode |= GL::DEPTH_BUFFER_BIT;
  }

  if (stencil) {
    _gl->clearStencil(0);
    mode |= GL::STENCIL_BUFFER_BIT;
  }

  _gl->clear(mode);
}

void Engine::scissorClear(int x, int y, int width, int height,
                          const Color4& clearColor)
{
  // Save state
  int curScissor = _gl->getParameteri(GL::SCISSOR_TEST);
  std::array<int, 3> curScissorBox = _gl->getScissorBoxParameter();

  // Change state
  _gl->enable(GL::SCISSOR_TEST);
  _gl->scissor(x, y, width, height);

  // Clear
  clear(clearColor, true, true, true);

  // Restore state
  _gl->scissor(curScissorBox[0], curScissorBox[1], curScissorBox[2],
               curScissorBox[3]);

  if (curScissor == 1) {
    _gl->enable(GL::SCISSOR_TEST);
  }
  else {
    _gl->disable(GL::SCISSOR_TEST);
  }
}

void Engine::setViewport(Viewport& viewport, int requiredWidth,
                         int requiredHeight)
{

  int width  = requiredWidth != 0 ? requiredWidth : _renderingCanvas->width;
  int height = requiredHeight != 0 ? requiredHeight : _renderingCanvas->height;
  int x      = viewport.x;
  int y      = viewport.y;

  _cachedViewport = &viewport;

  _gl->viewport(x * width, y * height, width * viewport.width,
                height * viewport.height);
}

Viewport& Engine::setDirectViewport(int x, int y, int width, int height)
{
  auto currentViewport = _cachedViewport;
  _cachedViewport      = nullptr;

  _gl->viewport(x, y, width, height);

  return *currentViewport;
}

void Engine::beginFrame()
{
  _measureFps();
}

void Engine::endFrame()
{
  // Force a flush in case we are using a bad OS.
  if (_badOS) {
    flushFramebuffer();
  }

  // submit frame to the vr device, if enabled
  // if (_vrDisplayEnabled && _vrDisplayEnabled.isPresenting) {
  //  _vrDisplayEnabled.submitFrame()
  //}
}

void Engine::resize()
{
  int width  = _renderingCanvas->clientWidth;
  int height = _renderingCanvas->clientHeight;

  setSize(width / _hardwareScalingLevel, height / _hardwareScalingLevel);

  // for (auto& scene : scenes) {
  //  if (scene->debugLayer()->isVisible()) {
  //    scene->debugLayer->_syncPositions();
  //  }
  //}
}

void Engine::setSize(int width, int height)
{
  _renderingCanvas->width  = width;
  _renderingCanvas->height = height;

  for (auto& scene : scenes) {
    for (auto& cam : scene->cameras) {
      cam->_currentRenderId = 0;
    }
  }
}

/*void Engine::initWebVR()
{
}

void Engine::enableVR(VRDevice vrDevice)
{
}

void Engine::disableVR()
{
}*/

void Engine::_onVRFullScreenTriggered()
{
}

void Engine::_getVRDisplays()
{
}

void Engine::bindFramebuffer(GL::IGLTexture* texture, unsigned int faceIndex,
                             int requiredWidth, int requiredHeight)
{
  _currentRenderTarget = texture;

  bindUnboundFramebuffer(texture->_framebuffer.get());

  if (texture->isCube) {
    _gl->framebufferTexture2D(GL::FRAMEBUFFER, GL::COLOR_ATTACHMENT0,
                              GL::TEXTURE_CUBE_MAP_POSITIVE_X + faceIndex,
                              texture, 0);
  }

  _gl->viewport(0, 0, (requiredWidth == 0) ? texture->_width : requiredWidth,
                (requiredHeight == 0) ? texture->_height : requiredHeight);

  wipeCaches();
}

void Engine::bindUnboundFramebuffer(GL::IGLFramebuffer* framebuffer)
{
  if (_currentFramebuffer != framebuffer) {
    _gl->bindFramebuffer(GL::FRAMEBUFFER, framebuffer);
    _currentFramebuffer = framebuffer;
  }
}

void Engine::unBindFramebuffer(GL::IGLTexture* texture,
                               bool disableGenerateMipMaps)
{
  _currentRenderTarget = nullptr;
  if (texture->generateMipMaps && !disableGenerateMipMaps) {
    _bindTextureDirectly(GL::TEXTURE_2D, texture);
    _gl->generateMipmap(GL::TEXTURE_2D);
    _bindTextureDirectly(GL::TEXTURE_2D, nullptr);
  }

  bindUnboundFramebuffer(nullptr);
}

void Engine::generateMipMapsForCubemap(GL::IGLTexture* texture)
{
  if (texture->generateMipMaps) {
    _bindTextureDirectly(GL::TEXTURE_CUBE_MAP, texture);
    _gl->generateMipmap(GL::TEXTURE_CUBE_MAP);
    _bindTextureDirectly(GL::TEXTURE_CUBE_MAP, nullptr);
  }
}

void Engine::flushFramebuffer()
{
  _gl->flush();
}

void Engine::restoreDefaultFramebuffer()
{
  _currentRenderTarget = nullptr;
  bindUnboundFramebuffer(nullptr);

  setViewport(*_cachedViewport);

  wipeCaches();
}

// VBOs
void Engine::_resetVertexBufferBinding()
{
  bindArrayBuffer(nullptr);
  _cachedVertexBuffers = nullptr;
}

Engine::GLBufferPtr Engine::createVertexBuffer(const Float32Array& vertices)
{
  auto vbo = _gl->createBuffer();
  bindArrayBuffer(vbo.get());
  _gl->bufferData(GL::ARRAY_BUFFER, vertices, GL::STATIC_DRAW);
  _resetVertexBufferBinding();
  vbo->references = 1;
  return vbo;
}

Engine::GLBufferPtr
Engine::createDynamicVertexBuffer(const Float32Array& vertices)
{
  auto vbo = _gl->createBuffer();
  bindArrayBuffer(vbo.get());
  _gl->bufferData(GL::ARRAY_BUFFER, vertices, GL::DYNAMIC_DRAW);
  _resetVertexBufferBinding();
  vbo->references = 1;
  return vbo;
}

void Engine::updateDynamicVertexBuffer(const Engine::GLBufferPtr& vertexBuffer,
                                       const Float32Array& vertices, int offset,
                                       int count)
{
  bindArrayBuffer(vertexBuffer.get());

  int _offset = (offset < 0) ? 0 : offset;

  if (count == -1) {
    _gl->bufferSubData(GL::ARRAY_BUFFER, offset, vertices);
  }
  else {
    Float32Array subvector;
    std::copy(vertices.begin() + _offset, vertices.begin() + _offset + count,
              std::back_inserter(subvector));
    _gl->bufferSubData(GL::ARRAY_BUFFER, 0, subvector);
  }

  _resetVertexBufferBinding();
}

void Engine::_resetIndexBufferBinding()
{
  bindIndexBuffer(nullptr);
  _cachedIndexBuffer = nullptr;
}

Engine::GLBufferPtr Engine::createIndexBuffer(const Uint32Array& indices)
{
  auto vbo = _gl->createBuffer();
  bindIndexBuffer(vbo.get());

  // Check for 32 bits indices
  auto need32Bits = false;

  if (_caps.uintIndices) {
    auto it = std::find_if(indices.begin(), indices.end(),
                           std::bind2nd(std::greater<float>(), 65535.f));
    if (it != indices.end()) {
      need32Bits = true;
    }
  }

  if (need32Bits) {
    Uint32Array arrayBuffer;
    arrayBuffer.assign(indices.begin(), indices.end());
    _gl->bufferData(GL::ELEMENT_ARRAY_BUFFER, arrayBuffer, GL::STATIC_DRAW);
  }
  else {
    Uint16Array arrayBuffer;
    arrayBuffer.assign(indices.begin(), indices.end());
    _gl->bufferData(GL::ELEMENT_ARRAY_BUFFER, arrayBuffer, GL::STATIC_DRAW);
  }

  _resetIndexBufferBinding();
  vbo->references = 1;
  vbo->is32Bits   = need32Bits;
  return vbo;
}

void Engine::bindArrayBuffer(GL::IGLBuffer* buffer)
{
  bindBuffer(buffer, GL::ARRAY_BUFFER);
}

void Engine::bindIndexBuffer(GL::IGLBuffer* buffer)
{
  bindBuffer(buffer, GL::ELEMENT_ARRAY_BUFFER);
}

void Engine::bindBuffer(GL::IGLBuffer* buffer, int target)
{
  if ((_currentBoundBuffer.find(target) == _currentBoundBuffer.end())
      || (_currentBoundBuffer[target] != buffer)) {
    _gl->bindBuffer(static_cast<unsigned int>(target), buffer);
    _currentBoundBuffer[target] = buffer;
  }
}

void Engine::updateArrayBuffer(const Float32Array& data)
{
  _gl->bufferSubData(GL::ARRAY_BUFFER, 0, data);
}

void Engine::vertexAttribPointer(GL::IGLBuffer* buffer, unsigned int indx,
                                 int size, unsigned int type, bool normalized,
                                 int stride, int offset)
{
  bool changed = false;
  if (_currentBufferPointers.find(indx) == _currentBufferPointers.end()) {
    changed = true;
    _currentBufferPointers[indx]
      = BufferPointer(indx, size, type, normalized, stride, offset, buffer);
  }
  else {
    BufferPointer& pointer = _currentBufferPointers[indx];
    if (pointer.buffer != buffer) {
      pointer.buffer = buffer;
      changed        = true;
    }
    if (pointer.size != size) {
      pointer.size = size;
      changed      = true;
    }
    if (pointer.type != type) {
      pointer.type = type;
      changed      = true;
    }
    if (pointer.normalized != normalized) {
      pointer.normalized = normalized;
      changed            = true;
    }
    if (pointer.stride != stride) {
      pointer.stride = stride;
      changed        = true;
    }
    if (pointer.offset != offset) {
      pointer.offset = offset;
      changed        = true;
    }
  }

  if (changed) {
    bindArrayBuffer(buffer);
    _gl->vertexAttribPointer(indx, size, type, normalized, stride, offset);
  }
}

void Engine::bindBuffersDirectly(GL::IGLBuffer* vertexBuffer,
                                 GL::IGLBuffer* indexBuffer,
                                 const std::vector<float>& vertexDeclaration,
                                 int vertexStrideSize, Effect* effect)
{
  if (_cachedVertexBuffers != vertexBuffer
      || _cachedEffectForVertexBuffers != effect) {
    _cachedVertexBuffers          = vertexBuffer;
    _cachedEffectForVertexBuffers = effect;

    size_t attributesCount = effect->getAttributesCount();

    int offset = 0;
    for (unsigned int index = 0; index < attributesCount; ++index) {
      if (index < vertexDeclaration.size()) {
        int vertexDeclarationi = static_cast<int>(vertexDeclaration[index]);
        int order              = effect->getAttributeLocation(index);

        if (order >= 0) {
          unsigned int _order = static_cast<unsigned int>(order);
          if (_order + 1 > _vertexAttribArraysEnabled.size()) {
            _gl->enableVertexAttribArray(_order);
            _vertexAttribArraysEnabled.resize(_order + 1);
            _vertexAttribArraysEnabled[_order] = true;
          }

          _gl->vertexAttribPointer(static_cast<unsigned int>(order),
                                   vertexDeclarationi, GL::FLOAT, false,
                                   vertexStrideSize, offset);
        }
        offset += vertexDeclarationi * 4;
      }
      else {
        // disable effect attributes that have no data
        int order = effect->getAttributeLocation(index);
        if (order >= 0) {
          unsigned int _order = static_cast<unsigned int>(order);
          if (_order + 1 <= _vertexAttribArraysEnabled[_order]) {
            _gl->disableVertexAttribArray(_order);
            _vertexAttribArraysEnabled[_order] = false;
          }
        }
      }
    }
  }

  if (_cachedIndexBuffer != indexBuffer) {
    _cachedIndexBuffer = indexBuffer;
    bindIndexBuffer(indexBuffer);
    _uintIndicesCurrentlySet = indexBuffer->is32Bits;
  }
}

void Engine::bindBuffers(
  const std::unordered_map<std::string, VertexBuffer*>& vertexBuffers,
  GL::IGLBuffer* indexBuffer, Effect* effect)
{
  if (_cachedVertexBuffersMap != vertexBuffers
      || _cachedEffectForVertexBuffers != effect) {
    _cachedVertexBuffersMap       = vertexBuffers;
    _cachedEffectForVertexBuffers = effect;

    std::vector<std::string>& attributes = effect->getAttributesNames();

    for (unsigned int index = 0; index < attributes.size(); ++index) {
      int order = effect->getAttributeLocation(index);

      if (order >= 0) {
        unsigned int _order      = static_cast<unsigned int>(order);
        const auto& vertexBuffer = vertexBuffers.at(attributes[index]);

        if (!vertexBuffer) {
          if (_order + 1 <= _vertexAttribArraysEnabled.size()) {
            _gl->disableVertexAttribArray(_order);
            _vertexAttribArraysEnabled[_order] = false;
          }
          continue;
        }

        if (_order + 1 > _vertexAttribArraysEnabled.size()) {
          _gl->enableVertexAttribArray(_order);
          _vertexAttribArraysEnabled.resize(_order + 1);
          _vertexAttribArraysEnabled[_order] = true;
        }

        auto buffer = vertexBuffer->getBuffer();
        vertexAttribPointer(buffer, _order,
                            static_cast<int>(vertexBuffer->getSize()),
                            GL::FLOAT, false, vertexBuffer->getStrideSize() * 4,
                            static_cast<int>(vertexBuffer->getOffset() * 4));

        if (vertexBuffer->getIsInstanced()) {
          _caps.instancedArrays->vertexAttribDivisorANGLE(_order, 1);
          _currentInstanceLocations.emplace_back(order);
          _currentInstanceBuffers.emplace_back(buffer);
        }
      }
    }
  }

  if (indexBuffer != nullptr && _cachedIndexBuffer != indexBuffer) {
    _cachedIndexBuffer = indexBuffer;
    bindIndexBuffer(indexBuffer);
    _uintIndicesCurrentlySet = indexBuffer->is32Bits;
  }
}

void Engine::unbindInstanceAttributes()
{
  GL::IGLBuffer* boundBuffer = nullptr;
  for (size_t i = 0, ul = _currentInstanceLocations.size(); i < ul; ++i) {
    auto instancesBuffer = _currentInstanceBuffers[i];
    if (boundBuffer != instancesBuffer) {
      boundBuffer = instancesBuffer;
      bindArrayBuffer(instancesBuffer);
    }
    auto offsetLocation
      = static_cast<unsigned int>(_currentInstanceLocations[i]);
    _caps.instancedArrays->vertexAttribDivisorANGLE(offsetLocation, 0);
  }
  _currentInstanceBuffers.clear();
  _currentInstanceLocations.clear();
}

bool Engine::_releaseBuffer(GL::IGLBuffer* buffer)
{
  --buffer->references;

  if (buffer->references == 0) {
    _gl->deleteBuffer(buffer);
    return true;
  }

  return false;
}

Engine::GLBufferPtr Engine::createInstancesBuffer(unsigned int capacity)
{
  auto buffer = _gl->createBuffer();

  buffer->capacity = capacity;

  bindArrayBuffer(buffer.get());
  _gl->bufferData(GL::ARRAY_BUFFER, capacity, GL::DYNAMIC_DRAW);
  return buffer;
}

void Engine::deleteInstancesBuffer(GL::IGLBuffer* buffer)
{
  _gl->deleteBuffer(buffer);
}

void Engine::updateAndBindInstancesBuffer(GL::IGLBuffer* instancesBuffer,
                                          const Float32Array& data,
                                          const Uint32Array& offsetLocations)
{
  _gl->bindBuffer(GL::ARRAY_BUFFER, instancesBuffer);
  _gl->bufferSubData(GL::ARRAY_BUFFER, 0, data);

  for (unsigned int index = 0; index < 4; ++index) {
    auto& offsetLocation = offsetLocations[index];
    _gl->enableVertexAttribArray(offsetLocation);

    if (offsetLocation > _vertexAttribArraysEnabled.size()) {
      _gl->enableVertexAttribArray(offsetLocation);
      _vertexAttribArraysEnabled.resize(offsetLocation + 1);
      _vertexAttribArraysEnabled[offsetLocation] = true;
    }

    vertexAttribPointer(instancesBuffer, offsetLocation, 4, GL::FLOAT, false,
                        64, static_cast<int>(index * 16));
    _caps.instancedArrays->vertexAttribDivisorANGLE(offsetLocation, 1);
    _currentInstanceLocations.emplace_back(offsetLocation);
    _currentInstanceBuffers.emplace_back(instancesBuffer);
  }
}

void Engine::updateAndBindInstancesBuffer(
  GL::IGLBuffer* instancesBuffer, const Float32Array& data,
  const std::vector<InstancingAttributeInfo>& offsetLocations)
{
  _gl->bindBuffer(GL::ARRAY_BUFFER, instancesBuffer);
  _gl->bufferSubData(GL::ARRAY_BUFFER, 0, data);

  int stride = 0;
  for (unsigned int i = 0; i < offsetLocations.size(); ++i) {
    stride += offsetLocations[i].attributeSize * 4;
  }

  for (size_t i = 0; i < offsetLocations.size(); ++i) {
    const InstancingAttributeInfo& ai = offsetLocations[i];

    if (ai.index > _vertexAttribArraysEnabled.size()) {
      _gl->enableVertexAttribArray(ai.index);
      _vertexAttribArraysEnabled.resize(ai.index + 1);
      _vertexAttribArraysEnabled[ai.index] = true;
    }

    _gl->enableVertexAttribArray(ai.index);
    vertexAttribPointer(instancesBuffer, ai.index, ai.attributeSize,
                        ai.attribyteType, ai.normalized, stride, ai.offset);
    _caps.instancedArrays->vertexAttribDivisorANGLE(ai.index, 1);
    _currentInstanceLocations.emplace_back(ai.index);
    _currentInstanceBuffers.emplace_back(instancesBuffer);
  }
}

void Engine::applyStates()
{
  _depthCullingState->apply(*_gl);
  _stencilState->apply(*_gl);
  _alphaState->apply(*_gl);
}

void Engine::draw(bool useTriangles, unsigned int indexStart, size_t indexCount,
                  size_t instancesCount)
{
  // Apply states
  applyStates();
  _drawCalls.addCount(1, false);

  // Render
  GL::GLenum indexFormat
    = _uintIndicesCurrentlySet ? GL::UNSIGNED_INT : GL::UNSIGNED_SHORT;
  unsigned int mult = _uintIndicesCurrentlySet ? 4 : 2;

  if (instancesCount) {
#if 0
    _caps.instancedArrays->drawElementsInstancedANGLE(
      useTriangles ? GL::TRIANGLES : GL::LINES, indexCount, indexFormat,
      indexStart * mult, instancesCount);
#endif
    return;
  }

  _gl->drawElements(useTriangles ? GL::TRIANGLES : GL::LINES,
                    static_cast<int>(indexCount), indexFormat,
                    indexStart * mult);
}

void Engine::drawPointClouds(int verticesStart, size_t verticesCount,
                             size_t instancesCount)
{
  // Apply states
  applyStates();
  _drawCalls.addCount(1, false);

  if (instancesCount) {
    _caps.instancedArrays->drawArraysInstancedANGLE(
      GL::POINTS, verticesStart, static_cast<int>(verticesCount),
      static_cast<int>(instancesCount));
    return;
  }

  _gl->drawArrays(GL::POINTS, verticesStart, static_cast<int>(verticesCount));
}

void Engine::drawUnIndexed(bool useTriangles, int verticesStart,
                           size_t verticesCount, size_t instancesCount)
{
  // Apply states
  applyStates();
  _drawCalls.addCount(1, false);

  if (instancesCount > 0) {
    _caps.instancedArrays->drawArraysInstancedANGLE(
      useTriangles ? GL::TRIANGLES : GL::LINES, verticesStart,
      static_cast<int>(verticesCount), static_cast<int>(instancesCount));
    return;
  }

  _gl->drawArrays(useTriangles ? GL::TRIANGLES : GL::LINES, verticesStart,
                  static_cast<int>(verticesCount));
}

// Shaders
void Engine::_releaseEffect(Effect* effect)
{
  if (std_util::contains(_compiledEffects, effect->_key)) {
    _compiledEffects.erase(effect->_key);
    if (effect->getProgram()) {
      _gl->deleteProgram(effect->getProgram());
    }
  }
}

Effect* Engine::createEffect(
  std::unordered_map<std::string, std::string>& baseName,
  const std::vector<std::string>& attributesNames,
  const std::vector<std::string>& uniformsNames,
  const std::vector<std::string>& samplers, const std::string& defines,
  EffectFallbacks* fallbacks,
  const std::function<void(const Effect* effect)>& onCompiled,
  const std::function<void(const Effect* effect, const std::string& errors)>&
    onError,
  const std::unordered_map<std::string, unsigned int>& indexParameters)
{
  std::string vertex
    = std_util::contains(baseName, "vertexElement") ?
        baseName["vertexElement"] :
        std_util::contains(baseName, "vertex") ? baseName["vertex"] : "vertex";
  std::string fragment = std_util::contains(baseName, "fragmentElement") ?
                           baseName["fragmentElement"] :
                           std_util::contains(baseName, "fragment") ?
                           baseName["fragment"] :
                           "fragment";

  std::string name = vertex + "+" + fragment + "@" + defines;
  if (std_util::contains(_compiledEffects, name)) {
    return _compiledEffects[name].get();
  }

  auto effect = std_util::make_unique<Effect>(
    baseName, attributesNames, uniformsNames, samplers, this, defines,
    fallbacks, onCompiled, onError, indexParameters);
  auto _effect           = effect.get();
  effect->_key           = name;
  _compiledEffects[name] = std::move(effect);

  return _effect;
}

Effect* Engine::createEffect(
  const std::string& baseName, const std::vector<std::string>& attributesNames,
  const std::vector<std::string>& uniformsNames,
  const std::vector<std::string>& samplers, const std::string& defines,
  EffectFallbacks* fallbacks,
  const std::function<void(const Effect* effect)>& onCompiled,
  const std::function<void(const Effect* effect, const std::string& errors)>&
    onError,
  const std::unordered_map<std::string, unsigned int>& indexParameters)
{
  std::string name = baseName + "+" + baseName + "@" + defines;
  if (std_util::contains(_compiledEffects, name)) {
    return _compiledEffects[name].get();
  }

  auto effect = std_util::make_unique<Effect>(
    baseName, attributesNames, uniformsNames, samplers, this, defines,
    fallbacks, onCompiled, onError, indexParameters);
  auto _effect           = effect.get();
  effect->_key           = name;
  _compiledEffects[name] = std::move(effect);

  return _effect;
}

Effect* Engine::createEffectForParticles(
  const std::string& fragmentName,
  const std::vector<std::string>& uniformsNames,
  const std::vector<std::string>& samplers, const std::string& defines,
  EffectFallbacks* fallbacks,
  const std::function<void(const Effect* effect)>& onCompiled,
  const std::function<void(const Effect* effect, const std::string& errors)>&
    onError)
{
  std::unordered_map<std::string, std::string> baseName;
  baseName["vertex"]                      = "particles";
  baseName["fragmentElement"]             = fragmentName;
  std::vector<std::string> _uniformsNames = {"view", "projection"};
  std_util::concat(_uniformsNames, uniformsNames);
  std::vector<std::string> _samplers = {"diffuseSampler"};
  std_util::concat(_samplers, samplers);

  return createEffect(baseName, {"position", "color", "options"},
                      _uniformsNames, _samplers, defines, fallbacks, onCompiled,
                      onError);
}

std::unique_ptr<GL::IGLProgram> Engine::createShaderProgram(
  const std::string& vertexCode, const std::string& fragmentCode,
  const std::string& defines, GL::IGLRenderingContext* iGl)
{
  auto gl = iGl ? iGl : _gl;

  auto vertexShader = Engine::CompileShader(gl, vertexCode, "vertex", defines);
  auto fragmentShader
    = Engine::CompileShader(gl, fragmentCode, "fragment", defines);

  auto shaderProgram = gl->createProgram();
  gl->attachShader(shaderProgram, vertexShader);
  gl->attachShader(shaderProgram, fragmentShader);

  bool linked = gl->linkProgram(shaderProgram);

  if (!linked) {
    const std::string& error = gl->getProgramInfoLog(shaderProgram);
    if (!error.empty()) {
      BABYLON_LOG_ERROR("Engine", error);
      return nullptr;
    }
  }

  gl->deleteShader(vertexShader);
  gl->deleteShader(fragmentShader);

  return shaderProgram;
}

std::unordered_map<std::string, std::unique_ptr<GL::IGLUniformLocation>>
Engine::getUniforms(GL::IGLProgram* shaderProgram,
                    const std::vector<std::string>& uniformsNames)
{
  std::unordered_map<std::string, std::unique_ptr<GL::IGLUniformLocation>>
    results;

  for (auto& name : uniformsNames) {
    auto uniform = _gl->getUniformLocation(shaderProgram, name);
    if (uniform) {
      results[name] = std::move(uniform);
    }
  }

  return results;
}

Int32Array
Engine::getAttributes(GL::IGLProgram* shaderProgram,
                      const std::vector<std::string>& attributesNames)
{
  Int32Array results;

  for (auto& attributesName : attributesNames) {
    results.emplace_back(_gl->getAttribLocation(shaderProgram, attributesName));
  }

  return results;
}

void Engine::enableEffect(Effect* effect)
{
  // Use program
  setProgram(effect->getProgram());

  _currentEffect = effect;

  if (effect->onBind) {
    effect->onBind(effect);
  }
}

void Engine::setIntArray(GL::IGLUniformLocation* uniform,
                         const Int32Array& array)
{
  if (!uniform) {
    return;
  }

  _gl->uniform1iv(uniform, array);
}

void Engine::setIntArray2(GL::IGLUniformLocation* uniform,
                          const Int32Array& array)
{
  if (!uniform || array.size() % 2 != 0) {
    return;
  }

  _gl->uniform2iv(uniform, array);
}

void Engine::setIntArray3(GL::IGLUniformLocation* uniform,
                          const Int32Array& array)
{
  if (!uniform || array.size() % 3 != 0) {
    return;
  }

  _gl->uniform3iv(uniform, array);
}

void Engine::setIntArray4(GL::IGLUniformLocation* uniform,
                          const Int32Array& array)
{
  if (!uniform || array.size() % 4 != 0) {
    return;
  }

  _gl->uniform4iv(uniform, array);
}

void Engine::setFloatArray(GL::IGLUniformLocation* uniform,
                           const Float32Array& array)
{
  if (!uniform) {
    return;
  }

  _gl->uniform1fv(uniform, array);
}

void Engine::setFloatArray2(GL::IGLUniformLocation* uniform,
                            const Float32Array& array)
{
  if (!uniform || array.size() % 2 != 0) {
    return;
  }

  _gl->uniform2fv(uniform, array);
}

void Engine::setFloatArray3(GL::IGLUniformLocation* uniform,
                            const Float32Array& array)
{
  if (!uniform || array.size() % 3 != 0) {
    return;
  }

  _gl->uniform3fv(uniform, array);
}

void Engine::setFloatArray4(GL::IGLUniformLocation* uniform,
                            const Float32Array& array)
{
  if (!uniform || array.size() % 4 != 0) {
    return;
  }

  _gl->uniform4fv(uniform, array);
}

void Engine::setArray(GL::IGLUniformLocation* uniform,
                      const Float32Array& array)
{
  if (!uniform) {
    return;
  }

  _gl->uniform1fv(uniform, array);
}

void Engine::setArray2(GL::IGLUniformLocation* uniform,
                       const Float32Array& array)
{
  if (!uniform || array.size() % 2 != 0) {
    return;
  }

  _gl->uniform2fv(uniform, array);
}

void Engine::setArray3(GL::IGLUniformLocation* uniform,
                       const Float32Array& array)
{
  if (!uniform || array.size() % 3 != 0) {
    return;
  }

  _gl->uniform3fv(uniform, array);
}

void Engine::setArray4(GL::IGLUniformLocation* uniform,
                       const Float32Array& array)
{
  if (!uniform || array.size() % 4 != 0) {
    return;
  }

  _gl->uniform4fv(uniform, array);
}

void Engine::setMatrices(GL::IGLUniformLocation* uniform,
                         const Float32Array& matrices)
{
  if (!uniform) {
    return;
  }

  _gl->uniformMatrix4fv(uniform, false, matrices);
}

void Engine::setMatrix(GL::IGLUniformLocation* uniform, const Matrix& matrix)
{
  if (!uniform) {
    return;
  }

  _gl->uniformMatrix4fv(uniform, false, matrix.toArray());
}

void Engine::setMatrix3x3(GL::IGLUniformLocation* uniform,
                          const Float32Array& matrix)
{
  if (!uniform) {
    return;
  }

  _gl->uniformMatrix3fv(uniform, false, matrix);
}

void Engine::setMatrix2x2(GL::IGLUniformLocation* uniform,
                          const Float32Array& matrix)
{
  if (!uniform) {
    return;
  }

  _gl->uniformMatrix2fv(uniform, false, matrix);
}

void Engine::setFloat(GL::IGLUniformLocation* uniform, float value)
{
  if (!uniform) {
    return;
  }

  _gl->uniform1f(uniform, value);
}

void Engine::setFloat2(GL::IGLUniformLocation* uniform, float x, float y)
{
  if (!uniform) {
    return;
  }

  _gl->uniform2f(uniform, x, y);
}

void Engine::setFloat3(GL::IGLUniformLocation* uniform, float x, float y,
                       float z)
{
  if (!uniform) {
    return;
  }

  _gl->uniform3f(uniform, x, y, z);
}

void Engine::setBool(GL::IGLUniformLocation* uniform, int value)
{
  if (!uniform) {
    return;
  }

  _gl->uniform1i(uniform, value);
}

void Engine::setFloat4(GL::IGLUniformLocation* uniform, float x, float y,
                       float z, float w)
{
  if (!uniform) {
    return;
  }

  _gl->uniform4f(uniform, x, y, z, w);
}

void Engine::setColor3(GL::IGLUniformLocation* uniform, const Color3& color3)
{
  if (!uniform) {
    return;
  }

  _gl->uniform3f(uniform, color3.r, color3.g, color3.b);
}

void Engine::setColor4(GL::IGLUniformLocation* uniform, const Color3& color3,
                       float alpha)
{
  if (!uniform) {
    return;
  }

  _gl->uniform4f(uniform, color3.r, color3.g, color3.b, alpha);
}

/** States **/
void Engine::setState(bool culling, float zOffset, bool force, bool reverseSide)
{
  // Culling
  auto showSide = reverseSide ? GL::FRONT : GL::BACK;
  auto hideSide = reverseSide ? GL::BACK : GL::FRONT;
  auto cullFace = cullBackFaces ? showSide : hideSide;

  if (_depthCullingState->cull() != culling || force
      || _depthCullingState->cullFace() != static_cast<int>(cullFace)) {
    if (culling) {
      _depthCullingState->setCullFace(static_cast<int>(cullFace));
      _depthCullingState->setCull(true);
    }
    else {
      _depthCullingState->setCull(false);
    }
  }

  // Z offset
  _depthCullingState->setZOffset(zOffset);
}

void Engine::setDepthBuffer(bool enable)
{
  _depthCullingState->setDepthTest(enable);
}

bool Engine::getDepthWrite() const
{
  return _depthCullingState->depthMask();
}

void Engine::setDepthWrite(bool enable)
{
  _depthCullingState->setDepthMask(enable);
}

void Engine::setColorWrite(bool enable)
{
  _gl->colorMask(enable, enable, enable, enable);
}

void Engine::setAlphaMode(int mode, bool noDepthWriteChange)
{
  if (_alphaMode == mode) {
    return;
  }

  switch (mode) {
    case Engine::ALPHA_DISABLE:
      _alphaState->setAlphaBlend(false);
      break;
    case Engine::ALPHA_COMBINE:
      _alphaState->setAlphaBlendFunctionParameters(
        GL::SRC_ALPHA, GL::ONE_MINUS_SRC_ALPHA, GL::ONE, GL::ONE);
      _alphaState->setAlphaBlend(true);
      break;
    case Engine::ALPHA_ONEONE:
      _alphaState->setAlphaBlendFunctionParameters(GL::ONE, GL::ONE, GL::ZERO,
                                                   GL::ONE);
      _alphaState->setAlphaBlend(true);
      break;
    case Engine::ALPHA_ADD:
      _alphaState->setAlphaBlendFunctionParameters(GL::SRC_ALPHA, GL::ONE,
                                                   GL::ZERO, GL::ONE);
      _alphaState->setAlphaBlend(true);
      break;
    case Engine::ALPHA_SUBTRACT:
      _alphaState->setAlphaBlendFunctionParameters(
        GL::ZERO, GL::ONE_MINUS_SRC_COLOR, GL::ONE, GL::ONE);
      _alphaState->setAlphaBlend(true);
      break;
    case Engine::ALPHA_MULTIPLY:
      _alphaState->setAlphaBlendFunctionParameters(GL::DST_COLOR, GL::ZERO,
                                                   GL::ONE, GL::ONE);
      _alphaState->setAlphaBlend(true);
      break;
    case Engine::ALPHA_MAXIMIZED:
      _alphaState->setAlphaBlendFunctionParameters(
        GL::SRC_ALPHA, GL::ONE_MINUS_SRC_COLOR, GL::ONE, GL::ONE);
      _alphaState->setAlphaBlend(true);
      break;
    default:
      break;
  }
  if (!noDepthWriteChange) {
    setDepthWrite(mode == Engine::ALPHA_DISABLE);
  }
  _alphaMode = mode;
}

int Engine::getAlphaMode() const
{
  return _alphaMode;
}

void Engine::setAlphaTesting(bool enable)
{
  _alphaTest = enable;
}

bool Engine::getAlphaTesting() const
{
  return _alphaTest;
}

// Textures
void Engine::wipeCaches()
{
  resetTextureCache();
  _currentEffect = nullptr;

  _stencilState->reset();
  _depthCullingState->reset();
  setDepthFunctionToLessOrEqual();
  _alphaState->reset();

  _cachedVertexBuffers          = nullptr;
  _cachedIndexBuffer            = nullptr;
  _cachedEffectForVertexBuffers = nullptr;
}

void Engine::setSamplingMode(GL::IGLTexture* texture, unsigned int samplingMode)
{
  _bindTextureDirectly(GL::TEXTURE_2D, texture);

  auto magFilter = GL::NEAREST;
  auto minFilter = GL::NEAREST;

  if (samplingMode == Texture::BILINEAR_SAMPLINGMODE) {
    magFilter = GL::LINEAR;
    minFilter = GL::LINEAR;
  }
  else if (samplingMode == Texture::TRILINEAR_SAMPLINGMODE) {
    magFilter = GL::LINEAR;
    minFilter = GL::LINEAR_MIPMAP_LINEAR;
  }

  _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MAG_FILTER,
                     static_cast<int>(magFilter));
  _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MIN_FILTER,
                     static_cast<int>(minFilter));

  _bindTextureDirectly(GL::TEXTURE_2D, nullptr);

  texture->samplingMode = samplingMode;
}

std::string&
Engine::setTextureFormatToUse(const std::vector<std::string>& formatsAvailable)
{
  for (size_t i = 0; i < _texturesSupported.size(); ++i) {
    // code to allow the formats to be added as they can be developed / hw
    // tested
    if (_texturesSupported[i] == ".astc") {
      continue;
    }
    if (_texturesSupported[i] == ".pvr") {
      continue;
    }
    if (_texturesSupported[i] == ".etc1") {
      continue;
    }
    if (_texturesSupported[i] == ".etc2") {
      continue;
    }

    for (const auto& formatAvailable : formatsAvailable) {
      if (_texturesSupported[i] == String::toLowerCase(formatAvailable)) {
        _textureFormatInUse = _texturesSupported[i];
        return _textureFormatInUse;
      }
    }
  }
  // actively set format to nothing, to allow this to be called more than once
  // and possibly fail the 2nd time
  _textureFormatInUse.clear();
  return _textureFormatInUse;
}

GL::IGLTexture* Engine::createTexture(const std::vector<std::string>& list,
                                      bool noMipmap, bool invertY, Scene* scene,
                                      unsigned int samplingMode,
                                      const std::function<void()>& onLoad,
                                      const std::function<void()>& onError,
                                      Buffer* buffer)
{
  if (list.empty()) {
    return nullptr;
  }

  return createTexture(list[0], noMipmap, invertY, scene, samplingMode, onLoad,
                       onError, buffer);
}

GL::IGLTexture* Engine::createTexture(const std::string& _url, bool noMipmap,
                                      bool invertY, Scene* scene,
                                      unsigned int samplingMode,
                                      const std::function<void()>& /*onLoad*/,
                                      const std::function<void()>& onError,
                                      Buffer* /*buffer*/)
{
  auto texture  = _gl->createTexture();
  auto _texture = texture.get();

  std::string url = _url;
  std::string extension;
  bool fromDataBool = false;
  std::vector<std::string> fromDataArray;

  if ((url.size() >= 5) && (url.substr(0, 5) == "data:")) {
    fromDataBool = true;
  }

  if (!fromDataBool) {
    if (url.size() >= 4) {
      extension = String::toLowerCase(url.substr(url.size() - 4, 4));
    }
  }
  else {
    std::string oldUrl = url;
    fromDataArray      = String::split(oldUrl, ':');
    url                = oldUrl;
    if ((fromDataArray.size() >= 2) && (fromDataArray[1].size() >= 4)) {
      extension = String::toLowerCase(
        fromDataArray[1].substr(fromDataArray[1].size() - 4, 4));
    }
  }

  bool isDDS = (extension == ".dds");
  bool isTGA = (extension == ".tga");

  scene->_addPendingData(_texture);
  _texture->url          = url;
  _texture->noMipmap     = noMipmap;
  _texture->references   = 1;
  _texture->samplingMode = samplingMode;
  // texture->onLoadedCallbacks = {onLoad};
  _loadedTexturesCache.emplace_back(std::move(texture));

  auto onerror = [&scene, &_texture, &onError](const std::string& msg) {
    scene->_removePendingData(_texture);

    if (onError) {
      BABYLON_LOG_ERROR("Engine", msg);
      onError();
    }
  };
  std::function<void(const Image& img)> onload = nullptr;

  if (isTGA) {
    // Not implemented yet
  }
  else if (isDDS) {
    // Not implemented yet
  }
  else {
    onload = [&](const Image& img) {
      Engine::PrepareGLTexture(
        _texture, _gl, scene, img.width, img.height, noMipmap, false,
        [&](int potWidth, int potHeight) {
          bool isPot = (img.width == potWidth && img.height == potHeight);
          isPot      = true;
          if (isPot) {
            _gl->texImage2D(GL::TEXTURE_2D, 0, GL::RGBA, img.width, img.height,
                            0, GL::RGBA, GL::UNSIGNED_BYTE, img.data);
          }
        },
        invertY, samplingMode);
    };
  }

  if (fromDataArray.empty()) {
    Tools::LoadImage(url, onload, onerror);
  }
  else {
    // Not implemented yet
  }

  return _texture;
}

GL::GLenum Engine::_getInternalFormat(int format) const
{
  GL::GLenum internalFormat = GL::RGBA;
  switch (format) {
    case Engine::TEXTUREFORMAT_ALPHA:
      internalFormat = GL::ALPHA;
      break;
    case Engine::TEXTUREFORMAT_LUMINANCE:
      internalFormat = GL::LUMINANCE;
      break;
    case Engine::TEXTUREFORMAT_LUMINANCE_ALPHA:
      internalFormat = GL::LUMINANCE_ALPHA;
      break;
    case Engine::TEXTUREFORMAT_RGB:
      internalFormat = GL::RGB;
      break;
    case Engine::TEXTUREFORMAT_RGBA:
      internalFormat = GL::RGBA;
      break;
    default:
      break;
  }

  return internalFormat;
}

void Engine::updateRawTexture(GL::IGLTexture* texture, const Uint8Array& data,
                              int format, bool invertY,
                              const std::string& compression)
{
  auto internalFormat = _getInternalFormat(format);
  _bindTextureDirectly(GL::TEXTURE_2D, texture);
  _gl->pixelStorei(GL::UNPACK_FLIP_Y_WEBGL, invertY ? 1 : 0);

  if (texture->_width % 4 != 0) {
    _gl->pixelStorei(GL::UNPACK_ALIGNMENT, 1);
  }

  if (!compression.empty()) {
    //_gl->compressedTexImage2D(GL::TEXTURE_2D, 0, getCaps().s3tc[compression],
    //                          texture->_width, texture->_height, 0, data);
  }
  else {
    GL::GLint _internalFormat = static_cast<GL::GLint>(internalFormat);
    _gl->texImage2D(GL::TEXTURE_2D, 0, _internalFormat, texture->_width,
                    texture->_height, 0, internalFormat, GL::UNSIGNED_BYTE,
                    data);
  }

  if (texture->generateMipMaps) {
    _gl->generateMipmap(GL::TEXTURE_2D);
  }
  _bindTextureDirectly(GL::TEXTURE_2D, nullptr);
  _activeTexturesCache.clear();
  texture->isReady = true;
}

GL::IGLTexture* Engine::createRawTexture(const Uint8Array& data, int width,
                                         int height, int format,
                                         bool generateMipMaps, bool invertY,
                                         unsigned int samplingMode,
                                         const std::string& compression)
{
  auto texture          = _gl->createTexture();
  auto _texture         = texture.get();
  _texture->_baseWidth  = width;
  _texture->_baseHeight = height;
  _texture->_width      = width;
  _texture->_height     = height;
  _texture->references  = 1;

  updateRawTexture(_texture, data, format, invertY, compression);
  _bindTextureDirectly(GL::TEXTURE_2D, _texture);

  // Filters
  SamplingParameters filters
    = GetSamplingParameters(samplingMode, generateMipMaps);

  _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MAG_FILTER, filters.mag);
  _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MIN_FILTER, filters.min);
  _bindTextureDirectly(GL::TEXTURE_2D, nullptr);

  _texture->samplingMode = samplingMode;

  _loadedTexturesCache.emplace_back(std::move(texture));

  return _texture;
}

GL::IGLTexture* Engine::createDynamicTexture(int width, int height,
                                             bool generateMipMaps,
                                             unsigned int samplingMode)
{
  auto texture          = _gl->createTexture();
  auto _texture         = texture.get();
  _texture->_baseWidth  = width;
  _texture->_baseHeight = height;

  if (generateMipMaps) {
    width  = Tools::GetExponentOfTwo(width, _caps.maxTextureSize);
    height = Tools::GetExponentOfTwo(height, _caps.maxTextureSize);
  }

  resetTextureCache();
  _texture->_width          = width;
  _texture->_height         = height;
  _texture->isReady         = false;
  _texture->generateMipMaps = generateMipMaps;
  _texture->references      = 1;
  _texture->samplingMode    = samplingMode;

  updateTextureSamplingMode(samplingMode, _texture);

  _loadedTexturesCache.emplace_back(std::move(texture));

  return _texture;
}

void Engine::updateTextureSamplingMode(unsigned int samplingMode,
                                       GL::IGLTexture* texture)
{
  auto filters = GetSamplingParameters(samplingMode, texture->generateMipMaps);

  if (texture->isCube) {
    _bindTextureDirectly(GL::TEXTURE_CUBE_MAP, texture);

    _gl->texParameteri(GL::TEXTURE_CUBE_MAP, GL::TEXTURE_MAG_FILTER,
                       filters.mag);
    _gl->texParameteri(GL::TEXTURE_CUBE_MAP, GL::TEXTURE_MIN_FILTER,
                       filters.min);
    _bindTextureDirectly(GL::TEXTURE_CUBE_MAP, nullptr);
  }
  else {
    _bindTextureDirectly(GL::TEXTURE_2D, texture);

    _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MAG_FILTER, filters.mag);
    _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MIN_FILTER, filters.min);
    _bindTextureDirectly(GL::TEXTURE_2D, nullptr);
  }
}

void Engine::updateDynamicTexture(GL::IGLTexture* texture, ICanvas* /*canvas*/,
                                  bool invertY, bool premulAlpha)
{
  _bindTextureDirectly(GL::TEXTURE_2D, texture);
  _gl->pixelStorei(GL::UNPACK_FLIP_Y_WEBGL, invertY ? 1 : 0);
  if (premulAlpha) {
    _gl->pixelStorei(GL::UNPACK_PREMULTIPLY_ALPHA_WEBGL, 1);
  }
  //_gl->texImage2D(GL::TEXTURE_2D, 0, GL::RGBA, GL::RGBA, GL::UNSIGNED_BYTE,
  //                canvas);
  if (texture->generateMipMaps) {
    _gl->generateMipmap(GL::TEXTURE_2D);
  }
  _bindTextureDirectly(GL::TEXTURE_2D, nullptr);
  if (premulAlpha) {
    _gl->pixelStorei(GL::UNPACK_PREMULTIPLY_ALPHA_WEBGL, 0);
  }
  resetTextureCache();
  texture->isReady = true;
}

GL::IGLTexture*
Engine::createRenderTargetTexture(ISize size,
                                  const RenderTargetTextureOptions& options)
{
  // old version had a "generateMipMaps" arg instead of options.
  // if options.generateMipMaps is undefined, consider that options itself if
  // the generateMipmaps value in the same way, generateDepthBuffer is defaulted
  // to true
  bool generateMipMaps     = options.generateMipMaps;
  bool generateDepthBuffer = options.generateDepthBuffer;
  bool generateStencilBuffer
    = generateDepthBuffer && options.generateStencilBuffer;

  unsigned int type         = options.type;
  unsigned int samplingMode = options.samplingMode;

  if (type == Engine::TEXTURETYPE_FLOAT && !_caps.textureFloatLinearFiltering) {
    // if floating point linear (GL::FLOAT) then force to NEAREST_SAMPLINGMODE
    samplingMode = Texture::NEAREST_SAMPLINGMODE;
  }
  else if (type == Engine::TEXTURETYPE_HALF_FLOAT
           && !_caps.textureHalfFloatLinearFiltering) {
    // if floating point linear (HALF_FLOAT) then force to NEAREST_SAMPLINGMODE
    samplingMode = Texture::NEAREST_SAMPLINGMODE;
  }

  auto texture  = _gl->createTexture();
  auto _texture = texture.get();
  _bindTextureDirectly(GL::TEXTURE_2D, _texture);

  int width  = size.width;
  int height = size.height;

  SamplingParameters filters
    = GetSamplingParameters(samplingMode, generateMipMaps);

  if (type == Engine::TEXTURETYPE_FLOAT && !_caps.textureFloat) {
    type = Engine::TEXTURETYPE_UNSIGNED_INT;
    BABYLON_LOG_WARN(
      "Engine",
      "Float textures are not supported. Render target forced to "
      "TEXTURETYPE_UNSIGNED_BYTE type");
  }

  _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MAG_FILTER, filters.mag);
  _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MIN_FILTER, filters.min);
  _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_WRAP_S, GL::CLAMP_TO_EDGE);
  _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_WRAP_T, GL::CLAMP_TO_EDGE);

  _gl->texImage2D(GL::TEXTURE_2D, 0, GL::RGBA, width, height, 0, GL::RGBA,
                  GetGLTextureType(type), Uint8Array());

  GLRenderBufferPtr depthStencilBuffer = nullptr;

  // Create the depth/stencil buffer
  if (generateStencilBuffer) {
    depthStencilBuffer = _gl->createRenderbuffer();
    _gl->bindRenderbuffer(GL::RENDERBUFFER, depthStencilBuffer);
    _gl->renderbufferStorage(GL::RENDERBUFFER, GL::DEPTH_STENCIL, width,
                             height);
  }
  else if (generateDepthBuffer) {
    depthStencilBuffer = _gl->createRenderbuffer();
    _gl->bindRenderbuffer(GL::RENDERBUFFER, depthStencilBuffer);
    _gl->renderbufferStorage(GL::RENDERBUFFER, GL::DEPTH_COMPONENT16, width,
                             height);
  }

  // Create the framebuffer
  auto framebuffer = _gl->createFramebuffer();
  bindUnboundFramebuffer(framebuffer.get());

  // Manage attachments
  if (generateStencilBuffer) {
    _gl->framebufferRenderbuffer(GL::FRAMEBUFFER, GL::DEPTH_STENCIL_ATTACHMENT,
                                 GL::RENDERBUFFER, depthStencilBuffer);
  }
  else if (generateDepthBuffer) {
    _gl->framebufferRenderbuffer(GL::FRAMEBUFFER, GL::DEPTH_ATTACHMENT,
                                 GL::RENDERBUFFER, depthStencilBuffer);
  }
  _gl->framebufferTexture2D(GL::FRAMEBUFFER, GL::COLOR_ATTACHMENT0,
                            GL::TEXTURE_2D, _texture, 0);

  if (generateMipMaps) {
    _gl->generateMipmap(GL::TEXTURE_2D);
  }

  // Unbind
  _bindTextureDirectly(GL::TEXTURE_2D, nullptr);
  _gl->bindRenderbuffer(GL::RENDERBUFFER, nullptr);
  bindUnboundFramebuffer(nullptr);

  _texture->_framebuffer = std::move(framebuffer);
  if (generateDepthBuffer) {
    _texture->_depthBuffer = std::move(depthStencilBuffer);
  }
  _texture->_baseWidth      = width;
  _texture->_baseHeight     = height;
  _texture->_width          = width;
  _texture->_height         = height;
  _texture->isReady         = true;
  _texture->generateMipMaps = generateMipMaps;
  _texture->references      = 1;
  _texture->samplingMode    = samplingMode;
  _texture->type            = type;

  resetTextureCache();

  _loadedTexturesCache.emplace_back(std::move(texture));

  return _texture;
}

GL::IGLTexture* Engine::createRenderTargetCubeTexture(
  const ISize& size, const RenderTargetCubeTextureOptions& options)
{
  auto texture  = _gl->createTexture();
  auto _texture = texture.get();

  bool generateMipMaps     = options.generateMipMaps;
  bool generateDepthBuffer = options.generateDepthBuffer;
  bool generateStencilBuffer
    = generateDepthBuffer && options.generateStencilBuffer;

  unsigned int samplingMode = options.samplingMode;

  _texture->isCube          = true;
  _texture->references      = 1;
  _texture->generateMipMaps = generateMipMaps;
  _texture->samplingMode    = samplingMode;

  SamplingParameters filters
    = Engine::GetSamplingParameters(samplingMode, generateMipMaps);

  _bindTextureDirectly(GL::TEXTURE_CUBE_MAP, _texture);

  for (unsigned int face = 0; face < 6; ++face) {
    _gl->texImage2D((GL::TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL::RGBA,
                    size.width, size.height, 0, GL::RGBA, GL::UNSIGNED_BYTE,
                    Uint8Array());
  }

  _gl->texParameteri(GL::TEXTURE_CUBE_MAP, GL::TEXTURE_MAG_FILTER, filters.mag);
  _gl->texParameteri(GL::TEXTURE_CUBE_MAP, GL::TEXTURE_MIN_FILTER, filters.min);
  _gl->texParameteri(GL::TEXTURE_CUBE_MAP, GL::TEXTURE_WRAP_S,
                     GL::CLAMP_TO_EDGE);
  _gl->texParameteri(GL::TEXTURE_CUBE_MAP, GL::TEXTURE_WRAP_T,
                     GL::CLAMP_TO_EDGE);

  // Create the depth buffer
  GLRenderBufferPtr depthStencilBuffer = nullptr;

  // Create the depth/stencil buffer
  if (generateStencilBuffer) {
    depthStencilBuffer = _gl->createRenderbuffer();
    _gl->bindRenderbuffer(GL::RENDERBUFFER, depthStencilBuffer);
    _gl->renderbufferStorage(GL::RENDERBUFFER, GL::DEPTH_STENCIL, size.width,
                             size.height);
  }
  else if (generateDepthBuffer) {
    depthStencilBuffer = _gl->createRenderbuffer();
    _gl->bindRenderbuffer(GL::RENDERBUFFER, depthStencilBuffer);
    _gl->renderbufferStorage(GL::RENDERBUFFER, GL::DEPTH_COMPONENT16,
                             size.width, size.height);
  }
  // Create the framebuffer
  GLFrameBufferPtr framebuffer = _gl->createFramebuffer();
  bindUnboundFramebuffer(framebuffer.get());

  // Manage attachments
  if (generateStencilBuffer) {
    _gl->framebufferRenderbuffer(GL::FRAMEBUFFER, GL::DEPTH_STENCIL_ATTACHMENT,
                                 GL::RENDERBUFFER, depthStencilBuffer);
  }
  else if (generateDepthBuffer) {
    _gl->framebufferRenderbuffer(GL::FRAMEBUFFER, GL::DEPTH_ATTACHMENT,
                                 GL::RENDERBUFFER, depthStencilBuffer);
  }

  // Mipmaps
  if (_texture->generateMipMaps) {
    _bindTextureDirectly(GL::TEXTURE_CUBE_MAP, _texture);
    _gl->generateMipmap(GL::TEXTURE_CUBE_MAP);
  }

  // Unbind
  _bindTextureDirectly(GL::TEXTURE_CUBE_MAP, nullptr);
  _gl->bindRenderbuffer(GL::RENDERBUFFER, nullptr);
  bindUnboundFramebuffer(nullptr);

  _texture->_framebuffer = std::move(framebuffer);
  if (generateDepthBuffer) {
    _texture->_depthBuffer = std::move(depthStencilBuffer);
  }

  _texture->_width  = size.width;
  _texture->_height = size.height;
  _texture->isReady = true;

  resetTextureCache();
  _loadedTexturesCache.emplace_back(std::move(texture));

  return _texture;
}

GL::IGLTexture*
Engine::createCubeTexture(const std::string& /*rootUrl*/, Scene* /*scene*/,
                          const std::vector<std::string>& /*extensions*/,
                          bool /*noMipmap*/,
                          const std::function<void()>& /*onLoad*/,
                          const std::function<void()>& /*onError*/)
{
  return nullptr;
}

void Engine::updateTextureSize(GL::IGLTexture* texture, int width, int height)
{
  texture->_width      = width;
  texture->_height     = height;
  texture->_size       = width * height;
  texture->_baseWidth  = width;
  texture->_baseHeight = height;
}

void Engine::_releaseTexture(GL::IGLTexture* texture)
{
  if (texture->_framebuffer) {
    _gl->deleteFramebuffer(texture->_framebuffer);
  }

  if (texture->_depthBuffer) {
    _gl->deleteRenderbuffer(texture->_depthBuffer);
  }

  _gl->deleteTexture(texture);

  // Unbind channels
  unbindAllTextures();

  _loadedTexturesCache.erase(
    std::remove_if(_loadedTexturesCache.begin(), _loadedTexturesCache.end(),
                   [&texture](const GLTexturePtr& _texture) {
                     return _texture.get() == texture;
                   }),
    _loadedTexturesCache.end());
}

void Engine::setProgram(GL::IGLProgram* program)
{
  if (_currentProgram != program) {
    _gl->useProgram(program);
    _currentProgram = program;
  }
}

void Engine::bindSamplers(Effect* effect)
{
  setProgram(effect->getProgram());
  const auto& samplers = effect->getSamplers();
  for (size_t index = 0; index < samplers.size(); ++index) {
    auto uniform = effect->getUniform(samplers[index]);
    _gl->uniform1i(uniform, static_cast<int>(index));
  }
  _currentEffect = nullptr;
}

void Engine::activateTexture(unsigned int texture)
{
  if (_activeTexture != texture) {
    _gl->activeTexture(texture);
    _activeTexture = texture;
  }
}

void Engine::_bindTextureDirectly(unsigned int target, GL::IGLTexture* texture)
{
  if ((_activeTexturesCache.find(target) != _activeTexturesCache.end())
      && (_activeTexturesCache[_activeTexture] != texture)) {
    _gl->bindTexture(target, texture);
    _activeTexturesCache[_activeTexture] = texture;
  }
}

void Engine::_bindTexture(int channel, GL::IGLTexture* texture)
{
  if (channel < 0) {
    return;
  }

  activateTexture((*_gl)["TEXTURE" + std::to_string(channel)]);
  _bindTextureDirectly(GL::TEXTURE_2D, texture);
}

void Engine::setTextureFromPostProcess(int channel, PostProcess* postProcess)
{
  size_t _ind = static_cast<size_t>(postProcess->_currentRenderTextureInd);
  _bindTexture(channel, postProcess->_textures[_ind]);
}

void Engine::unbindAllTextures()
{
  for (int channel = 0; channel < _caps.maxTexturesImageUnits; ++channel) {
    activateTexture((*_gl)["TEXTURE" + std::to_string(channel)]);
    _bindTextureDirectly(GL::TEXTURE_2D, nullptr);
    _bindTextureDirectly(GL::TEXTURE_CUBE_MAP, nullptr);
  }
}

void Engine::setTexture(int channel, GL::IGLUniformLocation* uniform,
                        BaseTexture* texture)
{
  if (channel < 0) {
    return;
  }

  _gl->uniform1i(uniform, channel);
  _setTexture(static_cast<unsigned>(channel), texture);
}

void Engine::_setTexture(unsigned int channel, BaseTexture* texture)
{
  // Not ready?
  if (!texture || !texture->isReady()) {
    if ((_activeTexturesCache.find(channel) != _activeTexturesCache.end())
        && (_activeTexturesCache[channel] != nullptr)) {
      activateTexture((*_gl)["TEXTURE" + std::to_string(channel)]);
      _bindTextureDirectly(GL::TEXTURE_2D, nullptr);
      _bindTextureDirectly(GL::TEXTURE_CUBE_MAP, nullptr);
    }
    return;
  }

  // Video
  bool alreadyActivated = false;
  /*VideoTexture* videoTexturePointer
    = dynamic_cast<VideoTexture*>(texture);
  if (videoTexturePointer != nullptr) {
    activateTexture(_gl["TEXTURE" + channel]);
    alreadyActivated = true;
    videoTexturePointer->update();
  }
  else*/
  if (texture->delayLoadState == Engine::DELAYLOADSTATE_NOTLOADED) {
    // Delay loading
    texture->delayLoad();
    return;
  }

  auto internalTexture = texture->getInternalTexture();

  if (_activeTexturesCache[channel] == internalTexture) {
    return;
  }

  if (!alreadyActivated) {
    activateTexture((*_gl)["TEXTURE" + std::to_string(channel)]);
  }

  if (internalTexture->isCube) {
    _bindTextureDirectly(GL::TEXTURE_CUBE_MAP, internalTexture);

    if (internalTexture->_cachedCoordinatesMode != texture->coordinatesMode) {
      internalTexture->_cachedCoordinatesMode = texture->coordinatesMode;
      // CUBIC_MODE and SKYBOX_MODE both require CLAMP_TO_EDGE.  All other modes
      // use REPEAT.
      auto textureWrapMode
        = (texture->coordinatesMode != Texture::CUBIC_MODE
           && texture->coordinatesMode != Texture::SKYBOX_MODE) ?
            GL::REPEAT :
            GL::CLAMP_TO_EDGE;
      _gl->texParameteri(GL::TEXTURE_CUBE_MAP, GL::TEXTURE_WRAP_S,
                         textureWrapMode);
      _gl->texParameteri(GL::TEXTURE_CUBE_MAP, GL::TEXTURE_WRAP_T,
                         textureWrapMode);
    }

    _setAnisotropicLevel(GL::TEXTURE_CUBE_MAP, texture);
  }
  else {
    _bindTextureDirectly(GL::TEXTURE_2D, internalTexture);

    if (internalTexture->_cachedWrapU != texture->wrapU) {
      internalTexture->_cachedWrapU = texture->wrapU;

      switch (texture->wrapU) {
        case Texture::WRAP_ADDRESSMODE:
          _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_WRAP_S, GL::REPEAT);
          break;
        case Texture::CLAMP_ADDRESSMODE:
          _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_WRAP_S,
                             GL::CLAMP_TO_EDGE);
          break;
        case Texture::MIRROR_ADDRESSMODE:
          _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_WRAP_S,
                             GL::MIRRORED_REPEAT);
          break;
        default:
          break;
      }
    }

    if (internalTexture->_cachedWrapV != texture->wrapV) {
      internalTexture->_cachedWrapV = texture->wrapV;
      switch (texture->wrapV) {
        case Texture::WRAP_ADDRESSMODE:
          _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_WRAP_T, GL::REPEAT);
          break;
        case Texture::CLAMP_ADDRESSMODE:
          _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_WRAP_T,
                             GL::CLAMP_TO_EDGE);
          break;
        case Texture::MIRROR_ADDRESSMODE:
          _gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_WRAP_T,
                             GL::MIRRORED_REPEAT);
          break;
        default:
          break;
      }
    }

    _setAnisotropicLevel(GL::TEXTURE_2D, texture);
  }
}

void Engine::setTextureArray(int channel, GL::IGLUniformLocation* uniform,
                             const std::vector<BaseTexture*>& textures)
{
  if (channel < 0) {
    return;
  }

  auto channeli = static_cast<unsigned>(channel);

  if (_textureUnits.empty() || _textureUnits.size() != textures.size()) {
    _textureUnits.clear();
    _textureUnits.resize(textures.size());
  }
  int _i = 0;
  for (unsigned int i = 0; i < textures.size(); ++i, ++_i) {
    _textureUnits[i] = channel + _i;
  }
  _gl->uniform1iv(uniform, _textureUnits);

  unsigned int index = 0;
  for (auto& texture : textures) {
    _setTexture(channeli + index, texture);
    ++index;
  }
}

void Engine::_setAnisotropicLevel(unsigned int key, BaseTexture* texture)
{
  bool anisotropicFilterExtension = _caps.textureAnisotropicFilterExtension;
  unsigned int value              = texture->anisotropicFilteringLevel;

  if (texture->getInternalTexture()->samplingMode
      == Texture::NEAREST_SAMPLINGMODE) {
    value = 1;
  }

  if (anisotropicFilterExtension
      && texture->_cachedAnisotropicFilteringLevel != value) {
    _gl->texParameterf(
      key, AnisotropicFilterExtension::TEXTURE_MAX_ANISOTROPY_EXT,
      static_cast<float>(std::min(value, _caps.maxAnisotropy)));
    texture->_cachedAnisotropicFilteringLevel = value;
  }
}

Uint8Array Engine::readPixels(int x, int y, int width, int height)
{
  Uint8Array data(static_cast<unsigned int>(height * width * 4));
  _gl->readPixels(x, y, width, height, GL::RGBA, GL::UNSIGNED_BYTE, data);
  return data;
}

void Engine::releaseInternalTexture(GL::IGLTexture* texture)
{
  if (!texture) {
    return;
  }

  --texture->references;

  // Final reference ?
  if (texture->references == 0) {
    auto& texturesCache = getLoadedTexturesCache();

    texturesCache.erase(
      std::remove_if(
        texturesCache.begin(), texturesCache.end(),
        [&texture](const std::unique_ptr<GL::IGLTexture>& _texture) {
          return _texture.get() == texture;
        }),
      texturesCache.end());

    _releaseTexture(texture);
  }
}

void Engine::unbindAllAttributes()
{
  for (unsigned int i = 0; i < _vertexAttribArraysEnabled.size(); i++) {
    if (i >= static_cast<unsigned>(_caps.maxVertexAttribs)
        || !_vertexAttribArraysEnabled[i]) {
      continue;
    }
    _gl->disableVertexAttribArray(i);
    _vertexAttribArraysEnabled[i] = false;
  }
}

// Dispose
void Engine::dispose(bool /*doNotRecurse*/)
{
  hideLoadingUI();
  stopRenderLoop();

  // Release scenes
  for (auto& scene : scenes) {
    scene->dispose();
  }
  scenes.clear();

  // Release audio engine
  // Engine::audioEngine->dispose();

  // Release effects
  for (auto& pair : _compiledEffects) {
    _gl->deleteProgram(pair.second->getProgram());
  }

  // Unbind
  unbindAllAttributes();

  _gl = nullptr;

  // WebVR
  // disableVR();
}

// Loading screen
void Engine::displayLoadingUI()
{
  _loadingScreen->displayLoadingUI();
}

void Engine::hideLoadingUI()
{
  _loadingScreen->hideLoadingUI();
}

ILoadingScreen* Engine::loadingScreen()
{
  return _loadingScreen;
}

void Engine::setLoadingScreen(ILoadingScreen* loadingScreen_)
{
  _loadingScreen = loadingScreen_;
}

void Engine::setLoadingUIText(const std::string& text)
{
  _loadingScreen->loadingUIText = text;
}

void Engine::loadingUIBackgroundColor(const std::string& color)
{
  _loadingScreen->loadingUIBackgroundColor = color;
}

std::string Engine::getVertexShaderSource(GL::IGLProgram* program)
{
  auto shaders = _gl->getAttachedShaders(program);
  if (shaders.empty()) {
    BABYLON_LOGF_ERROR("Engine",
                       "Unable to get vertex shader source for program %d",
                       program->value)
  }

  return _gl->getShaderSource(shaders[0]);
}

std::string Engine::getFragmentShaderSource(GL::IGLProgram* program)
{
  auto shaders = _gl->getAttachedShaders(program);
  if (shaders.size() < 2) {
    BABYLON_LOGF_ERROR("Engine",
                       "Unable to get fragment shader source for program %d",
                       program->value)
  }

  return _gl->getShaderSource(shaders[1]);
}

// FPS
float Engine::getFps() const
{
  return fps;
}

microseconds_t Engine::getDeltaTime() const
{
  return deltaTime;
}

void Engine::_measureFps()
{
  previousFramesDuration.emplace_back(Time::highresTimepointNow());
  auto length = previousFramesDuration.size();

  if (length >= 2) {
    deltaTime = std::chrono::duration_cast<microseconds_t>(
      previousFramesDuration[length - 1] - previousFramesDuration[length - 2]);
  }

  if (length >= fpsRange) {
    if (length > fpsRange) {
      // Remove the first element (oldest one)
      previousFramesDuration.erase(previousFramesDuration.begin());
      length -= 1;
    }

    // fractional duration -> no duration_cast needed
    std::chrono::duration<float, std::milli> sumMillis{0.f};
    for (unsigned int id = 0; id < length - 1; ++id) {
      sumMillis += previousFramesDuration[id + 1] - previousFramesDuration[id];
    }

    fps = 1000.f / (sumMillis.count() / static_cast<float>(length - 1));
  }
}

bool Engine::_canRenderToFloatTexture()
{
  return _canRenderToTextureOfType(Engine::TEXTURETYPE_FLOAT,
                                   "OES_texture_float");
}

bool Engine::_canRenderToHalfFloatTexture()
{
  return _canRenderToTextureOfType(Engine::TEXTURETYPE_HALF_FLOAT,
                                   "OES_texture_half_float");
}

// Thank you :
// http://stackoverflow.com/questions/28827511/webgl-ios-render-to-floating-point-texture
bool Engine::_canRenderToTextureOfType(unsigned int /*format*/,
                                       const std::string& /*extension*/)
{
#if 0
  ICanvas* tempcanvas = _renderingCanvas;
  tempcanvas->height  = 16;
  tempcanvas->width   = 16;
  EngineOptions options;
  GL::IGLRenderingContext* gl = tempcanvas->getContext3d(options);

  // extension.
  if (!gl->hasExtension(extension)) {
    return false;
  }

  // setup GLSL program
  const char* vertexCode
    = "attribute vec4 a_position;"
      "void main() {"
      "    gl_Position = a_position;"
      "}";
  const char* fragmentCode
    = "precision mediump float;"
      "uniform vec4 u_color;"
      "uniform sampler2D u_texture;"
      ""
      "void main() {"
      "    gl_FragColor = texture2D(u_texture, vec2(0.5, 0.5)) * u_color;"
      "}";
  auto program = createShaderProgram(vertexCode, fragmentCode, nullptr, gl);
  gl->useProgram(program.get());

  // look up where the vertex data needs to go.
  GL::GLint _positionLocation
    = gl->getAttribLocation(program.get(), "a_position");
  if (_positionLocation < 0) {
    return false;
  }
  GL::GLuint positionLocation = static_cast<GL::GLuint>(_positionLocation);
  GL::IGLUniformLocation* colorLoc
    = gl->getUniformLocation(program.get(), "u_color");

  // provide texture coordinates for the rectangle.
  GL::IGLBuffer* positionBuffer = gl->createBuffer();
  gl->bindBuffer(GL::ARRAY_BUFFER, positionBuffer);
  Float32Array data{-1.f, -1.f, //
                    1.f,  -1.f, //
                    -1.f, 1.f,  //
                    -1.f, 1.f,  //
                    1.f,  -1.f, //
                    1.f,  1.f};
  gl->bufferData(GL::ARRAY_BUFFER, data, GL::STATIC_DRAW);
  gl->enableVertexAttribArray(positionLocation);
  gl->vertexAttribPointer(positionLocation, 2, GL::FLOAT, false, 0, 0);

  GL::IGLTexture* whiteTex = gl->createTexture();
  gl->bindTexture(GL::TEXTURE_2D, whiteTex);
  Uint8Array texImage2DData{255, 255, 255, 255};
  gl->texImage2D(GL::TEXTURE_2D, 0, GL::RGBA, 1, 1, 0, GL::RGBA,
                 GL::UNSIGNED_BYTE, texImage2DData);

  GL::IGLTexture* tex = gl->createTexture();
  gl->bindTexture(GL::TEXTURE_2D, tex);
  gl->texImage2D(GL::TEXTURE_2D, 0, GL::RGBA, 1, 1, 0, GL::RGBA,
                 GetGLTextureType(format), Uint8Array());
  gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MIN_FILTER, GL::NEAREST);
  gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MAG_FILTER, GL::NEAREST);

  GL::IGLFramebuffer* fb = gl->createFramebuffer();
  gl->bindFramebuffer(GL::FRAMEBUFFER, fb);
  gl->framebufferTexture2D(GL::FRAMEBUFFER, GL::COLOR_ATTACHMENT0,
                           GL::TEXTURE_2D, tex, 0);

  auto cleanup = [&]() {
    gl->deleteProgram(program.get());
    gl->disableVertexAttribArray(positionLocation);
    gl->deleteBuffer(positionBuffer);
    gl->deleteFramebuffer(fb);
    gl->deleteTexture(whiteTex);
    gl->deleteTexture(tex);
  };

  GL::GLenum status = gl->checkFramebufferStatus(GL::FRAMEBUFFER);
  if (status != GL::FRAMEBUFFER_COMPLETE) {
    BABYLON_LOGF_ERROR("Engine", "GL Support: can **NOT** render to %d texture",
                       format)
    cleanup();
    return false;
  }

  // Draw the rectangle.
  gl->bindTexture(GL::TEXTURE_2D, whiteTex);
  Float32Array colorData1{0.f, 10.f, 20.f, 1.f};
  gl->uniform4fv(colorLoc, colorData1);
  gl->drawArrays(GL::TRIANGLES, 0, 6);

  gl->bindTexture(GL::TEXTURE_2D, tex);
  gl->bindFramebuffer(GL::FRAMEBUFFER, nullptr);

  gl->clearColor(1, 0, 0, 1);
  gl->clear(GL::COLOR_BUFFER_BIT);

  Float32Array colorData2{0.f, 1.f / 10.f, 1.f / 20.f, 1.f};
  gl->uniform4fv(colorLoc, colorData2);
  gl->drawArrays(GL::TRIANGLES, 0, 6);

  Uint8Array pixel(4);
  gl->readPixels(0, 0, 1, 1, GL::RGBA, GL::UNSIGNED_BYTE, pixel);
  if (pixel[0] != 0 || pixel[1] < 248 || pixel[2] < 248 || pixel[3] < 254) {
    BABYLON_LOGF_ERROR(
      "Engine", "GL Support: Was not able to actually render to %d texture",
      format);
    cleanup();
    return false;
  }

  // Succesfully rendered to "format" texture.
  cleanup();
#endif
  return true;
}

// Statics
bool Engine::isSupported()
{
  return true;
}

std::unique_ptr<GL::IGLShader>
Engine::CompileShader(GL::IGLRenderingContext* gl, const std::string& source,
                      const std::string& type, const std::string& defines)
{
  auto shader = gl->createShader(type == "vertex" ? GL::VERTEX_SHADER :
                                                    GL::FRAGMENT_SHADER);
  gl->shaderSource(shader, ((!defines.empty()) ? defines + "\n" : "") + source);
  gl->compileShader(shader);

  if (!gl->getShaderParameter(shader, GL::COMPILE_STATUS)) {
    BABYLON_LOG_ERROR("Engine", gl->getShaderInfoLog(shader));
    return nullptr;
  }
  return shader;
}

GL::GLenum Engine::GetGLTextureType(GL::GLenum type)
{
  if (type == Engine::TEXTURETYPE_FLOAT) {
    return GL::FLOAT;
  }
  else if (type == Engine::TEXTURETYPE_HALF_FLOAT) {
    // Add Half Float Constant.
    return HALF_FLOAT_OES;
  }

  return GL::UNSIGNED_BYTE;
}

SamplingParameters Engine::GetSamplingParameters(unsigned int samplingMode,
                                                 bool generateMipMaps)
{
  GL::GLenum magFilter = GL::NEAREST;
  GL::GLenum minFilter = GL::NEAREST;
  if (samplingMode == Texture::BILINEAR_SAMPLINGMODE) {
    magFilter = GL::LINEAR;
    if (generateMipMaps) {
      minFilter = GL::LINEAR_MIPMAP_NEAREST;
    }
    else {
      minFilter = GL::LINEAR;
    }
  }
  else if (samplingMode == Texture::TRILINEAR_SAMPLINGMODE) {
    magFilter = GL::LINEAR;
    if (generateMipMaps) {
      minFilter = GL::LINEAR_MIPMAP_LINEAR;
    }
    else {
      minFilter = GL::LINEAR;
    }
  }
  else if (samplingMode == Texture::NEAREST_SAMPLINGMODE) {
    magFilter = GL::NEAREST;
    if (generateMipMaps) {
      minFilter = GL::NEAREST_MIPMAP_LINEAR;
    }
    else {
      minFilter = GL::NEAREST;
    }
  }

  return {static_cast<int>(minFilter), static_cast<int>(magFilter)};
}

void Engine::PrepareGLTexture(
  GL::IGLTexture* texture, GL::IGLRenderingContext* gl, Scene* scene, int width,
  int height, bool noMipmap, bool isCompressed,
  const std::function<void(int width, int height)>& processFunction,
  bool invertY, unsigned int samplingMode)
{
  auto engine = scene->getEngine();
  auto potWidth
    = Tools::GetExponentOfTwo(width, engine->getCaps().maxTextureSize);
  auto potHeight
    = Tools::GetExponentOfTwo(height, engine->getCaps().maxTextureSize);

  engine->_bindTextureDirectly(GL::TEXTURE_2D, texture);
  gl->pixelStorei(GL::UNPACK_FLIP_Y_WEBGL, invertY ? 1 : 0);

  texture->_baseWidth  = width;
  texture->_baseHeight = height;
  texture->_width      = potWidth;
  texture->_height     = potHeight;
  texture->isReady     = true;

  processFunction(potWidth, potHeight);

  auto filters = GetSamplingParameters(samplingMode, !noMipmap);

  gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MAG_FILTER, filters.mag);
  gl->texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MIN_FILTER, filters.min);

  if (!noMipmap && !isCompressed) {
    gl->generateMipmap(GL::TEXTURE_2D);
  }

  engine->_bindTextureDirectly(GL::TEXTURE_2D, nullptr);

  engine->resetTextureCache();
  scene->_removePendingData(texture);

  for (auto& callback : texture->onLoadedCallbacks) {
    callback();
  }
  texture->onLoadedCallbacks.clear();
}

} // end of namespace BABYLON
