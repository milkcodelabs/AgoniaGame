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
    std::vector<Card> takeDiscardPile(); 
};