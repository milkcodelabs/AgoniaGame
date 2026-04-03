#include "AIBot.h"
#include <iostream>
#include <chrono>

AIBot::AIBot(int index, Difficulty diff) : playerIndex(index), difficulty(diff) {
    // Hardcoded outer settings
    if (difficulty == Difficulty::BEGINNER) {
        skillLevel = 0;
    } else if (difficulty == Difficulty::EXPERT) {
        skillLevel = 100;
    } else {
        // Randomized skill for Intermediate (e.g., between 40 and 80)
        unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
        std::mt19937 gen(seed);
        std::uniform_int_distribution<> distrib(40, 80);
        skillLevel = distrib(gen);
    }
}

int AIBot::findBestMove(Match& match, const std::vector<Card>& hand) {
    int bestIndex = -1;
    int highestScore = -999;

    for (size_t i = 0; i < hand.size(); ++i) {
        if (match.isValidMove(hand[i])) {
            int moveScore = hand[i].getPoints(); // Base score: shed high points
            std::string val = hand[i].getValue();

            // Strategic Adjustments
            if (val == "A") moveScore -= 20; // Hoard Aces for emergencies
            if (val == "8") moveScore -= 5;  // Hoard 8s for free turns
            if (val == "7") moveScore += 10; // Prioritize offensive 7s

            if (moveScore > highestScore) {
                highestScore = moveScore;
                bestIndex = static_cast<int>(i);
            }
        }
    }
    return bestIndex;
}

BotMoveData AIBot::takeTurn(Match& match) {
    Player& me = match.getCurrentPlayer();
    BotMoveData moveMade;
    moveMade.cardIndex = -1;
    moveMade.suit = "";
    
    // 1. Handle Penalty Draws automatically
    if (match.getCardsToDraw() > 0) {
        bool hasSeven = false;
        int sevenIndex = -1;
        for (size_t i = 0; i < me.getHand().size(); ++i) {
            if (me.getHand()[i].getValue() == "7") {
                hasSeven = true;
                sevenIndex = i;
                break;
            }
        }
        
        if (hasSeven) {
            match.attemptPlayCard(sevenIndex);
            std::cout << me.getName() << " countered with a 7!\n";
            moveMade.type = std::to_string(sevenIndex);
            moveMade.cardIndex = sevenIndex;
            return moveMade;
        } else {
            match.attemptDrawPenalty();
            std::cout << me.getName() << " ate the penalty and drew cards.\n";
            moveMade.type = "D";
            return moveMade;
        }
    }

    // 2. Determine Move based on Skill Level
    int moveIndex = -1;
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 gen(seed);
    std::uniform_int_distribution<> distrib(1, 100);
    
    if (distrib(gen) <= skillLevel) {
        moveIndex = findBestMove(match, me.getHand());
    } else {
        for (size_t i = 0; i < me.getHand().size(); ++i) {
            if (match.isValidMove(me.getHand()[i])) {
                moveIndex = static_cast<int>(i);
                break;
            }
        }
    }

    // 3. Execute the Action
    if (moveIndex != -1) {
        std::string newSuit = ""; 
        if (me.getHand()[moveIndex].getValue() == "A") {
            // Hardcoded to Hearts for speed
            newSuit = "Hearts"; 
            moveMade.suit = newSuit;
        }
        
        match.attemptPlayCard(moveIndex, newSuit);
        std::cout << me.getName() << " played a card.\n";
        
        moveMade.type = std::to_string(moveIndex);
        moveMade.cardIndex = moveIndex;
        return moveMade;
        
    } else {
        if (!me.getHasDrawnThisTurn()) {
            match.attemptDraw();
            std::cout << me.getName() << " drew a card.\n";
            moveMade.type = "D";
            return moveMade;
        } else {
            match.attemptPass();
            std::cout << me.getName() << " passed their turn.\n";
            moveMade.type = "P";
            return moveMade;
        }
    }
}