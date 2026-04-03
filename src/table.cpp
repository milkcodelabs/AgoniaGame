#include "Table.h"

Table::Table(Card initialCard) : topCard(initialCard) {}

Card Table::getTopCard() const {
    return topCard;
}

void Table::addCard(Card c) {
    discardPile.push_back(topCard);
    topCard = c;
}

std::vector<Card> Table::takeDiscardPile() {
    std::vector<Card> recycledCards = discardPile;
    discardPile.clear();
    return recycledCards;
}