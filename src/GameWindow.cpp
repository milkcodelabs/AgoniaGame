#include "GameWindow.h"
#include <SDL_image.h>
#include <iostream>
#include <algorithm>
#include <string>


// --- SCROLL STATE ---
static float rulesScrollY = 0.0f;
static float rulesMaxScroll = 0.0f;

// --- ANDROID ASSET PATHING MACRO ---
#ifdef __ANDROID__
    const std::string ASSET_PATH = "";
#else
    const std::string ASSET_PATH = "assets/";
#endif

void GameWindow::playSound(Sfx sound) {
    if (isMuted) return; // THE FIX: Respect the mute button!
    
    if (sounds.count(sound) && sounds[sound]) {
        Mix_PlayChannel(-1, sounds[sound], 0);
    }
}

GameWindow::GameWindow(const std::string& title, int width, int height) {
    window = nullptr;
    renderer = nullptr;
    font = nullptr;
    cardSpriteSheet = nullptr;
    tableBackground = nullptr;
    running = false;
    needsSuitSelection = false;
    currentTextInput = "";
    selectedCardIndex = -1;
    isLoadingNextRound = false; 
    mouseX = 0;
    mouseY = 0;
}

GameWindow::~GameWindow() {
    if (tableBackground) SDL_DestroyTexture(tableBackground);
    if (cardSpriteSheet) SDL_DestroyTexture(cardSpriteSheet);
    if (font) TTF_CloseFont(font);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    for (auto& pair : sounds) {
        if (pair.second) Mix_FreeChunk(pair.second);
    }
    sounds.clear();
    Mix_CloseAudio();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

bool GameWindow::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) < 0) return false;

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_IDLE_TIMER_DISABLED, "1");
    
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        std::cerr << "Failed to initialize SDL_image: " << IMG_GetError() << "\n";
        return false;
    }

    if (TTF_Init() == -1) return false;

    // --- PRIORITY TASK 4: DYNAMIC MOBILE RESOLUTION ---
    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);
    
#ifdef __ANDROID__
    // Mobile: Force Fullscreen Native Resolution
    window = SDL_CreateWindow("Agonia", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                              displayMode.w, displayMode.h, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
#else
    // Desktop: Standard Resizable Window
    window = SDL_CreateWindow("Agonia", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                              LOGICAL_WIDTH, LOGICAL_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
#endif

    if (!window) return false;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) return false;

    // WE NO LONGER USE SDL_RenderSetLogicalSize. We handle Viewports dynamically in render()
    // --- SET WINDOW/TASKBAR ICON ---
    
    std::string iconPath = ASSET_PATH + "agonia_icon.png";
    SDL_Surface* iconSurface = IMG_Load(iconPath.c_str());
    if (iconSurface) {
        SDL_SetWindowIcon(window, iconSurface);
        SDL_FreeSurface(iconSurface);
    }

    // --- ASSET PATHING FIX ---
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    std::string sheetPath = ASSET_PATH + "playingCards.png";
    SDL_Surface* sheetSurface = IMG_Load(sheetPath.c_str());
    if (sheetSurface) {
        cardSpriteSheet = SDL_CreateTextureFromSurface(renderer, sheetSurface);
        SDL_FreeSurface(sheetSurface);
    }

    std::string bgPath = ASSET_PATH + "felt.png";
    SDL_Surface* backgroundSurface = IMG_Load(bgPath.c_str());
    if (backgroundSurface) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); 
        tableBackground = SDL_CreateTextureFromSurface(renderer, backgroundSurface);
        SDL_FreeSurface(backgroundSurface);
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    std::string fontPath = ASSET_PATH + "Kenney Pixel.ttf";
    font = TTF_OpenFont(fontPath.c_str(), 56); 

    // --- AUDIO INITIALIZATION ---
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "Warning: SDL_mixer could not initialize!\n";
    }

    sounds[Sfx::CLICK] = Mix_LoadWAV((ASSET_PATH + "click.ogg").c_str());
    sounds[Sfx::START] = Mix_LoadWAV((ASSET_PATH + "start.ogg").c_str());
    sounds[Sfx::ROUND_END] = Mix_LoadWAV((ASSET_PATH + "round_end.ogg").c_str());
    sounds[Sfx::TOURNAMENT_END] = Mix_LoadWAV((ASSET_PATH + "tournament_end.ogg").c_str());
    sounds[Sfx::SELECT] = Mix_LoadWAV((ASSET_PATH + "select.ogg").c_str());
    sounds[Sfx::PLAY_CARD] = Mix_LoadWAV((ASSET_PATH + "play_card.ogg").c_str());
    sounds[Sfx::SHUFFLE] = Mix_LoadWAV((ASSET_PATH + "shuffle.ogg").c_str());
    sounds[Sfx::DRAW] = Mix_LoadWAV((ASSET_PATH + "draw.ogg").c_str());
    sounds[Sfx::YOUR_TURN] = Mix_LoadWAV((ASSET_PATH + "your_turn.ogg").c_str());
    sounds[Sfx::PASS] = Mix_LoadWAV((ASSET_PATH + "pass.ogg").c_str());
    sounds[Sfx::FAIL] = Mix_LoadWAV((ASSET_PATH + "fail.ogg").c_str());

    running = true;
    return true;
}

