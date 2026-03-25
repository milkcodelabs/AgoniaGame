#include "Deck.h"
#include <algorithm>
#include <random>
#include <chrono>
#include <stdexcept>
#include <random>

Deck::Deck() {
    std::vector<std::string> suits = {"Hearts", "Diamonds", "Clubs", "Spades"}; // 
    std::vector<std::string> values = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};

    for (const auto& suit : suits) {
        for (const auto& val : values) {
            cards.push_back(Card(val, suit));
        }
    }
}

void Deck::shuffle(unsigned int seed) {
    std::mt19937 rng(seed);
    std::shuffle(cards.begin(), cards.end(), rng);
}

Card Deck::drawCard() {
    if (cards.empty()) {
        throw std::out_of_range("Cannot draw from an empty deck!");
    }
    Card drawnCard = cards.back();
    cards.pop_back();
    return drawnCard;
}

void Deck::addCards(const std::vector<Card>& newCards) {
    cards.insert(cards.end(), newCards.begin(), newCards.end());
}

bool Deck::isEmpty() const { return cards.empty(); }

int Deck::cardsRemaining() const { return cards.size(); }
std::vector<std::string> Deck::serialize() const {
    std::vector<std::string> res;
    for (const auto& c : cards) {
        // Formats as "Value_Suit"
        res.push_back(c.getValue() + "_" + c.getSuit());
    }
    return res;
}

void Deck::loadFromSerialized(const std::vector<std::string>& data) {
    cards.clear();
    for (const auto& s : data) {
        size_t delim = s.find('_');
        if (delim != std::string::npos) {
            cards.push_back(Card(s.substr(0, delim), s.substr(delim + 1)));
        }
    }
}