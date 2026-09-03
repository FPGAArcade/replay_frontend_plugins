// Replay Graphics Manager for ScummVM
//
// Buffers rendered frames for the frontend to access via run_frame()

#ifndef REPLAY_GRAPHICS_H
#define REPLAY_GRAPHICS_H

#include "backends/graphics/graphics.h"
#include "common/list.h"
#include "common/mutex.h"
#include "common/rect.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

class ReplayGraphicsManager : public GraphicsManager {
  public:
    ReplayGraphicsManager();
    virtual ~ReplayGraphicsManager();

    // GraphicsManager interface
    virtual bool hasFeature(OSystem::Feature f) const override;
    virtual void setFeatureState(OSystem::Feature f, bool enable) override;
    virtual bool getFeatureState(OSystem::Feature f) const override;

    virtual const OSystem::GraphicsMode* getSupportedGraphicsModes() const override;
    virtual int getDefaultGraphicsMode() const override;
    virtual bool setGraphicsMode(int mode, uint flags = OSystem::kGfxModeNoFlags) override;
    virtual int getGraphicsMode() const override;

    virtual void initSize(uint width, uint height, const Graphics::PixelFormat* format) override;
    virtual int getScreenChangeID() const override;
    virtual Graphics::PixelFormat getScreenFormat() const override;
    virtual Common::List<Graphics::PixelFormat> getSupportedFormats() const override;

    virtual void beginGFXTransaction() override;
    virtual OSystem::TransactionError endGFXTransaction() override;

    virtual int16 getWidth() const override;
    virtual int16 getHeight() const override;

    virtual void setPalette(const byte* colors, uint start, uint num) override;
    virtual void grabPalette(byte* colors, uint start, uint num) const override;

    virtual void copyRectToScreen(const void* buf, int pitch, int x, int y, int w, int h) override;
    virtual Graphics::Surface* lockScreen() override;
    virtual void unlockScreen() override;
    virtual void fillScreen(uint32 col) override;
    virtual void fillScreen(const Common::Rect& r, uint32 col) override;
    virtual void updateScreen() override;

    virtual void setShakePos(int shakeXOffset, int shakeYOffset) override;
    virtual void setFocusRectangle(const Common::Rect& rect) override;
    virtual void clearFocusRectangle() override;

    // Overlay
    virtual void showOverlay(bool inGUI) override;
    virtual void hideOverlay() override;
    virtual bool isOverlayVisible() const override;
    virtual Graphics::PixelFormat getOverlayFormat() const override;
    virtual void clearOverlay() override;
    virtual void grabOverlay(Graphics::Surface& surface) const override;
    virtual void copyRectToOverlay(const void* buf, int pitch, int x, int y, int w, int h) override;
    virtual int16 getOverlayHeight() const override;
    virtual int16 getOverlayWidth() const override;

    // Cursor
    virtual bool showMouse(bool visible) override;
    virtual void warpMouse(int x, int y) override;
    virtual void setMouseCursor(const void* buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
                                bool dontScale, const Graphics::PixelFormat* format, const byte* mask) override;
    virtual void setCursorPalette(const byte* colors, uint start, uint num) override;

    // Access to frame buffer (for frontend)
    const uint32_t* getFrameBuffer() const {
        return _frameBuffer;
    }
    uint32_t getFrameWidth() const {
        return _screenWidth;
    }
    uint32_t getFrameHeight() const {
        return _screenHeight;
    }
    uint32_t getFramePitch() const {
        return _screenWidth * 4;
    }
    bool hasNewFrame() const {
        return _hasNewFrame;
    }
    void clearNewFrameFlag() {
        _hasNewFrame = false;
    }

  private:
    void convertToXRGB8888();

    uint32_t _screenWidth;
    uint32_t _screenHeight;
    Graphics::PixelFormat _screenFormat;
    Graphics::Surface _screenSurface;

    // Frame buffer in XRGB8888 format for frontend
    uint32_t* _frameBuffer;
    bool _hasNewFrame;

    // Palette (for 8-bit mode)
    byte _palette[256 * 3];
    uint32_t _paletteXRGB[256]; // Pre-computed XRGB8888 for fast conversion

    // Overlay
    Graphics::Surface _overlay;
    bool _overlayVisible;

    // Cursor
    bool _cursorVisible;
    int _cursorX, _cursorY;
    int _cursorHotspotX, _cursorHotspotY;
    uint32_t _cursorKeycolor;
    Graphics::Surface _cursor;
    byte _cursorPalette[256 * 3];

    // Transaction
    bool _inTransaction;
    int _screenChangeID;

    // Shake
    int _shakeXOffset;
    int _shakeYOffset;
};

#endif // REPLAY_GRAPHICS_H