void GameWindow::processInput(AppState& state, Match* match, int myIndex) {

    if (state != AppState::NAME_INPUT && state != AppState::JOIN_INPUT) {
        if (isTextBoxFocused) {
            isTextBoxFocused = false;
            SDL_StopTextInput();
        }
    }
    
    SDL_Event event;

    // --- TRUE PHYSICAL SAFE ZONE MATH ---
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    float scaleX = (float)w / LOGICAL_WIDTH;
    float scaleY = (float)h / LOGICAL_HEIGHT;
    float scale = (std::min)(scaleX, scaleY); 
    
    int viewW = (int)(LOGICAL_WIDTH * scale);
    int viewH = (int)(LOGICAL_HEIGHT * scale);
    int viewX = (w - viewW) / 2;
    int viewY = (h - viewH) / 2;

    int kbOffsetY = 0;
#ifdef __ANDROID__
    if ((state == AppState::NAME_INPUT || state == AppState::JOIN_INPUT) && isTextBoxFocused) {
        kbOffsetY = (int)(250 * scale); 
    }
#endif

    int rawMouseX, rawMouseY;
    SDL_GetMouseState(&rawMouseX, &rawMouseY);
    mouseX = (int)((rawMouseX - viewX) / scale);
    mouseY = (int)((rawMouseY - viewY + kbOffsetY) / scale);

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) running = false;
        
        // --- MOBILE LIFECYCLE EVENTS ---
        if (event.type == SDL_APP_WILLENTERBACKGROUND) {
            Mix_Pause(-1);     
            Mix_PauseMusic();  
        } else if (event.type == SDL_APP_DIDENTERFOREGROUND) {
            Mix_Resume(-1);
            Mix_ResumeMusic();
        }
        // --- TEXT INPUT HANDLING ---
        else if (event.type == SDL_TEXTINPUT) {
            if (isTextBoxFocused) {
                if (state == AppState::NAME_INPUT && currentTextInput.length() < 15) {
                    currentTextInput += event.text.text;
                } 
                else if (state == AppState::JOIN_INPUT && currentTextInput.length() < 4) {
                    std::string inputStr = event.text.text;
                    for (char& c : inputStr) c = toupper(c); 
                    currentTextInput += inputStr;
                }
            }
        }
        else if (event.type == SDL_KEYDOWN) {
            if (isTextBoxFocused) {
                if (event.key.keysym.sym == SDLK_BACKSPACE && currentTextInput.length() > 0) {
                    currentTextInput.pop_back();
                } 
                else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
                    if (state == AppState::NAME_INPUT && currentTextInput.length() > 0 && onNameEntered) {
                        isTextBoxFocused = false;
                        SDL_StopTextInput();
                        onNameEntered(currentTextInput);
                        currentTextInput = "";
                    } 
                    else if (state == AppState::JOIN_INPUT && currentTextInput.length() == 4 && onJoinCodeEntered) {
                        isTextBoxFocused = false;
                        SDL_StopTextInput();
                        onJoinCodeEntered(currentTextInput);
                    }
                }
            }
            if (event.key.keysym.sym == SDLK_AC_BACK || event.key.keysym.sym == SDLK_ESCAPE) {
                if (state == AppState::MAIN_MENU) {
                    running = false; // Exit the app if on the home screen
                } else {
                    // Act as a universal "Back / Exit" button
                    if (onMenuOptionSelected) onMenuOptionSelected(-1);
                    if (state == AppState::RULES) rulesScrollY = 0.0f; // Reset scroll
                    needsSuitSelection = false; // Close Ace menu if open
                }
            }
        }
        // --- SCROLLING LOGIC ---
        else if (event.type == SDL_MOUSEWHEEL) {
            if (state == AppState::RULES) {
                rulesScrollY += event.wheel.y * 50.0f; // Scroll speed
                if (rulesScrollY > 0.0f) rulesScrollY = 0.0f;
                if (rulesScrollY < -rulesMaxScroll) rulesScrollY = -rulesMaxScroll;
            }
        }
        else if (event.type == SDL_FINGERMOTION) {
            if (state == AppState::RULES) {
                // event.tfinger.dy is a normalized percentage, multiply by height for physical pixels
                rulesScrollY += (event.tfinger.dy * h) * 1.5f; 
                if (rulesScrollY > 0.0f) rulesScrollY = 0.0f;
                if (rulesScrollY < -rulesMaxScroll) rulesScrollY = -rulesMaxScroll;
            }
        }

        // --- TOUCH / MOUSE HANDLING ---
        static Uint32 lastClickTime = 0; 
        bool isClick = false;
        int inputX = 0, inputY = 0;

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            if (event.button.which != SDL_TOUCH_MOUSEID) {
                if (SDL_GetTicks() - lastClickTime > 250) {
                    isClick = true; 
                    inputX = (int)((event.button.x - viewX) / scale);
                    inputY = (int)((event.button.y - viewY + kbOffsetY) / scale);
                    lastClickTime = SDL_GetTicks();
                }
            }
        } else if (event.type == SDL_FINGERDOWN) {
            if (SDL_GetTicks() - lastClickTime > 250) {
                int rawX = (int)(event.tfinger.x * w);
                int rawY = (int)(event.tfinger.y * h);
                
                if (rawX >= viewX && rawX <= viewX + viewW && rawY >= viewY && rawY <= viewY + viewH) {
                    isClick = true; 
                    inputX = (int)((rawX - viewX) / scale); 
                    inputY = (int)((rawY - viewY + kbOffsetY) / scale);
                    lastClickTime = SDL_GetTicks();
                }
            }
        }

        if (isClick) {
            SDL_Point clickPoint = { inputX, inputY };
            bool actionHandled = false;

            float ui = 1.0f;
#ifdef __ANDROID__
            ui = 1.6f; 
#endif

            // --- KEYBOARD TRIGGERING (Hitboxes) ---
            if (state == AppState::NAME_INPUT) {
                int panelH = (int)(450 * ui);
                int menuPanelY = LOGICAL_HEIGHT / 2 - panelH / 2;
                int bw = (int)(400 * ui), bh = (int)(70 * ui);
                SDL_Rect inputBox = {LOGICAL_WIDTH/2 - bw/2, menuPanelY + 240, bw, bh};
                
                if (SDL_PointInRect(&clickPoint, &inputBox)) {
                    isTextBoxFocused = true;
                    SDL_StartTextInput(); 
                } else {
                    isTextBoxFocused = false;
                    SDL_StopTextInput();  
                }
            } else if (state == AppState::JOIN_INPUT) {
                int bw = (int)(200 * ui), bh = (int)(60 * ui);
                SDL_Rect inputBox = {LOGICAL_WIDTH/2 - bw/2, LOGICAL_HEIGHT/2, bw, bh};
                if (SDL_PointInRect(&clickPoint, &inputBox)) {
                    isTextBoxFocused = true;
                    SDL_StartTextInput();
                } else {
                    isTextBoxFocused = false;
                    SDL_StopTextInput();
                }
            }

            // 1. Check UI Buttons
            for (const auto& btn : activeButtons) {
                if (SDL_PointInRect(&clickPoint, &btn.rect)) {
                    playSound(Sfx::CLICK);
                    if (btn.onClick) btn.onClick();
                    actionHandled = true;
                    break; 
                }
            }

            // 2. Play Area Logic
            if (!actionHandled && state == AppState::PLAYING && match && !needsSuitSelection) {
                bool isMyTurn = (match->getCurrentPlayerIndex() == myIndex);
                if (isMyTurn) {
                    int standardCardW = 126;
                    int standardCardH = 180;
#ifdef __ANDROID__
                    standardCardW = 168; 
                    standardCardH = 240; 
#endif
                    int deckX = LOGICAL_WIDTH / 2 - standardCardW - 20;
                    int deckY = LOGICAL_HEIGHT / 2 - standardCardH / 2 - (int)(40 * ui);
                    int pileX = LOGICAL_WIDTH / 2 + 20;

                    SDL_Rect deckRect = { deckX, deckY, standardCardW, standardCardH };
                    SDL_Rect pileRect = { pileX, deckY, standardCardW, standardCardH };

                    if (SDL_PointInRect(&clickPoint, &deckRect)) {
                        if (onDrawClicked) onDrawClicked();
                        actionHandled = true;
                    }

                    if (!actionHandled && SDL_PointInRect(&clickPoint, &pileRect) && selectedCardIndex != -1) {
                        if (onCardPlayed) onCardPlayed(selectedCardIndex);
                        selectedCardIndex = -1;
                        actionHandled = true;
                    }

                    if (!actionHandled) {
                        for (int i = (int)handHitboxes.size() - 1; i >= 0; --i) {
                            if (SDL_PointInRect(&clickPoint, &handHitboxes[i])) {
                                if (selectedCardIndex == i) {
                                    if (onCardPlayed) onCardPlayed(i);
                                    selectedCardIndex = -1; 
                                } else {
                                    selectedCardIndex = i;
                                    playSound(Sfx::SELECT);
                                }
                                break; 
                            }
                        }
                    }
                }
            }
        }
    }

    if (state != AppState::NAME_INPUT && state != AppState::JOIN_INPUT) {
        if (isTextBoxFocused) {
            isTextBoxFocused = false;
            SDL_StopTextInput();
        }
    }
}

