#pragma once
#include <vector>
#include "Card.h"

class Table {
private:
    Card topCard;
    std::vector<Card> discardPile;

public:
    Table(Card initialCard);
    
    Card getTopCard() const;
    void addCard(Card c);
    
    // Returns the pile (excluding the top card) to be reshuffled into the deck
    std::vector<Card> takeDiscardPile(); 
};