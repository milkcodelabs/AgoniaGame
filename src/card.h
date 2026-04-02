#pragma once
#include <string>

class Card {
private:
    std::string value;
    std::string suit;
    int points;

    // Helper to calculate points based on game rules
    int calculatePoints(const std::string& val);

public:
    Card(std::string v, std::string s);
    
    std::string getValue() const;
    std::string getSuit() const;
    int getPoints() const;
    
    // Utilities for rendering and serialization
    std::string toString() const; 
    std::string getId() const;    
};