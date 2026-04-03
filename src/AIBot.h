#pragma once
#include "Match.h"
#include <random>
#include <string>

enum class Difficulty { BEGINNER, INTERMEDIATE, EXPERT };

struct BotMoveData {
    std::string type; // "D" for draw, "P" for pass, or the string representation of the card index
    int cardIndex;    // -1 if D or P
    std::string suit; // "" unless an Ace was played
};

class AIBot {
private:
    int playerIndex;
    Difficulty difficulty;
    int skillLevel; // Range: 0 to 100

    int findBestMove(Match& match, const std::vector<Card>& hand);

public:
    AIBot(int index, Difficulty diff);
        BotMoveData takeTurn(Match& match); 
};