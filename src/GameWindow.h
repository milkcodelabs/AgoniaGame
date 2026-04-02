#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>
#include <SDL_image.h> // Added for Kenney Sprite Sheet
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
    FIND_LOBBY,      // State for finding public games
    LOBBY,
    PLAYING,
    SUIT_SELECTION,  // Extracted as an explicit UI layer
    GAME_OVER
};

enum class Sfx {
    CLICK,
    START,
    ROUND_END,
    TOURNAMENT_END,
    SELECT,
    PLAY_CARD,
    SHUFFLE,
    DRAW,
    YOUR_TURN,
    PASS,
    FAIL
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

// --- NEW: Animation Struct for Delta Time Interpolation ---
struct CardAnim {
    Card card;
    float startX, startY;
    float targetX, targetY;
    float progress; // 0.0 to 1.0
    float speed = 2.0f; // Multiplier for dt
};

class GameWindow {
public:
    GameWindow(const std::string& title, int width, int height);
    ~GameWindow();

    bool init();
    bool isRunning() const { return running; }
    bool isLoadingNextRound = false; // UI Lock flag
    bool isTextBoxFocused = false;
    
    // myIndex added so the UI knows exactly who the local player is
    void processInput(AppState& state, Match* match, int myIndex);

    void render(float dt, float turnProgress, AppState state, Match* match, int myIndex, const std::string& myName, const std::string& lobbyCode, const std::vector<PlayerInfo>& lobbyPlayers, const std::vector<PublicLobbyInfo>& publicLobbies, const std::string& hostName, int targetScore, bool sortBySuit);
    
    // --- Callbacks ---
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
        playSound(Sfx::FAIL);
    }

    // Clears the selected card (e.g., when a turn ends)
    void clearCardSelection() { selectedCardIndex = -1; }

    // --- NEW: Public animation trigger ---
    void triggerAnimation(const Card& card, float startX, float startY, float targetX, float targetY) {
        activeAnimations.push_back({card, startX, startY, targetX, targetY, 0.0f, 3.0f});
    }
    void playSound(Sfx sound);
private:
    void renderCard(const Card& card, int x, int y, int w, int h, bool isHidden = false, bool isSelected = false, bool isGrayedOut = false);
    void renderText(const std::string& text, int x, int y, SDL_Color color, bool centered = false, int rectWidth = 0, float scale = 1.0f);
    void renderButton(const UIButton& btn);
    std::string getSuitSymbol(const std::string& suit);

    // --- NEW: Helper Functions for Sprites and Pixel UI ---
    SDL_Rect getCardSourceRect(const Card& card);
    void drawPixelUIBox(int x, int y, int w, int h, SDL_Color color, bool isHovered);

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    
    // --- NEW: Textures and Animation State ---
    SDL_Texture* cardSpriteSheet = nullptr;
    SDL_Texture* tableBackground = nullptr;
    std::vector<CardAnim> activeAnimations;
    int mouseX, mouseY; // Tracks mouse position for hover states

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
    std::map<Sfx, Mix_Chunk*> sounds;
};