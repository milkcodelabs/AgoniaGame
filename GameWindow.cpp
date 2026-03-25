#include "GameWindow.h"
#include <iostream>

GameWindow::GameWindow(const std::string& title, int width, int height) {
    window = nullptr;
    renderer = nullptr;
    font = nullptr;
    running = false;
    needsSuitSelection = false;
    currentTextInput = "";
    selectedCardIndex = -1;
    isLoadingNextRound = false; 
}

GameWindow::~GameWindow() {
    if (font) TTF_CloseFont(font);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    Mix_CloseAudio();
    TTF_Quit();
    SDL_Quit();
}

bool GameWindow::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) return false;
    if (TTF_Init() == -1) return false;

    window = SDL_CreateWindow("Agonia", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) return false;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) return false;

    SDL_RenderSetLogicalSize(renderer, LOGICAL_WIDTH, LOGICAL_HEIGHT);

    font = TTF_OpenFont("arial.ttf", 36); 
    if (!font) {
        std::cerr << "Warning: Failed to load arial.ttf.\n";
    }
    // --- AUDIO INITIALIZATION ---
    // 44100Hz frequency, standard format, 2 channels (stereo), 2048 chunk size
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "Warning: SDL_mixer could not initialize! Error: " << Mix_GetError() << "\n";
    }

    SDL_StartTextInput();
    running = true;
    return true;
}

void GameWindow::processInput(AppState& state, Match* match, int myIndex) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) running = false;
        
        // --- TEXT INPUT HANDLING ---
        if (state == AppState::NAME_INPUT || state == AppState::JOIN_INPUT) {
            if (event.type == SDL_TEXTINPUT) {
                if (currentTextInput.length() < 15) currentTextInput += event.text.text;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_BACKSPACE && currentTextInput.length() > 0) {
                    currentTextInput.pop_back();
                } else if (event.key.keysym.sym == SDLK_RETURN && currentTextInput.length() > 0) {
                    if (state == AppState::NAME_INPUT && onNameEntered) {
                        std::string newTitle = "Agonia user:" + currentTextInput;
                        SDL_SetWindowTitle(window, newTitle.c_str());
                        
                        onNameEntered(currentTextInput);
                        currentTextInput = "";
                    } else if (state == AppState::JOIN_INPUT && onJoinCodeEntered) {
                        onJoinCodeEntered(currentTextInput);
                        currentTextInput = "";
                    }
                }
            }
        }

        // --- MOUSE/TOUCH HANDLING ---
        bool isClick = false;
        int inputX = 0, inputY = 0;

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            isClick = true; inputX = event.button.x; inputY = event.button.y;
        } else if (event.type == SDL_FINGERDOWN) {
            isClick = true; inputX = event.tfinger.x * LOGICAL_WIDTH; inputY = event.tfinger.y * LOGICAL_HEIGHT;
        }

        if (isClick) {
            SDL_Point clickPoint = { inputX, inputY };
            bool actionHandled = false;
            
            // 1. Check UI Buttons
            for (const auto& btn : activeButtons) {
                if (SDL_PointInRect(&clickPoint, &btn.rect)) {
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
                        // Loop backward so visually top cards are clicked first
                        for (int i = (int)handHitboxes.size() - 1; i >= 0; --i) {
                            if (SDL_PointInRect(&clickPoint, &handHitboxes[i])) {
                                if (selectedCardIndex == i) {
                                    if (onCardPlayed) onCardPlayed(i);
                                    selectedCardIndex = -1; 
                                } else {
                                    selectedCardIndex = i;
                                }
                                break; 
                            }
                        }
                    }
                }
            }
        }
    }
}