void GameWindow::render(float dt, float turnProgress, AppState state, Match* match, int myIndex, const std::string& myName, const std::string& lobbyCode, const std::vector<PlayerInfo>& lobbyPlayers, const std::vector<PublicLobbyInfo>& publicLobbies, const std::string& hostName, int targetScore, bool sortBySuit) {
    
    // --- TRUE PHYSICAL SAFE ZONE MATH ---
    // (This guarantees viewX is defined for the Ace UI bleed later!)
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    float scaleX = (float)w / LOGICAL_WIDTH;
    float scaleY = (float)h / LOGICAL_HEIGHT;
    float scale = (std::min)(scaleX, scaleY);
    
    int viewW = (int)(LOGICAL_WIDTH * scale);
    int viewH = (int)(LOGICAL_HEIGHT * scale);
    int viewX = (w - viewW) / 2;
    int viewY = (h - viewH) / 2;

    int kbOffsetY = 0;
#ifdef __ANDROID__
    if ((state == AppState::NAME_INPUT || state == AppState::JOIN_INPUT) && isTextBoxFocused) {
        kbOffsetY = (int)(250 * scale);
    }
#endif

    // --- 1. RENDER BACKGROUND (Pinned to Screen) ---
    SDL_RenderSetViewport(renderer, NULL); 
    SDL_RenderSetScale(renderer, 1.0f, 1.0f); 
    
    if (tableBackground) {
        int texW, texH;
        SDL_QueryTexture(tableBackground, NULL, NULL, &texW, &texH);
        
        float screenAspect = (float)w / h;
        float texAspect = (float)texW / texH;
        SDL_Rect srcRect;
        
        if (texAspect > screenAspect) {
            srcRect.h = texH;
            srcRect.w = (int)(texH * screenAspect);
            srcRect.x = (texW - srcRect.w) / 2;
            srcRect.y = 0;
        } else {
            srcRect.w = texW;
            srcRect.h = (int)(texW / screenAspect);
            srcRect.x = 0;
            srcRect.y = (texH - srcRect.h) / 2;
        }

        // THE FIX: DestRect is {0, 0, w, h}. 
        // It no longer slides up with -kbOffsetY, meaning no black static at the bottom!
        SDL_Rect destRect = {0, 0, w, h};
        SDL_RenderCopy(renderer, tableBackground, &srcRect, &destRect);
    } else {
        SDL_SetRenderDrawColor(renderer, 35, 107, 43, 255); 
        SDL_RenderClear(renderer);
    }

    // --- 2. SET SAFE ZONE VIEWPORT FOR UI ---
    // The UI slides up smoothly OVER the pinned background
    SDL_Rect safeZone = { viewX, viewY - kbOffsetY, viewW, viewH };
    SDL_RenderSetViewport(renderer, &safeZone);
    SDL_RenderSetScale(renderer, scale, scale);

    activeButtons.clear(); 
    handHitboxes.clear();

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color black = {0, 0, 0, 255};
    SDL_Color darkGray = {50, 50, 50, 255};
    SDL_Color blue = {50, 100, 200, 255};
    SDL_Color red = {200, 50, 50, 255};

    // --- GLOBAL UI SCALE FACTOR ---
    float ui = 1.0f;
#ifdef __ANDROID__
    ui = 1.5f; // Visual buttons are 60% larger globally
#endif

    if (state == AppState::NAME_INPUT) {
        // 1. Decorative UI Panel
        int panelW = (int)(600 * ui);
        int panelH = (int)(450 * ui);
        SDL_Rect menuPanel = { LOGICAL_WIDTH / 2 - panelW / 2, LOGICAL_HEIGHT / 2 - panelH / 2, panelW, panelH };
        drawPixelUIBox(menuPanel.x, menuPanel.y, menuPanel.w, menuPanel.h, {20, 30, 25, 230}, false);

        // 2. Decorative Ace of Spades
        int cardW = 126, cardH = 180;
        renderCard(Card("A", "Spades"), LOGICAL_WIDTH/2 - cardW/2, menuPanel.y - cardH/2 - 20, cardW, cardH);

        // 3. Golden Title Text
        renderText("PLAYER PROFILE", LOGICAL_WIDTH/2 - 160, menuPanel.y + 100, {255, 215, 0, 255}, true, 320, 1.2f);
        renderText("Enter your name:", LOGICAL_WIDTH/2 - 120, menuPanel.y + 180, {200, 200, 200, 255}, true, 240, 0.8f);
        
        // 4. The Text Box
        int bw = (int)(400 * ui), bh = (int)(70 * ui);
        SDL_Rect inputBox = {LOGICAL_WIDTH/2 - bw/2, menuPanel.y + 240, bw, bh};
        
        // Highlight box when typing
        SDL_Color boxColor = isTextBoxFocused ? SDL_Color{255, 255, 255, 255} : SDL_Color{200, 200, 200, 255};
        drawPixelUIBox(inputBox.x, inputBox.y, inputBox.w, inputBox.h, boxColor, false);
        
        // Blinking Cursor logic
        std::string dispText = currentTextInput;
        if (isTextBoxFocused && (SDL_GetTicks() / 500) % 2 == 0) dispText += "|";
        else dispText += " ";
        
        renderText(dispText, inputBox.x + 20, inputBox.y + 15 + (isTextBoxFocused ? 0 : 2), black);

        // 5. Submit Button (Mobile Lifesaver!)
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - bw/2, inputBox.y + bh + 30, bw, bh}, "Continue", blue, [this]() {
            if (currentTextInput.length() > 0 && onNameEntered) {
                isTextBoxFocused = false;
                SDL_StopTextInput();
                SDL_SetWindowTitle(window, ("Agonia user:" + currentTextInput).c_str());
                onNameEntered(currentTextInput);
                currentTextInput = "";
            }
        }});
    }
    else if (state == AppState::MAIN_MENU) {
        // 1. Decorative UI Panel (A dark translucent box for the menu)
        int panelW = (int)(600 * ui);
        int panelH = (int)(550 * ui);
        SDL_Rect menuPanel = { LOGICAL_WIDTH / 2 - panelW / 2, LOGICAL_HEIGHT / 2 - panelH / 2, panelW, panelH };
        drawPixelUIBox(menuPanel.x, menuPanel.y, menuPanel.w, menuPanel.h, {20, 30, 25, 230}, false);

        // 2. Decorative Cards behind the title
        int cardW = 126, cardH = 180;
        // Left Card (Grayed out, pushed back)
        renderCard(Card("J", "Spades"), LOGICAL_WIDTH/2 - cardW - 30, menuPanel.y - cardH/2 + 20, cardW, cardH, false, false, true); 
        // Right Card (Grayed out, pushed back)
        renderCard(Card("Q", "Hearts"), LOGICAL_WIDTH/2 + 30, menuPanel.y - cardH/2 + 20, cardW, cardH, false, false, true);
        // Center Card (Bright, pushed up)
        renderCard(Card("K", "Diamonds"), LOGICAL_WIDTH/2 - cardW/2, menuPanel.y - cardH/2 - 10, cardW, cardH); 

        // 3. Golden Title Text
        renderText("AGONIA", LOGICAL_WIDTH/2 - 120, menuPanel.y + 100, {255, 215, 0, 255}, true, 240, 1.5f); 
        renderText("MULTIPLAYER", LOGICAL_WIDTH/2 - 100, menuPanel.y + 160, {200, 200, 200, 255}, true, 200, 0.8f);

        // 4. Color-Coded Buttons (Bigger, friendlier)
        int bw = (int)(400 * ui), bh = (int)(60 * ui), gap = (int)(75 * ui);
        int startY = menuPanel.y + 220;

        activeButtons.push_back({ {LOGICAL_WIDTH/2 - bw/2, startY, bw, bh}, "Create Public Game", blue, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(1); } });
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - bw/2, startY + gap, bw, bh}, "Create Private Game", {180, 50, 50, 255}, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(2); } });
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - bw/2, startY + gap*2, bw, bh}, "Find Public Games", {50, 150, 50, 255}, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(4); } });
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - bw/2, startY + gap*3, bw, bh}, "Join by Code", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(3); } });
    // Add the "?" Rules Button anchored to the top right of the panel!
        int qSize = (int)(60 * ui);
        activeButtons.push_back({ {menuPanel.x + 10, menuPanel.y + 10, (int)(120 * ui), qSize}, isMuted ? "Unmute" : "Mute", darkGray, [this]() { isMuted = !isMuted; } });
        activeButtons.push_back({ {menuPanel.x + menuPanel.w - qSize - 10, menuPanel.y + 10, qSize, qSize}, "?", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(5); } });
    }
    else if (state == AppState::RULES) {
        // 1. Static Glass Panel
        int panelW = 1600;
        int panelH = 950;
        SDL_Rect rulesPanel = { LOGICAL_WIDTH / 2 - panelW / 2, LOGICAL_HEIGHT / 2 - panelH / 2, panelW, panelH };
        drawPixelUIBox(rulesPanel.x, rulesPanel.y, rulesPanel.w, rulesPanel.h, {20, 30, 25, 245}, false);

        // 2. Static Header & Footer Elements
        renderText("HOW TO PLAY AGONIA", rulesPanel.x, rulesPanel.y + 40, {255, 215, 0, 255}, true, panelW, 1.5f);

        int btnW = (int)(300 * ui), btnH = (int)(70 * ui);
        activeButtons.push_back({ 
            {LOGICAL_WIDTH/2 - btnW/2, rulesPanel.y + panelH - btnH - 30, btnW, btnH}, 
            "Back to Menu", 
            darkGray, 
            std::function<void()>([this]() { 
                if(onMenuOptionSelected) onMenuOptionSelected(-1); 
                rulesScrollY = 0.0f; // Reset scroll when leaving!
            }) 
        });

        // 3. HARDWARE CLIPPING (The Scroll Window)
        // THE FIX: Push clipY down to 150 to clear the title, and let SDL handle the scaling automatically!
        int clipY = rulesPanel.y + 150;
        int clipH = panelH - 150 - btnH - 40; 
        
        SDL_Rect logicalClip = { rulesPanel.x + 20, clipY, panelW - 40, clipH };
        SDL_RenderSetClipRect(renderer, &logicalClip);

        // --- SCROLLABLE CONTENT BELOW ---
        int leftX = rulesPanel.x + 80;
        int startY = clipY + 20 + (int)rulesScrollY; // All Y coordinates shift by rulesScrollY!

        // Objective
        renderText("OBJECTIVE:", leftX, startY, {100, 200, 255, 255}, false, 0, 1.2f);
        renderText("Be the first player to discard all your cards.", leftX, startY + 60, white, false, 0, 0.9f);
        renderText("You must match the Top Card's SUIT or VALUE.", leftX, startY + 100, white, false, 0, 0.9f);
        
        // Drawing & Passing
        startY += 180;
        renderText("DRAWING & PASSING:", leftX, startY, {100, 200, 255, 255}, false, 0, 1.2f);
        renderText("- If you cannot play, you MUST tap the deck to DRAW.", leftX, startY + 60, white, false, 0, 0.9f);
        renderText("- You can only draw ONCE per turn.", leftX, startY + 100, {255, 215, 0, 255}, false, 0, 0.9f);
        renderText("- If you still cannot play after drawing, tap PASS to end your turn.", leftX, startY + 140, white, false, 0, 0.9f);

        // Special Cards
        startY += 220;
        renderText("SPECIAL CARDS:", leftX, startY, {100, 200, 255, 255}, false, 0, 1.2f);

        int cardW = 70;
        int cardH = 100;
        int textOffsetX = cardW + 30;
        int rowSpacing = cardH + 30;
        startY += 70;

        renderCard(Card("A", "Spades"), leftX, startY, cardW, cardH);
        renderText("ACE: Change active suit. Playable on anything.", leftX + textOffsetX, startY + (cardH/2) - 20, white, false, 0, 0.9f);
        startY += rowSpacing;

        renderCard(Card("7", "Hearts"), leftX, startY, cardW, cardH);
        renderText("SEVEN: Next player draws 2 cards (Stacks!).", leftX + textOffsetX, startY + (cardH/2) - 20, white, false, 0, 0.9f);
        startY += rowSpacing;

        renderCard(Card("8", "Diamonds"), leftX, startY, cardW, cardH);
        renderText("EIGHT: Play another card immediately.", leftX + textOffsetX, startY + (cardH/2) - 20, white, false, 0, 0.9f);
        startY += rowSpacing;

        renderCard(Card("9", "Clubs"), leftX, startY, cardW, cardH);
        renderText("NINE: Skips the next player's turn.", leftX + textOffsetX, startY + (cardH/2) - 20, white, false, 0, 0.9f);
        startY += rowSpacing;

        // Ending Constraints
        renderText("IMPORTANT CONSTRAINTS:", leftX, startY, {255, 100, 100, 255}, false, 0, 1.2f);
        renderText("- You CANNOT win the round by playing a special card as your last card.", leftX, startY + 60, white, false, 0, 0.9f);
        renderText("  (If you do, you will be penalized and forced to draw!)", leftX, startY + 100, {200, 200, 200, 255}, false, 0, 0.8f);

        // Points
        startY += 180;
        renderText("POINTS SYSTEM:", leftX, startY, {100, 200, 255, 255}, false, 0, 1.2f);
        renderText("When a round ends, cards left in your hand add to your penalty score:", leftX, startY + 60, white, false, 0, 0.9f);
        renderText("- Number Cards (2-6, 10) = Face Value", leftX, startY + 100, white, false, 0, 0.9f);
        renderText("- Face Cards (J, Q, K) = 10 Points", leftX, startY + 140, white, false, 0, 0.9f);
        renderText("- Special Cards (7, 8, 9) = 10 Points", leftX, startY + 180, white, false, 0, 0.9f);
        renderText("- Aces = 11 Points", leftX, startY + 220, white, false, 0, 0.9f);
        renderText("If you reach the Target Score, you lose the tournament!", leftX, startY + 280, {255, 215, 0, 255}, false, 0, 0.9f);

        // 4. Update the Max Scroll Bounds dynamically based on the final text position
        int totalContentHeight = (startY + 350) - (clipY + 20 + (int)rulesScrollY);
        rulesMaxScroll = (float)(totalContentHeight - clipH);
        if (rulesMaxScroll < 0) rulesMaxScroll = 0;

        // 5. DISABLE CLIPPING so the rest of the game UI draws correctly!
        SDL_RenderSetClipRect(renderer, NULL);
    }
    else if (state == AppState::FIND_LOBBY) {
        int bw = (int)(500 * ui), bh = (int)(60 * ui), gap = (int)(80 * ui);
        renderText("PUBLIC LOBBIES", LOGICAL_WIDTH/2 - 150, 100, white);
        
        if (publicLobbies.empty()) {
            renderText("No public games available right now.", LOGICAL_WIDTH/2 - 220, 250, white);
        } else {
            int yOff = 200;
            for (const auto& lob : publicLobbies) {
                std::string btnText = "Join " + lob.hostName + " (" + std::to_string(lob.playerCount) + "/4)";
                std::string joinCode = lob.code;
                activeButtons.push_back({ {LOGICAL_WIDTH/2 - bw/2, yOff, bw, bh}, btnText, blue, [this, joinCode]() { 
                    if(onJoinCodeEntered) onJoinCodeEntered(joinCode); 
                }});
                yOff += gap;
            }
        }
        int backW = (int)(200 * ui);
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - backW/2, LOGICAL_HEIGHT - (int)(150 * ui), backW, bh}, "Back", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(-1); } });
    }
    else if (state == AppState::JOIN_INPUT) {
        int bw = (int)(200 * ui), bh = (int)(60 * ui);
        
        SDL_Color textWhite = {255, 255, 255, 255};
        SDL_Color textBlack = {0, 0, 0, 255};
        SDL_Color btnGray = {100, 100, 100, 255};
        SDL_Color btnBlue = {50, 100, 200, 255}; 
        SDL_Color errorRed = {255, 50, 50, 255};

        renderText("Enter 4-Character Code:", LOGICAL_WIDTH/2 - 190, LOGICAL_HEIGHT/2 - 50, textWhite);
        SDL_Rect inputBox = {LOGICAL_WIDTH/2 - bw/2, LOGICAL_HEIGHT/2, bw, bh};
        
        SDL_Color boxColor = isTextBoxFocused ? SDL_Color{255, 255, 255, 255} : SDL_Color{200, 200, 200, 255};
        drawPixelUIBox(inputBox.x, inputBox.y, inputBox.w, inputBox.h, boxColor, false);
        
        std::string dispText = currentTextInput;
        if (isTextBoxFocused && (SDL_GetTicks() / 500) % 2 == 0) dispText += "|";
        else dispText += " ";
        
        renderText(dispText, inputBox.x + 20, inputBox.y + 15 + (isTextBoxFocused ? 0 : 2), textBlack);
        
        if (!joinErrorMessage.empty()) {
            renderText(joinErrorMessage, LOGICAL_WIDTH/2 - 150, inputBox.y + bh + 20, errorRed, true, 300, 0.8f);
        }
        
        int joinW = (int)(250 * ui);
        activeButtons.push_back({ 
            {LOGICAL_WIDTH/2 - joinW/2, inputBox.y + bh + (int)(50 * ui), joinW, bh}, 
            "Join Game", 
            btnBlue, 
            std::function<void()>([this]() { 
                if(currentTextInput.length() == 4 && onJoinCodeEntered) {
                    isTextBoxFocused = false;
                    SDL_StopTextInput();
                    onJoinCodeEntered(currentTextInput);
                }
            }) 
        });

        int backW = (int)(200 * ui);
        activeButtons.push_back({ 
            {LOGICAL_WIDTH/2 - backW/2, LOGICAL_HEIGHT/2 + (int)(200 * ui), backW, bh}, 
            "Back", 
            btnGray, 
            std::function<void()>([this]() { 
                if(onMenuOptionSelected) onMenuOptionSelected(-1); 
            }) 
        });
    }
    else if (state == AppState::LOBBY) {
        bool isHost = (myName == hostName); 

        // 1. Main Lobby Panel
        int panelW = (int)(700 * ui);
        int panelH = (int)(550 * ui);
        SDL_Rect lobbyPanel = { LOGICAL_WIDTH / 2 - panelW / 2, LOGICAL_HEIGHT / 2 - panelH / 2 - (int)(30 * ui), panelW, panelH };
        drawPixelUIBox(lobbyPanel.x, lobbyPanel.y, lobbyPanel.w, lobbyPanel.h, {20, 30, 25, 230}, false);

        // 2. Lobby Header
        renderText("LOBBY CODE: " + lobbyCode, lobbyPanel.x, lobbyPanel.y + (int)(30 * ui), {255, 215, 0, 255}, true, lobbyPanel.w, 1.2f);
        if (!isHost) {
            renderText("Target Score: " + std::to_string(targetScore), lobbyPanel.x, lobbyPanel.y + (int)(80 * ui), {200, 200, 200, 255}, true, lobbyPanel.w, 0.8f);
        }

        // 3. The 4 Player Slots
        int slotW = panelW - (int)(60 * ui);
        int slotH = (int)(70 * ui);
        int startX = lobbyPanel.x + (int)(30 * ui);
        int startY = lobbyPanel.y + (int)(130 * ui);

        for (int i = 0; i < 4; ++i) {
            int currentY = startY + (i * (slotH + (int)(15 * ui)));
            SDL_Rect slotRect = { startX, currentY, slotW, slotH };

            if (i < lobbyPlayers.size()) {
                // Occupied Slot
                drawPixelUIBox(slotRect.x, slotRect.y, slotRect.w, slotRect.h, {40, 60, 50, 255}, false);
                
                std::string name = lobbyPlayers[i].name;
                SDL_Color nameColor = white;
                std::string tag = "";
                
                if (name == hostName) { 
                    nameColor = {255, 215, 0, 255}; // Gold for host
                    tag = " (HOST)"; 
                } else if (lobbyPlayers[i].isBot) { 
                    tag = " (BOT)"; 
                }

                // Center text vertically in the slot
                renderText(name + tag, slotRect.x + (int)(20 * ui), slotRect.y + (slotRect.h / 2) - 15, nameColor);

                // Kick Button (Anchored to the right side of the slot)
                if (isHost && name != hostName) {
                    int kickW = (int)(120 * ui);
                    int kickH = (int)(50 * ui);
                    activeButtons.push_back({ {slotRect.x + slotRect.w - kickW - (int)(10 * ui), slotRect.y + (slotRect.h - kickH) / 2, kickW, kickH}, "Kick", red, [this, name]() { 
                        if(onKickPlayerClicked) onKickPlayerClicked(name); 
                    }});
                }
            } else {
                // Empty Slot
                drawPixelUIBox(slotRect.x, slotRect.y, slotRect.w, slotRect.h, {30, 30, 30, 150}, false);
                renderText("Waiting for player...", slotRect.x + (int)(20 * ui), slotRect.y + (slotRect.h / 2) - 15, {120, 120, 120, 255});
            }
        }

        // 4. Action Buttons (Underneath the panel)
        int btnW = (int)(220 * ui), btnH = (int)(70 * ui);
        int actionY = lobbyPanel.y + panelH + (int)(30 * ui);

        if (isHost) {
            int gap = btnW + (int)(20 * ui);
            int btnStartX = LOGICAL_WIDTH/2 - (gap * 1) + (gap/2); 
            
            activeButtons.push_back({ {btnStartX - gap, actionY, btnW, btnH}, "Target: " + std::to_string(targetScore), darkGray, onToggleScoreClicked });
            activeButtons.push_back({ {btnStartX, actionY, btnW, btnH}, "Fill Bots", darkGray, onFillBotsClicked });
            
            // THE FIX: Lock the Start Button unless there are exactly 2 or 4 players
            int pCount = lobbyPlayers.size();
            bool canStart = (pCount == 2 || pCount == 4);
            SDL_Color startColor = canStart ? blue : SDL_Color{100, 100, 100, 255}; // Gray out if locked
            
            activeButtons.push_back({ {btnStartX + gap, actionY, btnW, btnH}, "Start Game", startColor, [this, canStart]() {
                if (canStart && onStartGameClicked) onStartGameClicked();
            }});
        } else {
            renderText("Waiting for host to start...", LOGICAL_WIDTH/2 - 190, actionY + 20, white);
        }
        
        // Exit Button (Top Left corner like a standard back button)
        activeButtons.push_back({ {(int)(30 * ui), (int)(30 * ui), (int)(120 * ui), (int)(60 * ui)}, "Exit", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(-1); } });
    }
    else if (state == AppState::PLAYING && match) {
        bool isMyTurn = (match->getCurrentPlayerIndex() == myIndex);
        
        // --- DECOUPLED CARD SCALING ---
        int standardCardW = 126;
        int standardCardH = 180;
        
        int handCardW = 126;
        int handCardH = 180;
        int handSpacing = 80;
        int handYOffset = 50; 

#ifdef __ANDROID__
        standardCardW = 168; 
        standardCardH = 240; 
        
        handCardW = 240;   
        handCardH = 342;
        handSpacing = 140; 
        handYOffset = -60; 
#endif

        // --- 1. TOP HUD (Game Info) ---
        int hudW = (int)(400 * ui), hudH = (int)(60 * ui);
        
        // Center on Desktop, Anchor to Top-Left on Mobile
        int hudX = LOGICAL_WIDTH / 2 - hudW / 2;
#ifdef __ANDROID__
        hudX = (int)(30 * ui); 
#endif

        SDL_Rect hudRect = { hudX, (int)(20 * ui), hudW, hudH };
        drawPixelUIBox(hudRect.x, hudRect.y, hudRect.w, hudRect.h, {20, 30, 25, 230}, false);
        
        std::string currentPlayerName = "Unknown";
        if (match->getCurrentPlayerIndex() < match->getPlayers().size()) {
            currentPlayerName = match->getPlayers()[match->getCurrentPlayerIndex()].getName();
        }
        std::string turnText = isMyTurn ? "YOUR TURN" : (currentPlayerName + "'s Turn");
        SDL_Color turnColor = isMyTurn ? SDL_Color{50, 255, 50, 255} : white;
        renderText(turnText, hudRect.x, hudRect.y + (int)(15 * ui), turnColor, true, hudW, 1.0f);

        // --- 2. THE TABLE (Center) ---
        int deckX = LOGICAL_WIDTH / 2 - standardCardW - 20;
        int deckY = LOGICAL_HEIGHT / 2 - standardCardH / 2 - (int)(40 * ui);
        int pileX = LOGICAL_WIDTH / 2 + 20;
        
        // Contextual Text hovering directly over the table cards!
        if (match->getCardsToDraw() > 0) {
            renderText("DRAW " + std::to_string(match->getCardsToDraw()), deckX, deckY - (int)(30 * ui)-40, {255, 50, 50, 255}, true, standardCardW, 0.9f);
        }
        std::string suitText = match->getDeclaredSuit();
        if (!suitText.empty()) {
            renderText("SUIT: " + suitText, pileX, deckY - (int)(30 * ui)-40, {255, 215, 0, 255}, true, standardCardW, 0.9f);
        }

        // Draw the Physical Deck (Stacked cards)
        for(int i = 0; i < 4; ++i) { 
            renderCardBack(deckX - (i*4), deckY - (i*4), standardCardW, standardCardH, false);
        }
        
        // Draw the Discard Pile
        Card topCard = match->getTopCard();
        renderCard(topCard, pileX, deckY, standardCardW, standardCardH);

        // ONLY render/activate gameplay buttons if we aren't picking an Ace suit
        if (!needsSuitSelection) {
            int btnW = (int)(160 * ui), btnH = (int)(60 * ui);
            int btnY = deckY + standardCardH + (int)(30 * ui);
            
            activeButtons.push_back({ {deckX + (standardCardW/2) - (btnW/2) - 6, btnY, btnW, btnH}, "DRAW", blue, onDrawClicked });
            activeButtons.push_back({ {pileX + (standardCardW/2) - (btnW/2), btnY, btnW, btnH}, "PASS", darkGray, onPassClicked });

            int sortW = (int)(180 * ui), sortH = (int)(60 * ui);
            int muteW = (int)(120 * ui), muteH = (int)(60 * ui);
            activeButtons.push_back({ {LOGICAL_WIDTH - muteW - 30, (int)(20 * ui), muteW, muteH}, isMuted ? "Unmute" : "Mute", darkGray, [this]() { isMuted = !isMuted; } });
            activeButtons.push_back({ {LOGICAL_WIDTH - sortW - 30, LOGICAL_HEIGHT - sortH - 30, sortW, sortH}, sortBySuit ? "Sort: Suit" : "Sort: Value", darkGray, onSortClicked });
        }

        // --- 3. OPPONENTS (Cards and Nameplates) ---
        int numPlayers = match->getPlayers().size();
        for (int i = 0; i < numPlayers; ++i) {
            if (i == myIndex) continue;

            int relativePos = (i - myIndex + numPlayers) % numPlayers;
            int cardCount = match->getPlayers()[i].getHand().size();
            
            int oppCardW = (int)(70 * ui), oppCardH = (int)(105 * ui); 
            int spacing = (int)(25 * ui); 

            bool isTop = (numPlayers == 2 && relativePos == 1) || (numPlayers == 4 && relativePos == 2);
            bool isLeft = (numPlayers >= 3 && relativePos == 1);
            bool isRight = (numPlayers == 3 && relativePos == 2) || (numPlayers == 4 && relativePos == 3);

            int nameW = (int)(200 * ui), nameH = (int)(40 * ui);
            std::string nameLabel = match->getPlayers()[i].getName() + " (" + std::to_string(cardCount) + ")";

            if (isTop) {
                // THE FIX: Push down on Desktop to clear the centered HUD, 
                // but move way up on Mobile to reclaim the empty center space!
                int startY = (int)(130 * ui); 
#ifdef __ANDROID__
                startY = (int)(60 * ui); 
#endif
                int totalWidth = (cardCount > 0) ? ((cardCount - 1) * spacing + oppCardW) : 0;
                int startX = (LOGICAL_WIDTH - totalWidth) / 2;
                for (int c = 0; c < cardCount; ++c) {
                    renderCardBack(startX + (c * spacing), startY, oppCardW, oppCardH, false);
                }
                drawPixelUIBox(LOGICAL_WIDTH/2 - nameW/2, startY - nameH - 10, nameW, nameH, {30, 30, 30, 200}, false);
                renderText(nameLabel, LOGICAL_WIDTH/2 - nameW/2, startY - nameH - 10 + (int)(5*ui), white, true, nameW, 0.8f);
            }
            else if (isLeft) {
                int totalHeight = (cardCount > 0) ? ((cardCount - 1) * spacing + oppCardW) : 0;
                int startY = (LOGICAL_HEIGHT - totalHeight) / 2;
                for (int c = 0; c < cardCount; ++c) {
                    renderCardBack(50, startY + (c * spacing), oppCardW, oppCardH, true); 
                }
                drawPixelUIBox(20, startY - nameH - 10, nameW, nameH, {30, 30, 30, 200}, false);
                renderText(nameLabel, 20, startY - nameH - 10 + (int)(5*ui), white, true, nameW, 0.8f);
            } 
            else if (isRight) {
                int totalHeight = (cardCount > 0) ? ((cardCount - 1) * spacing + oppCardW) : 0;
                int startY = (LOGICAL_HEIGHT - totalHeight) / 2;
                for (int c = 0; c < cardCount; ++c) {
                    renderCardBack(LOGICAL_WIDTH - oppCardH - 50, startY + (c * spacing), oppCardW, oppCardH, true);
                }
                drawPixelUIBox(LOGICAL_WIDTH - nameW - 20, startY - nameH - 10, nameW, nameH, {30, 30, 30, 200}, false);
                renderText(nameLabel, LOGICAL_WIDTH - nameW - 20, startY - nameH - 10 + (int)(5*ui), white, true, nameW, 0.8f);
            }
        }

        // --- 4. ANIMATIONS ---
        for (auto it = activeAnimations.begin(); it != activeAnimations.end(); ) {
            // THE FIX: Lower the speed slightly so it takes ~0.4 seconds to complete
            it->progress += 2.5f * dt; 
            
            if (it->progress >= 1.0f) {
                it = activeAnimations.erase(it);
            } else {
                // THE FIX: Cubic Ease-Out formula (Starts fast, smoothly decelerates)
                float t = it->progress;
                float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); 
                
                float currentX = it->startX + (it->targetX - it->startX) * ease;
                float currentY = it->startY + (it->targetY - it->startY) * ease;
                
                if (it->card.getSuit() == "Hidden") {
                    renderCardBack((int)currentX, (int)currentY, standardCardW, standardCardH, false);
                } else {
                    renderCard(it->card, (int)currentX, (int)currentY, standardCardW, standardCardH, false, false, false);
                }
                ++it;
            }
        }

        // --- 5. PLAYER'S HAND ---
        if (myIndex >= 0 && myIndex < match->getPlayers().size()) {
            const auto& hand = match->getPlayers()[myIndex].getHand();
            
            int maxHandWidth = LOGICAL_WIDTH - 100; 
            int totalSpacingNeeded = hand.size() * handSpacing;
            int spacing = (totalSpacingNeeded > maxHandWidth) ? (maxHandWidth / (int)hand.size()) : handSpacing;
            
            int startX = (LOGICAL_WIDTH - (hand.size() * spacing)) / 2;
            int baseY = LOGICAL_HEIGHT - handCardH - handYOffset;

            for (size_t i = 0; i < hand.size(); ++i) {
                int drawY = baseY;
                bool isSelected = (i == selectedCardIndex);
                if (isSelected && isMyTurn) drawY -= 50; 

                renderCard(hand[i], startX + (int)(i * spacing), drawY, handCardW, handCardH, false, isSelected, !isMyTurn);
                handHitboxes.push_back({ startX + (int)(i * spacing), drawY, handCardW, handCardH });
            }
            
            // --- 6. TIMER BAR ---
            if (isMyTurn) {
                int timerW = (int)(600 * ui);
                int timerH = (int)(12 * ui);
                int timerX = LOGICAL_WIDTH / 2 - timerW / 2;
                int timerY = baseY - timerH - (int)(30 * ui);

                drawPixelUIBox(timerX, timerY, timerW, timerH, {0, 0, 0, 200}, false);
                
                int currentWidth = (int)((timerW - 8) * turnProgress);
                if (currentWidth < 0) currentWidth = 0;
                SDL_Rect timerFill = { timerX + 4, timerY + 4, currentWidth, timerH - 8 }; 
                
                SDL_Color timerColor = {50, 255, 50, 255}; 
                if (turnProgress < 0.4f) timerColor = {255, 200, 50, 255}; 
                if (turnProgress < 0.15f) timerColor = {255, 50, 50, 255}; 

                SDL_SetRenderDrawColor(renderer, timerColor.r, timerColor.g, timerColor.b, timerColor.a);
                SDL_RenderFillRect(renderer, &timerFill);
            }
        }

        // --- 7. SUIT SELECTION (Overlay) ---
        if (needsSuitSelection) {
            SDL_RenderSetViewport(renderer, NULL);
            SDL_RenderSetScale(renderer, 1.0f, 1.0f);
            
            SDL_Rect overlay = {0, -kbOffsetY, w, h};
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(renderer, &overlay);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

            SDL_Rect safeZone = { viewX, viewY - kbOffsetY, viewW, viewH };
            SDL_RenderSetViewport(renderer, &safeZone);
            SDL_RenderSetScale(renderer, scale, scale);

            int titleW = (int)(400 * ui), titleH = (int)(80 * ui);
            drawPixelUIBox(LOGICAL_WIDTH/2 - titleW/2, LOGICAL_HEIGHT/2 - (int)(180 * ui), titleW, titleH, {20, 30, 25, 255}, false);
            renderText("CHOOSE A SUIT", LOGICAL_WIDTH/2 - titleW/2, LOGICAL_HEIGHT/2 - (int)(180 * ui) + (int)(20*ui), {255, 215, 0, 255}, true, titleW, 1.2f);
            
            auto pickSuit = [this](std::string s) { needsSuitSelection = false; if(onSuitSelected) onSuitSelected(s); };

            int selBtnW = (int)(200 * ui), selBtnH = (int)(80 * ui);
            int xOffset = (int)(20 * ui);

            activeButtons.push_back({ {LOGICAL_WIDTH/2 - selBtnW - xOffset, LOGICAL_HEIGHT/2 - (int)(50 * ui), selBtnW, selBtnH}, "Hearts", red, [=](){pickSuit("Hearts");} });
            activeButtons.push_back({ {LOGICAL_WIDTH/2 + xOffset, LOGICAL_HEIGHT/2 - (int)(50 * ui), selBtnW, selBtnH}, "Diamonds", red, [=](){pickSuit("Diamonds");} });
            activeButtons.push_back({ {LOGICAL_WIDTH/2 - selBtnW - xOffset, LOGICAL_HEIGHT/2 + selBtnH - (int)(10 * ui), selBtnW, selBtnH}, "Spades", darkGray, [=](){pickSuit("Spades");} });
            activeButtons.push_back({ {LOGICAL_WIDTH/2 + xOffset, LOGICAL_HEIGHT/2 + selBtnH - (int)(10 * ui), selBtnW, selBtnH}, "Clubs", darkGray, [=](){pickSuit("Clubs");} });

            // THE FIX: Added a Cancel Button!
            int cancelW = (int)(200 * ui), cancelH = (int)(60 * ui);
            activeButtons.push_back({ {LOGICAL_WIDTH/2 - cancelW/2, LOGICAL_HEIGHT/2 + (selBtnH * 2) + (int)(10 * ui), cancelW, cancelH}, "Cancel", {100, 100, 100, 255}, [this](){ needsSuitSelection = false; } });
        }
    }
    else if (state == AppState::GAME_OVER && match) {
        bool tournamentOver = false;
        for (const Player& p : match->getPlayers()) {
            if (p.getScore() >= targetScore) tournamentOver = true;
        }

        if (tournamentOver) {
            renderText("--- TOURNAMENT OVER ---", LOGICAL_WIDTH/2 - 200, 100, red);
        } else {
            renderText("--- ROUND OVER ---", LOGICAL_WIDTH/2 - 150, 100, white);
        }
        
        int yOff = 200;
        for (const Player& p : match->getPlayers()) {
            renderText(p.getName() + " : " + std::to_string(p.getScore()) + " Points", LOGICAL_WIDTH/2 - 120, yOff, white);
            yOff += 60;
        }

        if (tournamentOver) {
            activeButtons.push_back({ {LOGICAL_WIDTH/2 - 150, LOGICAL_HEIGHT - 200, 300, 80}, "Return to Menu", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(-1); } });
        } else {
            bool isHost = (myName == hostName);
            if (isHost) {
                if (isLoadingNextRound) {
                    renderText("Loading next round...", LOGICAL_WIDTH/2 - 180, LOGICAL_HEIGHT - 160, white);
                } else {
                    activeButtons.push_back({ {LOGICAL_WIDTH/2 - 150, LOGICAL_HEIGHT - 200, 300, 80}, "Start Next Round", blue, [this]() { if(onNextRoundClicked) onNextRoundClicked(); }});
                }
            } else {
                renderText("Waiting for Host to start next round...", LOGICAL_WIDTH/2 - 250, LOGICAL_HEIGHT - 200, white);
            }
        }
    }

    for (const auto& btn : activeButtons) renderButton(btn);

    if (notificationTimeout > SDL_GetTicks() && font) {
        int textW = 0, textH = 0;
        TTF_SizeText(font, notificationMessage.c_str(), &textW, &textH);
        
        int paddingX = 80; 
        int boxW = textW + paddingX;
        int boxH = 60; 
        
        if (boxW < 300) boxW = 300; 

        SDL_Rect notifRect = { (LOGICAL_WIDTH - boxW) / 2, 150, boxW, boxH };
        drawPixelUIBox(notifRect.x, notifRect.y, notifRect.w, notifRect.h, {200, 50, 50, 230}, false);
        renderText(notificationMessage, notifRect.x + (boxW - textW) / 2, notifRect.y + (boxH - textH) / 2, white);
    }
    
    SDL_RenderPresent(renderer);
}

