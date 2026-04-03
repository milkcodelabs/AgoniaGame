#include "Player.h"
#include <algorithm>

// Helper functions for sorting
int getCardValueInt(const std::string& v) {
    if (v == "J") return 11;
    if (v == "Q") return 12;
    if (v == "K") return 13;
    if (v == "A") return 14;
    try { return std::stoi(v); } catch (...) { return 0; }
}

int getSuitInt(const std::string& s) {
    if (s == "Hearts") return 1;
    if (s == "Diamonds") return 2;
    if (s == "Spades") return 3;
    return 4; // Clubs
}

void Player::sortHand(bool bySuit) {
    std::sort(hand.begin(), hand.end(), [bySuit](const Card& a, const Card& b) {
        if (bySuit) {
            if (a.getSuit() != b.getSuit()) return getSuitInt(a.getSuit()) < getSuitInt(b.getSuit());
            return getCardValueInt(a.getValue()) < getCardValueInt(b.getValue());
        } else {
            if (a.getValue() != b.getValue()) return getCardValueInt(a.getValue()) < getCardValueInt(b.getValue());
            return getSuitInt(a.getSuit()) < getSuitInt(b.getSuit());
        }
    });
}

Player::Player(std::string n, int order) 
    : name(n), turnOrder(order), state("hasn't played"), score(0), hasDrawnThisTurn(false) {}

std::string Player::getName() const { return name; }
int Player::getScore() const { return score; }
void Player::addPoints(int p) { score += p; }

std::string Player::getState() const { return state; }
void Player::setState(const std::string& s) { state = s; }

void Player::drawCard(Card c) {
    hand.push_back(c);
    hasDrawnThisTurn = true;
}

Card Player::playCard(int index) {
    Card played = hand[index];
    hand.erase(hand.begin() + index);
    state = "played";
    return played;
}

const std::vector<Card>& Player::getHand() const { return hand; }

bool Player::getHasDrawnThisTurn() const { return hasDrawnThisTurn; }
void Player::setHasDrawnThisTurn(bool drawn) { hasDrawnThisTurn = drawn; }

void Player::passTurn() {
    state = "passed";
    hasDrawnThisTurn = false;
}

bool Player::hasEmptyHand() const { return hand.empty(); }

int Player::calculateHandPoints() const {
    int total = 0;
    for (const Card& c : hand) {
        total += c.getPoints();
    }
    return total;
}