void GameWindow::render(AppState state, Match* match, int myIndex, const std::string& myName, const std::string& lobbyCode, const std::vector<PlayerInfo>& lobbyPlayers, const std::vector<PublicLobbyInfo>& publicLobbies, const std::string& hostName, int targetScore, bool sortBySuit) {
    SDL_SetRenderDrawColor(renderer, 35, 107, 43, 255); 
    SDL_RenderClear(renderer);

    activeButtons.clear(); 
    handHitboxes.clear();

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color black = {0, 0, 0, 255};
    SDL_Color darkGray = {50, 50, 50, 255};
    SDL_Color blue = {50, 100, 200, 255};
    SDL_Color red = {200, 50, 50, 255};

    if (state == AppState::NAME_INPUT) {
        renderText("WELCOME TO AGONIA", LOGICAL_WIDTH/2 - 190, LOGICAL_HEIGHT/2 - 150, white);
        renderText("Enter your player name:", LOGICAL_WIDTH/2 - 180, LOGICAL_HEIGHT/2 - 50, white);
        
        SDL_Rect inputBox = {LOGICAL_WIDTH/2 - 200, LOGICAL_HEIGHT/2, 400, 60};
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &inputBox);
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
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &inputBox);
        renderText(currentTextInput + "_", LOGICAL_WIDTH/2 - 80, LOGICAL_HEIGHT/2 + 10, black);
        
        activeButtons.push_back({ {LOGICAL_WIDTH/2 - 100, LOGICAL_HEIGHT/2 + 150, 200, 60}, "Back", darkGray, [this]() { if(onMenuOptionSelected) onMenuOptionSelected(-1); } });
    }
    else if (state == AppState::LOBBY) {
        bool isHost = (myName == hostName); 
        renderText("--- LOBBY [" + lobbyCode + "] ---", LOGICAL_WIDTH/2 - 180, 100, white);
        
        int yOffset = 200;
        for (const auto& p : lobbyPlayers) {
            std::string label = "- " + p.name;
            if (p.name == hostName) label += "<host>"; // Star symbol
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

        if (match->getCurrentPlayerIndex() < match->getPlayers().size()) {
            std::string currentPlayerName = match->getPlayers()[match->getCurrentPlayerIndex()].getName();
            renderText("Current Turn: " + currentPlayerName, 50, 50, white);
        }

        Card topCard = match->getTopCard();
        renderCard(topCard, LOGICAL_WIDTH / 2 - 75, LOGICAL_HEIGHT / 2 - 100, 150, 200);

        std::string turnText = isMyTurn ? ">>> YOUR TURN <<<" : "Waiting for Player " + std::to_string(match->getCurrentPlayerIndex()) + "...";
        renderText(turnText, LOGICAL_WIDTH / 2 - 150, LOGICAL_HEIGHT / 2 - 150, isMyTurn ? blue : white);

        std::string suitText = match->getDeclaredSuit();
        if (!suitText.empty()) renderText("ACTIVE SUIT: " + suitText, LOGICAL_WIDTH / 2 - 120, LOGICAL_HEIGHT / 2 + 120, white);

        if (match->getCardsToDraw() > 0) {
            renderText("PENALTY: Draw " + std::to_string(match->getCardsToDraw()), LOGICAL_WIDTH / 2 - 120, LOGICAL_HEIGHT / 2 - 180, red);
        }

        activeButtons.push_back({ {LOGICAL_WIDTH/2 - 300, LOGICAL_HEIGHT/2 - 100, 150, 200}, "DECK", {100, 20, 20, 255}, onDrawClicked });
        activeButtons.push_back({ {LOGICAL_WIDTH/2 + 150, LOGICAL_HEIGHT/2 - 20, 120, 60}, "PASS", darkGray, onPassClicked });

        // Sorting Toggle Button
        activeButtons.push_back({ {LOGICAL_WIDTH - 200, LOGICAL_HEIGHT - 100, 150, 60}, sortBySuit ? "Sort: Suit" : "Sort: Value", darkGray, onSortClicked });

        // Render Local Hand
        if (myIndex >= 0 && myIndex < match->getPlayers().size()) {
            const auto& hand = match->getPlayers()[myIndex].getHand();
            int cardWidth = 120, cardHeight = 180, spacing = 60;
            int startX = (LOGICAL_WIDTH - (hand.size() * spacing)) / 2;
            int baseY = LOGICAL_HEIGHT - cardHeight - 50;

            for (size_t i = 0; i < hand.size(); ++i) {
                int drawY = baseY;
                bool isSelected = (i == selectedCardIndex);
                if (isSelected && isMyTurn) drawY -= 30; 

                renderCard(hand[i], startX + (int)(i * spacing), drawY, cardWidth, cardHeight, false, isSelected, !isMyTurn);
                handHitboxes.push_back({ startX + (int)(i * spacing), drawY, cardWidth, cardHeight });
            }
        }

        // Render Opponents
        int numPlayers = match->getPlayers().size();
        for (int i = 0; i < numPlayers; ++i) {
            if (i == myIndex) continue;

            int relativePos = (i - myIndex + numPlayers) % numPlayers;
            int cardCount = match->getPlayers()[i].getHand().size();
            
            int cW = 120, cH = 180; 
            int spacing = 30; 

            bool isTop = (numPlayers == 2 && relativePos == 1) || (numPlayers == 4 && relativePos == 2);
            bool isLeft = (numPlayers >= 3 && relativePos == 1);
            bool isRight = (numPlayers == 3 && relativePos == 2) || (numPlayers == 4 && relativePos == 3);

            if (isTop) {
                int totalWidth = cardCount * spacing + (cW - spacing);
                int startX = (LOGICAL_WIDTH - totalWidth) / 2;
                for (int c = 0; c < cardCount; ++c) {
                    renderCardBack(startX + (c * spacing), 50, cW, cH, false);
                }
                renderText(match->getPlayers()[i].getName(), startX, 20, white);
            } 
            else if (isLeft) {
                int totalHeight = cardCount * spacing + (cW - spacing);
                int startY = (LOGICAL_HEIGHT - totalHeight) / 2;
                for (int c = 0; c < cardCount; ++c) {
                    renderCardBack(50, startY + (c * spacing), cH, cW, true); 
                }
                renderText(match->getPlayers()[i].getName(), 50, startY - 40, white);
            } 
            else if (isRight) {
                int totalHeight = cardCount * spacing + (cW - spacing);
                int startY = (LOGICAL_HEIGHT - totalHeight) / 2;
                for (int c = 0; c < cardCount; ++c) {
                    renderCardBack(LOGICAL_WIDTH - cH - 50, startY + (c * spacing), cH, cW, true);
                }
                renderText(match->getPlayers()[i].getName(), LOGICAL_WIDTH - cH - 50, startY - 40, white);
            }
        }

        if (needsSuitSelection) {
            SDL_Rect overlay = {0, 0, LOGICAL_WIDTH, LOGICAL_HEIGHT};
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(renderer, &overlay);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

            renderText("CHOOSE A SUIT", LOGICAL_WIDTH/2 - 130, LOGICAL_HEIGHT/2 - 150, white);
            auto pickSuit = [this](std::string s) { needsSuitSelection = false; if(onSuitSelected) onSuitSelected(s); };

            activeButtons.push_back({ {LOGICAL_WIDTH/2 - 220, LOGICAL_HEIGHT/2 - 50, 200, 80}, "H Hearts", red, [=](){pickSuit("Hearts");} });
            activeButtons.push_back({ {LOGICAL_WIDTH/2 + 20, LOGICAL_HEIGHT/2 - 50, 200, 80}, "D Diamonds", red, [=](){pickSuit("Diamonds");} });
            activeButtons.push_back({ {LOGICAL_WIDTH/2 - 220, LOGICAL_HEIGHT/2 + 50, 200, 80}, "S Spades", darkGray, [=](){pickSuit("Spades");} });
            activeButtons.push_back({ {LOGICAL_WIDTH/2 + 20, LOGICAL_HEIGHT/2 + 50, 200, 80}, "C Clubs", darkGray, [=](){pickSuit("Clubs");} });
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

    if (notificationTimeout > SDL_GetTicks()) {
        SDL_Rect notifRect = { LOGICAL_WIDTH / 2 - 300, 150, 600, 60 };
        SDL_SetRenderDrawColor(renderer, 200, 50, 50, 230);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(renderer, &notifRect);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer, &notifRect);
        
        renderText(notificationMessage, LOGICAL_WIDTH / 2 - (notificationMessage.length() * 8), 160, white);
    }
    
    SDL_RenderPresent(renderer);
}

void GameWindow::renderButton(const UIButton& btn) {
    SDL_SetRenderDrawColor(renderer, btn.bgColor.r, btn.bgColor.g, btn.bgColor.b, btn.bgColor.a);
    SDL_RenderFillRect(renderer, &btn.rect);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawRect(renderer, &btn.rect);
    renderText(btn.text, btn.rect.x + 15, btn.rect.y + 12, {255, 255, 255, 255});
}

std::string GameWindow::getSuitSymbol(const std::string& suit) {
    if (suit == "Spades") return "S";
    if (suit == "Hearts") return "H";
    if (suit == "Diamonds") return "D";
    if (suit == "Clubs") return "C";
    return suit;
}

void GameWindow::renderCard(const Card& card, int x, int y, int w, int h, bool isHidden, bool isSelected, bool isGrayedOut) {
    SDL_Rect cardRect = { x, y, w, h };
    int radius = 10; 

    if (isSelected) {
        fillRoundedRect(x - 6, y - 6, w + 12, h + 12, radius + 2, {255, 215, 0, 255});
    }

    fillRoundedRect(x - 2, y - 2, w + 4, h + 4, radius + 2, {0, 0, 0, 255});
    fillRoundedRect(x, y, w, h, radius, {255, 255, 255, 255});

    if (!isHidden) {
        std::string suit = card.getSuit();
        SDL_Color textColor = (suit == "Hearts" || suit == "Diamonds") ? SDL_Color{200, 0, 0, 255} : SDL_Color{0, 0, 0, 255};

        std::string symbol = getSuitSymbol(suit);
        std::string displayTop = card.getValue() + " " + symbol;
        
        renderText(displayTop, x + 8, y + 8, textColor);
        renderText(symbol, x + (w / 2) - 15, y + (h / 2) - 20, textColor);
    }

    if (isGrayedOut) {
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 150);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(renderer, &cardRect);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
}

void GameWindow::renderText(const std::string& text, int x, int y, SDL_Color color, bool centered, int rectWidth) {
    if (!font || text.empty()) return;
    
    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), color);
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
            int finalX = centered ? x + (rectWidth - surface->w) / 2 : x;
            SDL_Rect destRect = { finalX, y, surface->w, surface->h };
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
    int radius = 10;
    fillRoundedRect(x - 2, y - 2, w + 4, h + 4, radius + 2, {0, 0, 0, 255});
    fillRoundedRect(x, y, w, h, radius, {40, 80, 150, 255});
    SDL_Rect inner = {x + 8, y + 8, w - 16, h - 16};
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100);
    SDL_RenderDrawRect(renderer, &inner);
}