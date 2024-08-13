#include "raygui.h"
#include "raylib.h"
#include "raymath.h"

// [HACK] IXWebsocket includes some of Windows' headers, this manua define
// circumvents a link conflict between Raylib's functions and Windows'.
#if defined(_WIN32) || defined(_WIN64)
#define _WINUSER_
#define _IMM_
#define _APISETCONSOLEL3_
#define _WINGDI_
#endif
#include "ixwebsocket/IXWebSocketServer.h"
#if defined(_WIN32) || defined(_WIN64)
#undef _WINUSER_
#undef _IMM_
#undef _APISETCONSOLEL3_
#undef _WINGDI_
#endif

#include "AsepriteConnection.h"

#include "platformSetup.h"

#include <cmath>
#include <cstdint>

enum class UiState
{
    Nothing,
    Main,
    Help,
};

static void DrawTextBorder(const char *text,
                           float x,
                           float y,
                           int size,
                           Color textColor,
                           Color outlineColor)
{
    for (int xb = -1; xb <= 1; ++xb)
    {
        for (int yb = -1; yb <= 1; ++yb)
        {
            if (xb == 0 && yb == 0)
            {
                continue;
            }

            DrawText(text, x + xb, y + yb, size, outlineColor);
        }
    }
    DrawText(text, x, y, size, textColor);
}


