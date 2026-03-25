#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>
#include <string>
#include <vector>
#include <functional>
#include <map>

#include "Match.h" 
#include "LobbyManager.h" 


enum class AppState {
    NAME_INPUT,
    MAIN_MENU,
    JOIN_INPUT,
    FIND_LOBBY,      // New State for finding public games
    LOBBY,
    PLAYING,
    SUIT_SELECTION,  // Extracted as an explicit UI layer
    GAME_OVER
};

struct UIButton {
    SDL_Rect rect;
    std::string text;
    SDL_Color bgColor;
    std::function<void()> onClick;
};

// Simple struct to hold public lobby data for the UI
struct PublicLobbyInfo {
    std::string code;
    std::string hostName;
    int playerCount;
};

class GameWindow {
public:
    GameWindow(const std::string& title, int width, int height);
    ~GameWindow();

    bool init();
    bool isRunning() const { return running; }
    bool isLoadingNextRound = false; // UI Lock flag
    
    // myIndex added so the UI knows exactly who the local player is
    void processInput(AppState& state, Match* match, int myIndex);
    void render(AppState state, Match* match, int myIndex, const std::string& myName, const std::string& lobbyCode, const std::vector<PlayerInfo>& lobbyPlayers, const std::vector<PublicLobbyInfo>& publicLobbies, const std::string& hostName, int targetScore, bool sortBySuit);    // --- Callbacks ---
    std::function<void(std::string)> onKickPlayerClicked;
    std::function<void(std::string)> onNameEntered;
    std::function<void(int)> onMenuOptionSelected; 
    std::function<void(std::string)> onJoinCodeEntered;
    std::function<void()> onFillBotsClicked;
    std::function<void()> onStartGameClicked;
    std::function<void()> onToggleScoreClicked;
    std::function<void()> onSortClicked;
    
    std::function<void(int)> onCardPlayed; // Renamed to clarify intent
    std::function<void(std::string)> onSuitSelected; 
    std::function<void()> onDrawClicked;
    std::function<void()> onPassClicked;
    std::function<void()> onNextRoundClicked;

    void triggerSuitSelection() { needsSuitSelection = true; }
    
    // UI Notification System
    void triggerNotification(const std::string& msg) {
        notificationMessage = msg;
        notificationTimeout = SDL_GetTicks() + 3000; // 3 second timeout
    }

    // Clears the selected card (e.g., when a turn ends)
    void clearCardSelection() { selectedCardIndex = -1; }

private:
    void renderCard(const Card& card, int x, int y, int w, int h, bool isHidden = false, bool isSelected = false, bool isGrayedOut = false);
    void renderText(const std::string& text, int x, int y, SDL_Color color, bool centered = false, int rectWidth = 0);
    void renderButton(const UIButton& btn);
    std::string getSuitSymbol(const std::string& suit);

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;

    bool running = false;
    std::vector<UIButton> activeButtons;
    std::vector<SDL_Rect> handHitboxes;
    

    std::string currentTextInput = "";
    bool needsSuitSelection = false; 
    int selectedCardIndex = -1; // Tracks which card the user clicked once

    std::string notificationMessage = "";
    Uint32 notificationTimeout = 0;

    const int LOGICAL_WIDTH = 1920;
    const int LOGICAL_HEIGHT = 1080;

    void fillRoundedRect(int x, int y, int w, int h, int r, SDL_Color color);
    void renderCardBack(int x, int y, int w, int h, bool horizontal);
};