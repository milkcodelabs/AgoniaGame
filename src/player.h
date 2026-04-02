#pragma once
#include <string>
#include <vector>
#include "Card.h"

class Player {
private:
    std::string name;
    int turnOrder;
    std::string state; // "played", "passed", "hasn't played", "missed turn"
    int score;
    std::vector<Card> hand;
    bool hasDrawnThisTurn;

public:
    Player(std::string n, int order);
    int getIndex() const { return turnOrder; }
    void sortHand(bool bySuit);
    std::string getName() const;
    int getScore() const;
    void addPoints(int p);
    void clearHand() { hand.clear(); }
    std::string getState() const;
    void setState(const std::string& s);
    
    void drawCard(Card c);
    Card playCard(int index); // Removes and returns the card from hand
    const std::vector<Card>& getHand() const;
    
    bool getHasDrawnThisTurn() const;
    void setHasDrawnThisTurn(bool drawn);
    void passTurn();
    
    bool hasEmptyHand() const;
    int calculateHandPoints() const;
};