int start()
{
    // Initialization
    //--------------------------------------------------------------------------------------
    
    bool useFullScreen = false;

    UiState uiState = UiState::Nothing;

    AsepriteConnection imageServer;

    // Prepare the WebSocket server.
    ix::initNetSystem();
    ix::WebSocketServer serv(34613);
    serv.disablePerMessageDeflate();
    serv.setOnClientMessageCallback(
        [&imageServer](std::shared_ptr<ix::ConnectionState> connectionState,
                       ix::WebSocket &webSocket,
                       const ix::WebSocketMessagePtr &msg) {
            imageServer.onMessage(connectionState, webSocket, msg);
        });
    serv.listenAndStart();

    // Prepare Raylib, the window, the graphics settings...
    const Vector2 defaultWindowSize = Vector2{ 320, 200 };
    InitWindow(defaultWindowSize.x, defaultWindowSize.y, "Squint micro viewer");
    SetExitKey(KEY_NULL);
    SetWindowState(FLAG_VSYNC_HINT);

    Vector2 fullscreenWindowSize;
    Vector2 lastImageSize{ 0, 0 };
    bool imageSizeUpdated = false;

    SetTargetFPS(60);

    Texture2D currentTexture{};

    //--------------------------------------------------------------------------------------

    int renderScale = 1;

    bool previouslyConnected = false;

    while (!WindowShouldClose()) // Detect window close button or ESC key
    {
        if (IsKeyPressed(KEY_F))
        {
            unsigned int currentMonitor = GetCurrentMonitor();
            Vector2 currentMonitorPosition = GetMonitorPosition(currentMonitor);
            Vector2 currentMonitorSize =
                Vector2{float(GetMonitorWidth(currentMonitor)), float(GetMonitorHeight(currentMonitor))};

            useFullScreen = !useFullScreen;
            if (useFullScreen)
            {

                fullscreenWindowSize = currentMonitorSize;
                SetWindowSize(currentMonitorSize.x, currentMonitorSize.y);
                SetWindowState(FLAG_VSYNC_HINT | FLAG_WINDOW_TOPMOST);
                SetWindowPosition(currentMonitorPosition.x, currentMonitorPosition.y);
            }
            else
            {
                Vector2 windowSize = (lastImageSize.x == 0 && lastImageSize.y == 0) ? defaultWindowSize : lastImageSize;

                unsigned int currentMonitor = GetCurrentMonitor();
                Vector2 centeredLocalPosition =
                    Vector2{(currentMonitorSize.x - 160.f) / 2.f,
                            (currentMonitorSize.y - 120.f) / 2.f};

                Vector2 finalPosition =
                    Vector2Add(currentMonitorPosition, centeredLocalPosition);
                SetWindowSize(windowSize.x, windowSize.y);
                SetWindowState(FLAG_VSYNC_HINT);
                ClearWindowState(FLAG_WINDOW_TOPMOST);
                SetWindowPosition(finalPosition.x, finalPosition.y);
            }
        }

        if (imageSizeUpdated && !useFullScreen) {
            Vector2 windowSize = (lastImageSize.x == 0 && lastImageSize.y == 0) ? defaultWindowSize : lastImageSize;
            SetWindowSize(windowSize.x, windowSize.y);
            imageSizeUpdated = false;
        }

        // Draw
        //----------------------------------------------------------------------------------
        {
            BeginDrawing();

            if (!imageServer.connected)
            {
                ClearBackground(GRAY);
                Vector2 size =
                    MeasureTextEx(GetFontDefault(), "Waiting for connection.", 10, 0);
                Vector2 pos;
                pos.x = (defaultWindowSize.x - size.x) / 2.f;
                pos.y = (defaultWindowSize.y - size.y) / 2.f;
                DrawText("Waiting for connection.", int(pos.x), int(pos.y), 10, LIGHTGRAY);

                if (lastImageSize.x != 0 || lastImageSize.y != 0) {
                    imageSizeUpdated = true;
                    lastImageSize = Vector2{ 0, 0 };
                }
            }
            else
            {
                ClearBackground(WHITE);
                AsepriteImage lastImage; 
                if (imageServer.lastReadyImageMutex.try_lock())
                {
                    lastImage = std::move(imageServer.lastReadyImage);
                    imageServer.lastReadyImageMutex.unlock();
                }
                Image currentImage;
                currentImage.width = lastImage.width;
                currentImage.height = lastImage.height;
                currentImage.data = lastImage.pixels.data();
                currentImage.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
                currentImage.mipmaps = 1;

                if ((lastImage.width != 0 && lastImage.height != 0))
                {
                    bool sizeMismatch = lastImage.width != currentTexture.width ||
                                        lastImage.height != currentTexture.height;
                    // Regenerate the base texture
                    if (sizeMismatch)
                    {
                        UnloadTexture(currentTexture);
                        currentTexture = LoadTextureFromImage(currentImage);
                    }
                    else
                    {
                        UpdateTexture(currentTexture, currentImage.data);
                    }
                }
                else if (imageServer.connected && !previouslyConnected)
                {
                    // Clear the texture in case of reconnection.
                    UnloadTexture(currentTexture);
                    Image blankPixel;
                    blankPixel.width = 1;
                    blankPixel.height = 1;
                    const char singlePixel[4] = {
                        0x0,
                        0x0,
                        0x0,
                        0x0,
                    };
                    blankPixel.data = (void *)singlePixel;
                    blankPixel.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
                    blankPixel.mipmaps = 1;
                    currentTexture = LoadTextureFromImage(blankPixel);
                }

                if (currentTexture.width != lastImageSize.x || currentTexture.height != lastImageSize.y) {
                    imageSizeUpdated = true;
                    lastImageSize = Vector2{ float(currentTexture.width), float(currentTexture.height) };
                }

                Vector2 texturePosition{ useFullScreen ? (fullscreenWindowSize.x - currentTexture.width)  / 2.f : 0.f,
                                         useFullScreen ? (fullscreenWindowSize.y - currentTexture.height) / 2.f : 0.f};

                DrawTextureEx(currentTexture, texturePosition, 0, 1, WHITE);
            }

            previouslyConnected = imageServer.connected;
            EndDrawing();
        }
        //----------------------------------------------------------------------------------
    }

    serv.stop();
    ix::uninitNetSystem();

    // De-Initialization
    //--------------------------------------------------------------------------------------
    // Manual shader unload to avoid crashes due to unload order.
    UnloadTexture(currentTexture);

    CloseWindow(); // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}

int main(void)
{
    setupLoggingOutput();
    int result = start();
    unsetupLoggingOutput();
}
