#pragma once
#include <vector>
#include "Card.h"

class Deck {
private:
    std::vector<Card> cards;

public:
    Deck(); 
    
    void shuffle(unsigned int seed);
    Card drawCard();
    void addCards(const std::vector<Card>& newCards);
    bool isEmpty() const;
    int cardsRemaining() const;
    std::vector<std::string> serialize() const;
    void loadFromSerialized(const std::vector<std::string>& data);
};