// NOTE: Everything below here (drawPixelUIBox, renderCard, getCardSourceRect, etc.) 
// remains EXACTLY the same as your original file! Just leave it as-is.

void GameWindow::drawPixelUIBox(int x, int y, int w, int h, SDL_Color color, bool isHovered) {
    if (isHovered) {
        color.r = (Uint8)(std::min)(255, (int)color.r + 40);
        color.g = (Uint8)(std::min)(255, (int)color.g + 40);
        color.b = (Uint8)(std::min)(255, (int)color.b + 40);
    }
    SDL_Color highlight = { (Uint8)(std::min)(255, (int)color.r + 60), (Uint8)(std::min)(255, (int)color.g + 60), (Uint8)(std::min)(255, (int)color.b + 60), 255 };
    SDL_Color shadow = { (Uint8)(std::max)(0, (int)color.r - 60), (Uint8)(std::max)(0, (int)color.g - 60), (Uint8)(std::max)(0, (int)color.b - 60), 255 };
    SDL_Color border = { 20, 20, 20, 255 }; 
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect fillRect = { x + 4, y + 4, w - 8, h - 8 };
    SDL_RenderFillRect(renderer, &fillRect);
    SDL_SetRenderDrawColor(renderer, highlight.r, highlight.g, highlight.b, 255);
    SDL_Rect topH = { x + 4, y + 4, w - 8, 4 };
    SDL_Rect leftH = { x + 4, y + 4, 4, h - 8 };
    SDL_RenderFillRect(renderer, &topH);
    SDL_RenderFillRect(renderer, &leftH);
    SDL_SetRenderDrawColor(renderer, shadow.r, shadow.g, shadow.b, 255);
    SDL_Rect botS = { x + 4, y + h - 8, w - 8, 4 };
    SDL_Rect rightS = { x + w - 8, y + 4, 4, h - 8 };
    SDL_RenderFillRect(renderer, &botS);
    SDL_RenderFillRect(renderer, &rightS);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 255);
    SDL_Rect topB = { x + 4, y, w - 8, 4 };
    SDL_Rect botB = { x + 4, y + h - 4, w - 8, 4 };
    SDL_Rect leftB = { x, y + 4, 4, h - 8 };
    SDL_Rect rightB = { x + w - 4, y + 4, 4, h - 8 };
    SDL_RenderFillRect(renderer, &topB);
    SDL_RenderFillRect(renderer, &botB);
    SDL_RenderFillRect(renderer, &leftB);
    SDL_RenderFillRect(renderer, &rightB);
}

