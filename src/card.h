#pragma once
#include <string>

class Card {
private:
    std::string value;
    std::string suit;
    int points;
    int calculatePoints(const std::string& val);

public:
    Card(std::string v, std::string s);
    
    std::string getValue() const;
    std::string getSuit() const;
    int getPoints() const;
    
    std::string toString() const; 
    std::string getId() const;    
};