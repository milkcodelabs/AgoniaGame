#include "GameWindow.h"
#include <SDL_image.h>
#include <iostream>
#include <algorithm>
#include <string>

// --- ANDROID ASSET PATHING MACRO ---
#ifdef __ANDROID__
    const std::string ASSET_PATH = "";
#else
    const std::string ASSET_PATH = "assets/";
#endif

void GameWindow::playSound(Sfx sound) {
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
    
    // Calculate exactly how many pixels the 16:9 view takes up on the screen
    int viewW = (int)(LOGICAL_WIDTH * scale);
    int viewH = (int)(LOGICAL_HEIGHT * scale);
    int viewX = (w - viewW) / 2;
    int viewY = (h - viewH) / 2;

    int kbOffsetY = 0;
#ifdef __ANDROID__
    if ((state == AppState::NAME_INPUT || state == AppState::JOIN_INPUT) && isTextBoxFocused) {
        kbOffsetY = (int)(250 * scale); // Scale the keyboard push to physical pixels too!
    }
#endif

    // Track global mouse
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
        if (state == AppState::NAME_INPUT || state == AppState::JOIN_INPUT) {
            if (event.type == SDL_TEXTINPUT) {
                if (currentTextInput.length() < 15) currentTextInput += event.text.text;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_BACKSPACE && currentTextInput.length() > 0) {
                    currentTextInput.pop_back();
                } else if (event.key.keysym.sym == SDLK_RETURN && currentTextInput.length() > 0) {
                    
                    // FORCE KEYBOARD CLOSED ON SUBMIT
                    isTextBoxFocused = false;
                    SDL_StopTextInput();

                    if (state == AppState::NAME_INPUT && onNameEntered) {
                        SDL_SetWindowTitle(window, ("Agonia user:" + currentTextInput).c_str());
                        onNameEntered(currentTextInput);
                        currentTextInput = "";
                    } else if (state == AppState::JOIN_INPUT && onJoinCodeEntered) {
                        onJoinCodeEntered(currentTextInput);
                        currentTextInput = "";
                    }
                }
            }
        }

        // --- TOUCH / MOUSE HANDLING ---
        bool isClick = false;
        int inputX = 0, inputY = 0;

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            isClick = true; 
            inputX = (int)((event.button.x - viewX) / scale);
            inputY = (int)((event.button.y - viewY + kbOffsetY) / scale);
        } else if (event.type == SDL_FINGERDOWN) {
            int rawX = (int)(event.tfinger.x * w);
            int rawY = (int)(event.tfinger.y * h);
            isClick = true; 
            inputX = (int)((rawX - viewX) / scale); 
            inputY = (int)((rawY - viewY + kbOffsetY) / scale);
        }

        if (isClick) {
            SDL_Point clickPoint = { inputX, inputY };
            bool actionHandled = false;

            // --- KEYBOARD TRIGGERING ---
            if (state == AppState::NAME_INPUT) {
                SDL_Rect inputBox = {LOGICAL_WIDTH/2 - 200, LOGICAL_HEIGHT/2, 400, 60};
                if (SDL_PointInRect(&clickPoint, &inputBox)) {
                    isTextBoxFocused = true;
                    SDL_StartTextInput(); 
                } else {
                    isTextBoxFocused = false;
                    SDL_StopTextInput();  
                }
            } else if (state == AppState::JOIN_INPUT) {
                SDL_Rect inputBox = {LOGICAL_WIDTH/2 - 100, LOGICAL_HEIGHT/2, 200, 60};
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
                    SDL_Rect pileRect = { LOGICAL_WIDTH / 2 - 75, LOGICAL_HEIGHT / 2 - 100, 150, 200 };
                    if (SDL_PointInRect(&clickPoint, &pileRect) && selectedCardIndex != -1) {
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

    // --- AGGRESSIVE KEYBOARD FAILSAFE ---
    // Runs at the end of the frame to catch any state changes caused by buttons
    if (state != AppState::NAME_INPUT && state != AppState::JOIN_INPUT) {
        if (isTextBoxFocused) {
            isTextBoxFocused = false;
            SDL_StopTextInput();
        }
    }
}

void GameWindow::render(float dt, float turnProgress, AppState state, Match* match, int myIndex, const std::string& myName, const std::string& lobbyCode, const std::vector<PlayerInfo>& lobbyPlayers, const std::vector<PublicLobbyInfo>& publicLobbies, const std::string& hostName, int targetScore, bool sortBySuit) {
    
    // --- UPDATED SAFE ZONE CALCULATION ---
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    float scaleX = (float)w / LOGICAL_WIDTH;
    float scaleY = (float)h / LOGICAL_HEIGHT;
    float scale = (std::min)(scaleX, scaleY);
    SDL_RenderSetScale(renderer, scale, scale);

    int virtualW = (int)(w / scale);
    int virtualH = (int)(h / scale);
    int safeX = (virtualW - LOGICAL_WIDTH) / 2;
    int safeY = (virtualH - LOGICAL_HEIGHT) / 2;

    int kbOffsetY = 0;
#ifdef __ANDROID__
    if ((state == AppState::NAME_INPUT || state == AppState::JOIN_INPUT) && isTextBoxFocused) {
        kbOffsetY = 250;
    }
#endif

    // --- 1. RENDER BACKGROUND (Full Bleed) ---
    SDL_RenderSetViewport(renderer, NULL); 
    
    if (tableBackground) {
        int texW, texH;
        SDL_QueryTexture(tableBackground, NULL, NULL, &texW, &texH);
        
        float screenAspect = (float)virtualW / virtualH;
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

        // Fills the scaled screen perfectly on both Desktop and Mobile
        SDL_Rect destRect = {0, -kbOffsetY, virtualW, virtualH};
        SDL_RenderCopy(renderer, tableBackground, &srcRect, &destRect);
    } else {
        SDL_SetRenderDrawColor(renderer, 35, 107, 43, 255); 
        SDL_RenderClear(renderer);
    }

    // --- 2. SET SAFE ZONE VIEWPORT FOR UI ---
    // THE FIX: Do NOT multiply safeX or safeY by scale here. SDL handles it internally now!
    SDL_Rect safeZone = { safeX, safeY - kbOffsetY, LOGICAL_WIDTH, LOGICAL_HEIGHT };
    SDL_RenderSetViewport(renderer, &safeZone);

    activeButtons.clear(); 
    handHitboxes.clear();

    // ... (Keep the rest of your UI render states exactly as they are) ...

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color black = {0, 0, 0, 255};
    SDL_Color darkGray = {50, 50, 50, 255};
    SDL_Color blue = {50, 100, 200, 255};
    SDL_Color red = {200, 50, 50, 255};

    // --- REMAINDER OF RENDER LOGIC REMAINS IDENTICAL ---
    if (state == AppState::NAME_INPUT) {
        renderText("WELCOME TO AGONIA", LOGICAL_WIDTH/2 - 190, LOGICAL_HEIGHT/2 - 150, white, false, 0, 1.5f);
        renderText("Enter your player name:", LOGICAL_WIDTH/2 - 180, LOGICAL_HEIGHT/2 - 50, white);
        
        SDL_Rect inputBox = {LOGICAL_WIDTH/2 - 200, LOGICAL_HEIGHT/2, 400, 60};
        drawPixelUIBox(inputBox.x, inputBox.y, inputBox.w, inputBox.h, {200, 200, 200, 255}, false);
        renderText(currentTextInput + "_", LOGICAL_WIDTH/2 - 180, LOGICAL_HEIGHT/2 + 10, black);
    } 
    else if (state == AppState::MAIN_MENU) {
        renderText("ONLINE MENU", LOGICAL_WIDTH/2 - 120, LOGICAL_HEIGHT/2 - 200, white);
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - 200, LOGICAL_HEIGHT/2 - 100, 400, 60}, "1. Create Public Lobby", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(1); } });
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - 200, LOGICAL_HEIGHT/2 - 20, 400, 60}, "2. Create Private Lobby", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(2); } });
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - 200, LOGICAL_HEIGHT/2 + 60, 400, 60}, "3. Join Lobby by Code", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(3); } });
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - 200, LOGICAL_HEIGHT/2 + 140, 400, 60}, "4. Find Public Games", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(4); } });
    }
    else if (state == AppState::FIND_LOBBY) {
        renderText("PUBLIC LOBBIES", LOGICAL_WIDTH/2 - 150, 100, white);
        
        if (publicLobbies.empty()) {
            renderText("No public games available right now.", LOGICAL_WIDTH/2 - 220, 250, white);
        } else {
            int yOff = 200;
            for (const auto& lob : publicLobbies) {
                std::string btnText = "Join " + lob.hostName + " (" + std::to_string(lob.playerCount) + "/4)";
                std::string joinCode = lob.code;
                activeButtons.push_back({ {LOGICAL_WIDTH/2 - 250, yOff, 500, 60}, btnText, blue, [this, joinCode]() { 
                    if(onJoinCodeEntered) onJoinCodeEntered(joinCode); 
                }});
                yOff += 80;
            }
        }
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - 100, LOGICAL_HEIGHT - 150, 200, 60}, "Back", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(-1); } });
    }
    else if (state == AppState::JOIN_INPUT) {
        renderText("Enter 4-Character Code:", LOGICAL_WIDTH/2 - 190, LOGICAL_HEIGHT/2 - 50, white);
        SDL_Rect inputBox = {LOGICAL_WIDTH/2 - 100, LOGICAL_HEIGHT/2, 200, 60};
        drawPixelUIBox(inputBox.x, inputBox.y, inputBox.w, inputBox.h, {200, 200, 200, 255}, false);
        renderText(currentTextInput + "_", LOGICAL_WIDTH/2 - 80, LOGICAL_HEIGHT/2 + 10, black);
        
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - 100, LOGICAL_HEIGHT/2 + 150, 200, 60}, "Back", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(-1); } });
    }
    else if (state == AppState::LOBBY) {
        bool isHost = (myName == hostName); 
        renderText("--- LOBBY [" + lobbyCode + "] ---", LOGICAL_WIDTH/2 - 180, 100, white);
        
        int yOffset = 200;
        for (const auto& p : lobbyPlayers) {
            std::string label = "- " + p.name;
            if (p.name == hostName) label += "<host>"; 
            if (p.isBot) label += " (BOT)";
            
            renderText(label, LOGICAL_WIDTH/2 - 150, yOffset, white);
            
            if (isHost && p.name != hostName) {
                std::string target = p.name;
                activeButtons.push_back({ {LOGICAL_WIDTH/2 + 200, yOffset - 5, 80, 40}, "Kick", red, [this, target]() { 
                    if(onKickPlayerClicked) onKickPlayerClicked(target); 
                }});
            }
            yOffset += 50;
        }

        if (isHost) {
            activeButtons.push_back({ {LOGICAL_WIDTH/2 - 330, LOGICAL_HEIGHT - 200, 200, 60}, "Target: " + std::to_string(targetScore), darkGray, onToggleScoreClicked });
            activeButtons.push_back({ {LOGICAL_WIDTH/2 - 100, LOGICAL_HEIGHT - 200, 200, 60}, "Fill Bots", darkGray, onFillBotsClicked });
            activeButtons.push_back({ {LOGICAL_WIDTH/2 + 130, LOGICAL_HEIGHT - 200, 200, 60}, "Start Game", blue, onStartGameClicked });
        } else {
            renderText("Target Score: " + std::to_string(targetScore), LOGICAL_WIDTH/2 - 120, LOGICAL_HEIGHT - 260, white);
            renderText("Waiting for host to start...", LOGICAL_WIDTH/2 - 190, LOGICAL_HEIGHT - 200, white);
        }
        
        activeButtons.push_back({ {50, LOGICAL_HEIGHT - 120, 150, 60}, "Exit", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(-1); } });
    }
    else if (state == AppState::PLAYING && match) {
        bool isMyTurn = (match->getCurrentPlayerIndex() == myIndex);
        int standardCardW = 126;
        int standardCardH = 180;

        if (match->getCurrentPlayerIndex() < match->getPlayers().size()) {
            std::string currentPlayerName = match->getPlayers()[match->getCurrentPlayerIndex()].getName();
            renderText("Current Turn: " + currentPlayerName, 50, 50, white);
        }

        Card topCard = match->getTopCard();
        renderCard(topCard, LOGICAL_WIDTH / 2 - standardCardW / 2, LOGICAL_HEIGHT / 2 - standardCardH / 2, standardCardW, standardCardH);

        std::string suitText = match->getDeclaredSuit();
        if (!suitText.empty()) renderText("ACTIVE SUIT: " + suitText, LOGICAL_WIDTH / 2 - 120, LOGICAL_HEIGHT / 2 + 120, white);

        if (match->getCardsToDraw() > 0) {
            renderText("PENALTY: Draw " + std::to_string(match->getCardsToDraw()), LOGICAL_WIDTH / 2 - 120, LOGICAL_HEIGHT / 2 - 180, red);
        }

        activeButtons.push_back({ {LOGICAL_WIDTH/2 - 250, LOGICAL_HEIGHT/2 - standardCardH/2, standardCardW, standardCardH}, "DECK", {100, 20, 20, 255}, onDrawClicked });
        activeButtons.push_back({ {LOGICAL_WIDTH/2 + 150, LOGICAL_HEIGHT/2 - 30, 120, 60}, "PASS", darkGray, onPassClicked });
        activeButtons.push_back({ {LOGICAL_WIDTH - 160, LOGICAL_HEIGHT - 100, 150, 60}, sortBySuit ? "Sort: Suit" : "Sort: Value", darkGray, onSortClicked });

        if (myIndex >= 0 && myIndex < match->getPlayers().size()) {
            const auto& hand = match->getPlayers()[myIndex].getHand();
            
            int maxHandWidth = LOGICAL_WIDTH - 200; 
            int normalSpacing = 80;
            int totalSpacingNeeded = hand.size() * normalSpacing;
            int spacing = (totalSpacingNeeded > maxHandWidth) ? (maxHandWidth / (int)hand.size()) : normalSpacing;
            
            int startX = (LOGICAL_WIDTH - (hand.size() * spacing)) / 2;
            int baseY = LOGICAL_HEIGHT - standardCardH - 50;

            for (size_t i = 0; i < hand.size(); ++i) {
                int drawY = baseY;
                bool isSelected = (i == selectedCardIndex);
                if (isSelected && isMyTurn) drawY -= 30; 

                renderCard(hand[i], startX + (int)(i * spacing), drawY, standardCardW, standardCardH, false, isSelected, !isMyTurn);
                handHitboxes.push_back({ startX + (int)(i * spacing), drawY, standardCardW, standardCardH });
            }
        }

        int numPlayers = match->getPlayers().size();
        for (int i = 0; i < numPlayers; ++i) {
            if (i == myIndex) continue;

            int relativePos = (i - myIndex + numPlayers) % numPlayers;
            int cardCount = match->getPlayers()[i].getHand().size();
            
            int oppCardW = 100, oppCardH = 150; 
            int spacing = 30; 

            bool isTop = (numPlayers == 2 && relativePos == 1) || (numPlayers == 4 && relativePos == 2);
            bool isLeft = (numPlayers >= 3 && relativePos == 1);
            bool isRight = (numPlayers == 3 && relativePos == 2) || (numPlayers == 4 && relativePos == 3);

            if (isTop) {
                int totalWidth = (cardCount > 0) ? ((cardCount - 1) * spacing + oppCardW) : 0;
                int startX = (LOGICAL_WIDTH - totalWidth) / 2;
                for (int c = 0; c < cardCount; ++c) {
                    renderCardBack(startX + (c * spacing), 50, oppCardW, oppCardH, false);
                }
                renderText(match->getPlayers()[i].getName(), startX, 20, white);
            } 
            else if (isLeft) {
                int totalHeight = (cardCount > 0) ? ((cardCount - 1) * spacing + oppCardW) : 0;
                int startY = (LOGICAL_HEIGHT - totalHeight) / 2;
                for (int c = 0; c < cardCount; ++c) {
                    renderCardBack(50, startY + (c * spacing), oppCardW, oppCardH, true); 
                }
                renderText(match->getPlayers()[i].getName(), 50, startY - 40, white);
            } 
            else if (isRight) {
                int totalHeight = (cardCount > 0) ? ((cardCount - 1) * spacing + oppCardW) : 0;
                int startY = (LOGICAL_HEIGHT - totalHeight) / 2;
                for (int c = 0; c < cardCount; ++c) {
                    renderCardBack(LOGICAL_WIDTH - oppCardH - 50, startY + (c * spacing), oppCardW, oppCardH, true);
                }
                renderText(match->getPlayers()[i].getName(), LOGICAL_WIDTH - oppCardH - 50, startY - 40, white);
            }
        }

        for (auto it = activeAnimations.begin(); it != activeAnimations.end(); ) {
            it->progress += it->speed * dt;
            
            if (it->progress >= 1.0f) {
                it = activeAnimations.erase(it);
            } else {
                float currentX = it->startX + (it->targetX - it->startX) * it->progress;
                float currentY = it->startY + (it->targetY - it->startY) * it->progress;
                
                if (it->card.getSuit() == "Hidden") {
                    renderCardBack((int)currentX, (int)currentY, standardCardW, standardCardH, false);
                } else {
                    renderCard(it->card, (int)currentX, (int)currentY, standardCardW, standardCardH, false, false, false);
                }
                ++it;
            }
        }

        if (isMyTurn) {
            int boxW = 300, boxH = 50; 
            int boxX = (LOGICAL_WIDTH - boxW) / 2;
            int boxY = LOGICAL_HEIGHT / 2 - 240; 
            
            Uint32 pulse = (SDL_GetTicks() / 800) % 2; 
            SDL_Color boxColor = pulse ? SDL_Color{60, 120, 210, 255} : SDL_Color{40, 90, 170, 255}; 
            drawPixelUIBox(boxX, boxY, boxW, boxH, boxColor, false);
            renderText("YOUR TURN", boxX, boxY + 8, white, true, boxW, 1.0f);
        } else {
            std::string turnText = "Waiting for Player " + std::to_string(match->getCurrentPlayerIndex()) + "...";
            renderText(turnText, LOGICAL_WIDTH / 2 - 150, LOGICAL_HEIGHT / 2 - 220, white); 
        }

        if (needsSuitSelection) {
            SDL_Rect overlay = {0, 0, LOGICAL_WIDTH, LOGICAL_HEIGHT};
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(renderer, &overlay);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

            renderText("CHOOSE A SUIT", LOGICAL_WIDTH/2 - 130, LOGICAL_HEIGHT/2 - 150, white);
            auto pickSuit = [this](std::string s) { needsSuitSelection = false; if(onSuitSelected) onSuitSelected(s); };

            activeButtons.push_back({ {LOGICAL_WIDTH/2 - 220, LOGICAL_HEIGHT/2 - 50, 200, 80}, "Hearts", red, [=](){pickSuit("Hearts");} });
            activeButtons.push_back({ {LOGICAL_WIDTH/2 + 20, LOGICAL_HEIGHT/2 - 50, 200, 80}, "Diamonds", red, [=](){pickSuit("Diamonds");} });
            activeButtons.push_back({ {LOGICAL_WIDTH/2 - 220, LOGICAL_HEIGHT/2 + 50, 200, 80}, "Spades", darkGray, [=](){pickSuit("Spades");} });
            activeButtons.push_back({ {LOGICAL_WIDTH/2 + 20, LOGICAL_HEIGHT/2 + 50, 200, 80}, "Clubs", darkGray, [=](){pickSuit("Clubs");} });
        }

        if (isMyTurn) {
            SDL_Rect timerBg = { LOGICAL_WIDTH / 2 - 150, LOGICAL_HEIGHT / 2 - 175, 300, 14 }; 
            drawPixelUIBox(timerBg.x, timerBg.y, timerBg.w, timerBg.h, {0, 0, 0, 255}, false);
            
            int currentWidth = (int)(292 * turnProgress);
            SDL_Rect timerFill = { LOGICAL_WIDTH / 2 - 146, LOGICAL_HEIGHT / 2 - 171, currentWidth, 6 }; 
            
            SDL_Color timerColor = {50, 255, 50, 255}; 
            if (turnProgress < 0.4f) timerColor = {255, 200, 50, 255}; 
            if (turnProgress < 0.15f) timerColor = {255, 50, 50, 255}; 

            SDL_SetRenderDrawColor(renderer, timerColor.r, timerColor.g, timerColor.b, timerColor.a);
            SDL_RenderFillRect(renderer, &timerFill);
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
    renderText(btn.text, btn.rect.x + 15 + textOffset, btn.rect.y + 12 + textOffset, {255, 255, 255, 255});
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