#include "Card.h"

Card::Card(std::string v, std::string s) : value(v), suit(s) {
    points = calculatePoints(v);
}

int Card::calculatePoints(const std::string& val) {
    if (val == "A") return 11;
    if (val == "J" || val == "Q" || val == "K") return 10;
    return std::stoi(val); 
}

std::string Card::getValue() const { return value; }
std::string Card::getSuit() const { return suit; }
int Card::getPoints() const { return points; }

std::string Card::toString() const {
    return value + " of " + suit;
}

std::string Card::getId() const {
    return value + "_" + suit; 
}