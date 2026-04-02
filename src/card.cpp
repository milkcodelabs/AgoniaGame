#include "Card.h"

Card::Card(std::string v, std::string s) : value(v), suit(s) {
    points = calculatePoints(v);
}

int Card::calculatePoints(const std::string& val) {
    // Aces are 11 points [cite: 38]
    if (val == "A") return 11;
    // Face cards are 10 points [cite: 38]
    if (val == "J" || val == "Q" || val == "K") return 10;
    // Number cards (2-10) are worth their face value [cite: 37]
    return std::stoi(val); 
}

std::string Card::getValue() const { return value; }
std::string Card::getSuit() const { return suit; }
int Card::getPoints() const { return points; }

std::string Card::toString() const {
    return value + " of " + suit;
}

std::string Card::getId() const {
    // Creates our Firebase-friendly ID, e.g., "7_Hearts"
    return value + "_" + suit; 
}