SDL_Rect GameWindow::getCardSourceRect(const Card& card) {
    int strideX = 65, strideY = 65, cardWidth = 42, cardHeight = 60, offsetX = 12, offsetY = 2; 
    int col = 0;
    std::string val = card.getValue();
    if (val == "A") col = 0; else if (val == "J") col = 10; else if (val == "Q") col = 11; else if (val == "K") col = 12;
    else { try { col = std::stoi(val) - 1; } catch (...) { col = 0; } }
    int row = 0;
    std::string suit = card.getSuit();
    if (suit == "Hearts") row = 0; else if (suit == "Diamonds") row = 1; else if (suit == "Clubs") row = 2; else if (suit == "Spades") row = 3;
    return { (col * strideX) + offsetX, (row * strideY) + offsetY, cardWidth, cardHeight };
}

void GameWindow::renderButton(const UIButton& btn) {
    SDL_Point mousePt = { mouseX, mouseY };
    bool isHovered = SDL_PointInRect(&mousePt, &btn.rect);
    drawPixelUIBox(btn.rect.x, btn.rect.y, btn.rect.w, btn.rect.h, btn.bgColor, isHovered);
    int textOffset = isHovered ? 2 : 0;
    
    // THE FIX: Use the 'centered' flag to perfectly align the text 
    // to the center of the newly widened mobile buttons!
    renderText(btn.text, btn.rect.x + textOffset, btn.rect.y + 12 + textOffset, {255, 255, 255, 255}, true, btn.rect.w);
}

