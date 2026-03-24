#include "Table.h"

Table::Table(Card initialCard) : topCard(initialCard) {
    // The initial card placed on the table [cite: 5] doesn't go into the discard pile yet.
}

Card Table::getTopCard() const {
    return topCard;
}

void Table::addCard(Card c) {
    // The old top card becomes part of the discard pile
    discardPile.push_back(topCard);
    // The newly played card becomes the new top card
    topCard = c;
}

std::vector<Card> Table::takeDiscardPile() {
    std::vector<Card> recycledCards = discardPile;
    discardPile.clear(); // Empty the pile
    return recycledCards;
}