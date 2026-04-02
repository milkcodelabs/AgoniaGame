#pragma once
#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include "Player.h"
#include "Deck.h"
#include "Table.h"

class Match {
private:
    std::vector<Player> players;
    Deck deck;
    Table* table;
    
    std::atomic<int> currentPlayerIndex;
    std::string declaredSuit;
    int cardsToDraw; 
    bool matchOver;
    
    void safeDraw(Player& p);
    void reshuffleDiscardPile();
    unsigned int matchSeed = 0;

public:
    Match(std::vector<std::string> playerNames);
    ~Match();
    
    void dealInitialCards();
    void advanceTurn(int steps = 1);
    Deck& getDeck() { return deck; }
    
    bool isValidMove(const Card& c) const;
    std::string getInvalidReason(const Card& c) const;
    
    // Core Actions for the Console to call
    bool attemptPlayCard(int cardIndex, std::string newSuit = "");
    bool attemptDrawPenalty(); // For when a player is hit by 7s
    bool attemptDraw();
    bool attemptPass();
    
    // Getters for Console UI
    Player& getCurrentPlayer();
    int getCurrentPlayerIndex() const { return currentPlayerIndex.load(); }
    Card getTopCard() const;
    std::string getDeclaredSuit() const;
    int getCardsToDraw() const;
    bool isMatchOver() const;
    const std::vector<Player>& getPlayers() const;
    
    void endMatchPointsCalc();
    Player& getPlayer(int index) { return players[index]; }
    // --- NEW: Post-Match Loop & Deterministic RNG ---
    void setSeed(unsigned int seed) { matchSeed = seed; }
    void resetForNextRound(unsigned int newSeed);

    std::function<void(int playerIndex, Card card)> onCardPlayedEvent;
    std::function<void(int playerIndex)> onCardDrawnEvent;
};