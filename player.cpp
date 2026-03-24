#include "Player.h"

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
    hasDrawnThisTurn = false; // Reset for the next time it's their turn
}

bool Player::hasEmptyHand() const { return hand.empty(); }

int Player::calculateHandPoints() const {
    int total = 0;
    for (const Card& c : hand) {
        total += c.getPoints();
    }
    return total;
}