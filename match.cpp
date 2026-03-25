#include "Match.h"
#include <iostream>

Match::Match(std::vector<std::string> playerNames) {
    for (size_t i = 0; i < playerNames.size(); ++i) {
        players.push_back(Player(playerNames[i], i));
    }
    table = nullptr;
    currentPlayerIndex = 0;
    cardsToDraw = 0;
    matchOver = false;
}

Match::~Match() {
    delete table;
}

void Match::dealInitialCards() {
    // Deal 7 cards to each player
    for (int i = 0; i < 7; ++i) {
        for (Player& p : players) {
            safeDraw(p);
        }
    }

    for (Player& p : players) {
        p.setHasDrawnThisTurn(false);
    }

    // Flip first card to table
    table = new Table(deck.drawCard());
    declaredSuit = "";
}

void Match::reshuffleDiscardPile() {
    matchSeed += 1; // Increment so it's different from the initial shuffle
    Card topCard = table->getTopCard();
    std::vector<Card> recycled = table->takeDiscardPile();
    deck.addCards(recycled);
    
    // THE BUG FIX: Deterministic Reshuffle
    deck.shuffle(matchSeed); 
    std::cout << "[SYSTEM] Deck exhausted. Discard pile reshuffled with seed: " << matchSeed << "\n";
}

void Match::safeDraw(Player& p) {
    if (deck.isEmpty()) {
        reshuffleDiscardPile(); 
    }
    if (!deck.isEmpty()) {
        p.drawCard(deck.drawCard());
    }
}

void Match::advanceTurn(int steps) {
    players[currentPlayerIndex].setHasDrawnThisTurn(false);
    currentPlayerIndex = (currentPlayerIndex + steps) % players.size();
}

bool Match::isValidMove(const Card& c) const {
    Card top = table->getTopCard();
    
    // If caught in a 7s chain, you MUST play a 7
    if (cardsToDraw > 0) {
        return c.getValue() == "7";
    }
    
    // Cannot play an Ace on an Ace 
    if (c.getValue() == "A" && top.getValue() == "A") {
        return false;
    }
    
    // Ace can be played on anything else 
    if (c.getValue() == "A") return true;
    
    // If an Ace was previously played, match the declared suit 
    if (!declaredSuit.empty()) {
        return c.getSuit() == declaredSuit;
    }
    
    // Standard match by value or suit
    return (c.getValue() == top.getValue() || c.getSuit() == top.getSuit());
}

bool Match::attemptPlayCard(int cardIndex, std::string newSuit) {
    Player& current = players[currentPlayerIndex];
    if (cardIndex < 0 || cardIndex >= current.getHand().size()) return false;
    
    Card toPlay = current.getHand()[cardIndex];
    if (!isValidMove(toPlay)) return false;
    
    // Move is valid, execute play
    current.playCard(cardIndex);
    table->addCard(toPlay);
    declaredSuit = ""; // Reset suit override unless it's an Ace
    
    std::string val = toPlay.getValue();
    int stepsToAdvance = 1;
    
    // Handle Special Cards
    if (val == "A") {
        declaredSuit = newSuit; 
    } else if (val == "7") {
        cardsToDraw += 2; // Stacks if already active 
    } else if (val == "8") {
        stepsToAdvance = 0; // Player plays again 
    } else if (val == "9") {
        stepsToAdvance = 2; // Skips next player. Works perfectly for 2 or 4 players 
    }
    
    // Check ending constraints: Cannot go out on a special card 
    if (current.hasEmptyHand()) {
        if (val == "A" || val == "7" || val == "8" || val == "9") {
            safeDraw(current); // Penalty draw
        } else {
            matchOver = true; 
            return true;
        }
    }
    
    advanceTurn(stepsToAdvance);
    return true;
}

bool Match::attemptDrawPenalty() {
    if (cardsToDraw == 0) return false;
    
    Player& current = players[currentPlayerIndex];
    for (int i = 0; i < cardsToDraw; ++i) {
        safeDraw(current);
    }
    cardsToDraw = 0; // Penalty resolved
    advanceTurn(); // Turn ends after drawing penalty
    return true;
}

bool Match::attemptDraw() {
    if (cardsToDraw > 0) return false; // Must resolve penalty first
    Player& current = players[currentPlayerIndex];
    safeDraw(current); 
    return true;
}

bool Match::attemptPass() {
    Player& current = players[currentPlayerIndex];
    if (!current.getHasDrawnThisTurn()) return false; // Must draw before passing 
    
    current.passTurn();
    advanceTurn();
    return true;
}

Player& Match::getCurrentPlayer() { return players[currentPlayerIndex]; }
Card Match::getTopCard() const { return table->getTopCard(); }
std::string Match::getDeclaredSuit() const { return declaredSuit; }
int Match::getCardsToDraw() const { return cardsToDraw; }
bool Match::isMatchOver() const { return matchOver; }
const std::vector<Player>& Match::getPlayers() const { return players; }

void Match::endMatchPointsCalc() {
    for (Player& p : players) {
        p.addPoints(p.calculateHandPoints()); 
    }
}

// --- NEW: Reset logic for the Next Round ---
void Match::resetForNextRound(unsigned int newSeed) {
    matchSeed = newSeed;
    matchOver = false;
    cardsToDraw = 0;
    declaredSuit = "";
    currentPlayerIndex = 0;

    // 1. Re-initialize a brand new 52-card deck
    deck = Deck(); // Assumes your Deck constructor builds a standard 52-card deck
    deck.shuffle(matchSeed);

    // 2. Clear out the players' old hands and turn states
    for (Player& p : players) {
        p.clearHand(); 
        p.setHasDrawnThisTurn(false);
    }

    // 3. Clear the table
    if (table != nullptr) {
        delete table;
        table = nullptr;
    }

    // 4. Deal fresh cards!
    dealInitialCards();
}