std::string GameWindow::getSuitSymbol(const std::string& suit) {
    if (suit == "Spades") return "S"; if (suit == "Hearts") return "H"; if (suit == "Diamonds") return "D"; if (suit == "Clubs") return "C";
    return suit;
}

void GameWindow::renderCard(const Card& card, int x, int y, int w, int h, bool isHidden, bool isSelected, bool isGrayedOut) {
    SDL_Rect destRect = { x, y, w, h };
    if (isSelected) {
        destRect.y -= 20; 
        SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
        SDL_Rect glowRect = { destRect.x - 4, destRect.y - 4, destRect.w + 8, destRect.h + 8 };
        SDL_RenderFillRect(renderer, &glowRect);
    }
    if (cardSpriteSheet) {
        SDL_Rect srcRect;
        if (isHidden) srcRect = { 0, 4 * 190, 140, 190 }; else srcRect = getCardSourceRect(card);
        SDL_RenderCopy(renderer, cardSpriteSheet, &srcRect, &destRect);
    } else {
        fillRoundedRect(destRect.x, destRect.y, destRect.w, destRect.h, 10, {255,255,255,255});
        if (!isHidden) {
            std::string suit = card.getSuit();
            SDL_Color textColor = (suit == "Hearts" || suit == "Diamonds") ? SDL_Color{200, 0, 0, 255} : SDL_Color{0, 0, 0, 255};
            renderText(card.getValue() + " " + getSuitSymbol(suit), x + 8, y + 8, textColor);
        }
    }
    if (isGrayedOut) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 128); 
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(renderer, &destRect);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
}

