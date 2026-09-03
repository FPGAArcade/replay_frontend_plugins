// Replay Graphics Manager for ScummVM - Implementation

#include "replay_graphics.h"
#include <cstdlib>
#include <cstring>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Static graphics mode table

static const OSystem::GraphicsMode s_graphicsModes[] = { { "default", "Default", 1 }, { nullptr, nullptr, 0 } };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: Pack RGB components into RGBA32 format
// RGBA32 format: R | (G << 8) | (B << 16) | (A << 24)

static inline uint32_t pack_rgba32(byte r, byte g, byte b) {
    return r | (g << 8) | (b << 16) | (0xFFu << 24);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor

ReplayGraphicsManager::ReplayGraphicsManager()
    : _screenWidth(320), _screenHeight(200), _frameBuffer(nullptr), _hasNewFrame(false), _overlayVisible(false),
      _cursorVisible(false), _cursorX(0), _cursorY(0), _cursorHotspotX(0), _cursorHotspotY(0), _cursorKeycolor(0),
      _inTransaction(false), _screenChangeID(0), _shakeXOffset(0), _shakeYOffset(0) {

    memset(_palette, 0, sizeof(_palette));
    memset(_cursorPalette, 0, sizeof(_cursorPalette));

    // Initialize pre-computed RGBA32 palette (all black initially)
    for (int i = 0; i < 256; i++) {
        _paletteXRGB[i] = pack_rgba32(0, 0, 0);
    }

    // Default to 8-bit paletted format (ScummVM's native mode)
    _screenFormat = Graphics::PixelFormat::createFormatCLUT8();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor

ReplayGraphicsManager::~ReplayGraphicsManager() {
    _screenSurface.free();
    _overlay.free();
    _cursor.free();
    free(_frameBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Feature support

bool ReplayGraphicsManager::hasFeature(OSystem::Feature f) const {
    switch (f) {
        case OSystem::kFeatureCursorPalette:
            return true;
        default:
            return false;
    }
}

void ReplayGraphicsManager::setFeatureState(OSystem::Feature f, bool enable) {
    // Most features are no-ops for our simple implementation
    (void)f;
    (void)enable;
}

bool ReplayGraphicsManager::getFeatureState(OSystem::Feature f) const {
    (void)f;
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Graphics modes

const OSystem::GraphicsMode* ReplayGraphicsManager::getSupportedGraphicsModes() const {
    return s_graphicsModes;
}

int ReplayGraphicsManager::getDefaultGraphicsMode() const {
    return 1;
}

bool ReplayGraphicsManager::setGraphicsMode(int mode, uint flags) {
    (void)mode;
    (void)flags;
    return true;
}

int ReplayGraphicsManager::getGraphicsMode() const {
    return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Screen initialization

void ReplayGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat* format) {
    if (width == 0 || height == 0) {
        return;
    }

    _screenWidth = width;
    _screenHeight = height;

    if (format) {
        _screenFormat = *format;
    } else {
        _screenFormat = Graphics::PixelFormat::createFormatCLUT8();
    }

    // Allocate screen surface
    _screenSurface.free();
    _screenSurface.create(width, height, _screenFormat);

    // Allocate frame buffer (always XRGB8888 for frontend)
    free(_frameBuffer);
    _frameBuffer = (uint32_t*)calloc(width * height, sizeof(uint32_t));
    if (!_frameBuffer) {
        // Allocation failed - convertToXRGB8888 will handle nullptr gracefully
        return;
    }

    // Also update overlay to match screen size
    _overlay.free();
    _overlay.create(width, height, Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0));

    _screenChangeID++;
}

int ReplayGraphicsManager::getScreenChangeID() const {
    return _screenChangeID;
}

Graphics::PixelFormat ReplayGraphicsManager::getScreenFormat() const {
    return _screenFormat;
}

Common::List<Graphics::PixelFormat> ReplayGraphicsManager::getSupportedFormats() const {
    Common::List<Graphics::PixelFormat> formats;
    // 32-bit ARGB8888
    formats.push_back(Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0));
    // 16-bit RGB565
    formats.push_back(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
    // 8-bit paletted
    formats.push_back(Graphics::PixelFormat::createFormatCLUT8());
    return formats;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Transaction handling

void ReplayGraphicsManager::beginGFXTransaction() {
    _inTransaction = true;
}

OSystem::TransactionError ReplayGraphicsManager::endGFXTransaction() {
    _inTransaction = false;
    return OSystem::kTransactionSuccess;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Screen dimensions

int16 ReplayGraphicsManager::getWidth() const {
    return _screenWidth;
}

int16 ReplayGraphicsManager::getHeight() const {
    return _screenHeight;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Palette

void ReplayGraphicsManager::setPalette(const byte* colors, uint start, uint num) {
    if (!colors || start >= 256 || start + num > 256) {
        return;
    }

    memcpy(_palette + start * 3, colors, num * 3);

    // Update pre-computed RGBA32 palette for fast conversion
    // Read from _palette using same indexing as original convertToXRGB8888
    for (uint i = 0; i < num; i++) {
        uint idx = start + i;
        byte r = _palette[idx * 3 + 0];
        byte g = _palette[idx * 3 + 1];
        byte b = _palette[idx * 3 + 2];
        _paletteXRGB[idx] = pack_rgba32(r, g, b);
    }
}

void ReplayGraphicsManager::grabPalette(byte* colors, uint start, uint num) const {
    if (!colors || start >= 256 || start + num > 256) {
        return;
    }
    memcpy(colors, _palette + start * 3, num * 3);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Screen drawing

void ReplayGraphicsManager::copyRectToScreen(const void* buf, int pitch, int x, int y, int w, int h) {
    if (!buf || w <= 0 || h <= 0) {
        return;
    }

    const byte* src = (const byte*)buf;
    byte* dst = (byte*)_screenSurface.getBasePtr(x, y);
    int dstPitch = _screenSurface.pitch;

    for (int i = 0; i < h; i++) {
        memcpy(dst, src, w * _screenFormat.bytesPerPixel);
        src += pitch;
        dst += dstPitch;
    }
}

Graphics::Surface* ReplayGraphicsManager::lockScreen() {
    return &_screenSurface;
}

void ReplayGraphicsManager::unlockScreen() {
    // No-op - frame will be converted on updateScreen()
}

void ReplayGraphicsManager::fillScreen(uint32 col) {
    byte* dst = (byte*)_screenSurface.getPixels();
    if (_screenFormat.bytesPerPixel == 1) {
        memset(dst, col, _screenWidth * _screenHeight);
    } else {
        // For other formats, fill each pixel
        for (uint y = 0; y < _screenHeight; y++) {
            for (uint x = 0; x < _screenWidth; x++) {
                if (_screenFormat.bytesPerPixel == 2) {
                    ((uint16*)dst)[y * _screenWidth + x] = (uint16)col;
                } else if (_screenFormat.bytesPerPixel == 4) {
                    ((uint32*)dst)[y * _screenWidth + x] = col;
                }
            }
        }
    }
}

void ReplayGraphicsManager::fillScreen(const Common::Rect& r, uint32 col) {
    for (int y = r.top; y < r.bottom; y++) {
        for (int x = r.left; x < r.right; x++) {
            if (_screenFormat.bytesPerPixel == 1) {
                *((byte*)_screenSurface.getBasePtr(x, y)) = (byte)col;
            } else if (_screenFormat.bytesPerPixel == 2) {
                *((uint16*)_screenSurface.getBasePtr(x, y)) = (uint16)col;
            } else if (_screenFormat.bytesPerPixel == 4) {
                *((uint32*)_screenSurface.getBasePtr(x, y)) = col;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert screen to XRGB8888 frame buffer

void ReplayGraphicsManager::convertToXRGB8888() {
    if (!_frameBuffer)
        return;

    const byte* src = (const byte*)_screenSurface.getPixels();
    uint32_t* dst = _frameBuffer;

    if (_screenFormat.bytesPerPixel == 1) {
        // 8-bit paletted mode - use pre-computed RGBA32 palette for fast conversion
        for (uint y = 0; y < _screenHeight; y++) {
            const byte* src_row = src + y * _screenSurface.pitch;
            uint32_t* dst_row = dst + y * _screenWidth;
            for (uint x = 0; x < _screenWidth; x++) {
                dst_row[x] = _paletteXRGB[src_row[x]];
            }
        }
    } else if (_screenFormat.bytesPerPixel == 2) {
        // 16-bit mode - convert using format to RGBA32
        const uint16* src16 = (const uint16*)src;
        for (uint y = 0; y < _screenHeight; y++) {
            const uint16* src_row = src16 + y * (_screenSurface.pitch / 2);
            uint32_t* dst_row = dst + y * _screenWidth;
            for (uint x = 0; x < _screenWidth; x++) {
                byte r, g, b;
                _screenFormat.colorToRGB(src_row[x], r, g, b);
                dst_row[x] = r | (g << 8) | (b << 16) | (0xFF << 24);
            }
        }
    } else if (_screenFormat.bytesPerPixel == 4) {
        // 32-bit mode - convert to RGBA32
        const uint32* src32 = (const uint32*)src;
        for (uint y = 0; y < _screenHeight; y++) {
            const uint32* src_row = src32 + y * (_screenSurface.pitch / 4);
            uint32_t* dst_row = dst + y * _screenWidth;
            for (uint x = 0; x < _screenWidth; x++) {
                byte r, g, b;
                _screenFormat.colorToRGB(src_row[x], r, g, b);
                dst_row[x] = r | (g << 8) | (b << 16) | (0xFF << 24);
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Update screen - this is where we convert and mark frame ready

void ReplayGraphicsManager::updateScreen() {
    convertToXRGB8888();
    _hasNewFrame = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shake effect

void ReplayGraphicsManager::setShakePos(int shakeXOffset, int shakeYOffset) {
    _shakeXOffset = shakeXOffset;
    _shakeYOffset = shakeYOffset;
}

void ReplayGraphicsManager::setFocusRectangle(const Common::Rect& rect) {
    (void)rect;
}

void ReplayGraphicsManager::clearFocusRectangle() {}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Overlay

void ReplayGraphicsManager::showOverlay(bool inGUI) {
    (void)inGUI;
    _overlayVisible = true;
}

void ReplayGraphicsManager::hideOverlay() {
    _overlayVisible = false;
}

bool ReplayGraphicsManager::isOverlayVisible() const {
    return _overlayVisible;
}

Graphics::PixelFormat ReplayGraphicsManager::getOverlayFormat() const {
    return Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0); // ARGB8888
}

void ReplayGraphicsManager::clearOverlay() {
    _overlay.fillRect(Common::Rect(_overlay.w, _overlay.h), 0);
}

void ReplayGraphicsManager::grabOverlay(Graphics::Surface& surface) const {
    surface.copyFrom(_overlay);
}

void ReplayGraphicsManager::copyRectToOverlay(const void* buf, int pitch, int x, int y, int w, int h) {
    if (!buf || w <= 0 || h <= 0) {
        return;
    }

    const byte* src = (const byte*)buf;
    byte* dst = (byte*)_overlay.getBasePtr(x, y);

    for (int i = 0; i < h; i++) {
        memcpy(dst, src, w * _overlay.format.bytesPerPixel);
        src += pitch;
        dst += _overlay.pitch;
    }
}

int16 ReplayGraphicsManager::getOverlayHeight() const {
    return _overlay.h;
}

int16 ReplayGraphicsManager::getOverlayWidth() const {
    return _overlay.w;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cursor

bool ReplayGraphicsManager::showMouse(bool visible) {
    bool old = _cursorVisible;
    _cursorVisible = visible;
    return old;
}

void ReplayGraphicsManager::warpMouse(int x, int y) {
    _cursorX = x;
    _cursorY = y;
}

void ReplayGraphicsManager::setMouseCursor(const void* buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
                                           bool dontScale, const Graphics::PixelFormat* format, const byte* mask) {
    (void)dontScale;
    (void)mask;

    if (!buf || w == 0 || h == 0) {
        return;
    }

    _cursorHotspotX = hotspotX;
    _cursorHotspotY = hotspotY;
    _cursorKeycolor = keycolor;

    Graphics::PixelFormat cursorFormat;
    if (format) {
        cursorFormat = *format;
    } else {
        cursorFormat = Graphics::PixelFormat::createFormatCLUT8();
    }

    _cursor.free();
    _cursor.create(w, h, cursorFormat);
    memcpy(_cursor.getPixels(), buf, w * h * cursorFormat.bytesPerPixel);
}

void ReplayGraphicsManager::setCursorPalette(const byte* colors, uint start, uint num) {
    if (!colors || start >= 256 || start + num > 256) {
        return;
    }
    memcpy(_cursorPalette + start * 3, colors, num * 3);
}