void GameWindow::renderText(const std::string& text, int x, int y, SDL_Color color, bool centered, int rectWidth, float scale) {
    if (!font || text.empty()) return;
    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), color);
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
            int scaledW = (int)(surface->w * scale);
            int scaledH = (int)(surface->h * scale);
            int finalX = centered ? x + (rectWidth - scaledW) / 2 : x;
            SDL_Rect destRect = { finalX, y, scaledW, scaledH };
            SDL_RenderCopy(renderer, texture, nullptr, &destRect);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
    }
}

void GameWindow::fillRoundedRect(int x, int y, int w, int h, int r, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect center = {x + r, y, w - 2*r, h};
    SDL_Rect left = {x, y + r, r, h - 2*r};
    SDL_Rect right = {x + w - r, y + r, r, h - 2*r};
    SDL_RenderFillRect(renderer, &center);
    SDL_RenderFillRect(renderer, &left);
    SDL_RenderFillRect(renderer, &right);
    auto fillCorner = [&](int cx, int cy) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx*dx + dy*dy <= r*r) SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
            }
        }
    };
    fillCorner(x + r, y + r);                
    fillCorner(x + w - r - 1, y + r);        
    fillCorner(x + r, y + h - r - 1);        
    fillCorner(x + w - r - 1, y + h - r - 1); 
}

void GameWindow::renderCardBack(int x, int y, int w, int h, bool horizontal) {
    if (cardSpriteSheet) {
        int strideX = 65, strideY = 65, cardWidth = 42, cardHeight = 60, offsetX = 12, offsetY = 2;
        int backCol = 13, backRow = 1; 
        int srcX = (backCol * strideX) + offsetX;
        int srcY = (backRow * strideY) + offsetY;
        SDL_Rect srcRect = { srcX, srcY, cardWidth, cardHeight }; 
        SDL_Rect destRect = { x, y, w, h };
        if (horizontal) {
            SDL_Rect hDestRect = { x, y, h, w }; 
            SDL_RenderCopyEx(renderer, cardSpriteSheet, &srcRect, &hDestRect, 90.0, nullptr, SDL_FLIP_NONE);
        } else {
            SDL_RenderCopy(renderer, cardSpriteSheet, &srcRect, &destRect);
        }
    } else {
        int radius = 10;
        fillRoundedRect(x - 2, y - 2, w + 4, h + 4, radius + 2, {0, 0, 0, 255});
        fillRoundedRect(x, y, w, h, radius, {40, 80, 150, 255});
        SDL_Rect inner = {x + 8, y + 8, w - 16, h - 16};
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100);
        SDL_RenderDrawRect(renderer, &inner);
    }
}