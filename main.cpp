#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <memory>
#include <map>
#include <thread>
#include <chrono>
#include <future>
#include <atomic>
#include "Match.h"
#include "Player.h"
#include "Card.h"
#include "AIBot.h"
#include "LobbyManager.h"


#include "firebase/app.h"
#include "firebase/database.h"
#include "firebase/util.h"

void clearInput() {
    std::cin.clear();
    std::cin.ignore(10000, '\n');
}

int main() {
    std::cout << "=================================\n";
    std::cout << "      WELCOME TO AGONIA!         \n";
    std::cout << "=================================\n\n";


    std::cout << "Initializing Firebase...\n";

    // App::Create() with no arguments automatically looks for google-services.json 
    // in the executable's directory.
    firebase::App* app = firebase::App::Create();

    if (!app) {
        std::cerr << "ERROR: Failed to initialize Firebase App!\n";
        return 1;
    }

    // Now manually inject the URL into the database instance
    // REPLACE with your actual URL from the console
    firebase::database::Database* database = firebase::database::Database::GetInstance(app, "https://agoniagame-default-rtdb.europe-west1.firebasedatabase.app/");

    if (!database) {
        std::cerr << "ERROR: Failed to initialize Firebase Database!\n";
        return 1;
    }
    database->set_persistence_enabled(false);
    // --- PING TEST ---
    // This writes a test string to a node called "server_ping" in your database
    firebase::database::DatabaseReference pingRef = database->GetReference("server_ping");
    pingRef.SetValue("Agonia Game Engine is officially online!");
    
    std::cout << "SUCCESS! Firebase is connected and ping was sent.\n";
    std::cout << "Check your Firebase Console to see the data!\n\n";

    // --- LOBBY SYSTEM INTEGRATION ---
    LobbyManager lobbyManager(database);
    LobbyData currentLobby;
    std::atomic<bool> gameReady{false};

    // Callback: Updates local data when Firebase changes
    lobbyManager.onLobbyUpdated = [&](const LobbyData& data) {
        currentLobby = data;
        if (data.status == "playing") {
            gameReady = true;
        }
    };

    std::string myName;
    std::cout << "Enter your player name: ";
    std::cin >> myName;

    int choice = 0;
    bool isWaiting = false;
    while (!gameReady) {
        // Status Update: Show players currently in room
        if (!lobbyManager.getCode().empty()) {
            std::cout << "\n--- LOBBY [" << lobbyManager.getCode() << "] ---\n";
            for (auto& p : currentLobby.players) {
                std::cout << " - " << p.name << (p.isBot ? " (BOT)" : "") << "\n";
            }
            if (lobbyManager.isLocalHost()) {
                std::cout << "5. Fill with Bots & Start | 6. Start Game Now\nSelection: ";
            } else {
                std::cout << "Waiting for host to start...\n";
            }
        } else if (!isWaiting) {
            std::cout << "\n--- ONLINE MENU ---\n";
            std::cout << "1. Create Public Lobby\n2. Create Private Lobby\n3. Join Lobby by Code\n4. Find Public Games\nSelection: ";
        }

        // NON-BLOCKING INPUT WAIT (Short sleep to let Firebase update)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // We only prompt for input if we aren't waiting OR if we are the host and need to start
        if (!isWaiting || lobbyManager.isLocalHost()) {
            if (std::cin >> choice) {
                if (choice == 1) { lobbyManager.createLobby(myName, false); isWaiting = true; }
                else if (choice == 2) { lobbyManager.createLobby(myName, true); isWaiting = true; }
                else if (choice == 3) {
                    std::string code; std::cout << "Enter Code: "; std::cin >> code;
                    lobbyManager.joinLobby(code, myName); isWaiting = true;
                }
                else if (choice == 4) lobbyManager.findPublicLobbies();
                else if (choice == 5 && lobbyManager.isLocalHost()) {
                    lobbyManager.fillWithBots();
                    lobbyManager.startGame();
                }
                else if (choice == 6 && lobbyManager.isLocalHost()) lobbyManager.startGame();
            }
        }
        if (gameReady) break;
    }

    // --- 2. MATCH INIT ---
    std::vector<std::string> playerNames;
    std::map<int, std::unique_ptr<AIBot>> bots;
    int myIndex = -1;

    for (int i = 0; i < currentLobby.players.size(); ++i) {
        playerNames.push_back(currentLobby.players[i].name);
        if (currentLobby.players[i].name == myName && !currentLobby.players[i].isBot) myIndex = i;
        if (currentLobby.players[i].isBot) bots[i] = std::make_unique<AIBot>(i, Difficulty::INTERMEDIATE);
    }

    Match match(playerNames);
    
    if (lobbyManager.isLocalHost()) {
        std::cout << "Generating and syncing deck to cloud...\n";
        match.getDeck().shuffle();
        // Host pushes the full 52-card deck FIRST
        lobbyManager.syncInitialMatch(match); 
        // Then deals locally
        match.dealInitialCards();
    } else {
        std::cout << "Downloading deck from Host...\n";
        auto deckRef = database->GetReference("lobbies").Child(lobbyManager.getCode()).Child("game_state").Child("deck");
        std::vector<std::string> syncedDeck;
        
        // Polling loop to wait for the host's deck
        while (syncedDeck.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            auto future = deckRef.GetValue();
            while (future.status() == firebase::kFutureStatusPending) {} 
            
            if (future.status() == firebase::kFutureStatusComplete && future.error() == 0) {
                if (future.result()->exists()) {
                    // FIX: Do not use Firebase's default children() iterator! 
                    // It sorts lexicographically (1, 10, 11, 2). Force integer ordering.
                    int deckSize = (int)future.result()->children_count();
                    for (int i = 0; i < deckSize; ++i) {
                        syncedDeck.push_back(future.result()->Child(std::to_string(i)).value().string_value());
                    }
                }
            }
        }
        
        // Guest overwrites their deck with the Host's exact order
        match.getDeck().loadFromSerialized(syncedDeck);
        // Guest deals locally (results in exact same hands and table as Host!)
        match.dealInitialCards();
        std::cout << "Deck synced and cards dealt!\n";
    }
    // Only the host pushes the starting table card and turn info to the cloud
    if (lobbyManager.isLocalHost()) {
        lobbyManager.syncInitialMatch(match);
    }
    
    // --- 3. ONLINE GAME LOOP ---
    
    lobbyManager.listenForMoves([&](int pIdx, std::string type, int cIdx, std::string suit) {
        if (pIdx == myIndex) return;
        if (lobbyManager.isLocalHost() && currentLobby.players[pIdx].isBot) return;

        std::cout << "\n[NETWORK] Move received from Player " << pIdx << ": " << type << "\n";

        bool success = false;
        if (type == "D"||type == "d") {
            success = (match.getCardsToDraw() > 0) ? match.attemptDrawPenalty() : match.attemptDraw();
        } else if (type == "P"||type == "p") {
            success = match.attemptPass();
        } else {
            success = match.attemptPlayCard(cIdx, suit);
        }

        // The Safeguard: If the local game rejects the network move, warn us immediately!
        if (!success) {
            std::cout << "[CRITICAL DESYNC] Network move rejected! Card index " << cIdx << " was invalid locally.\n";
        }
    });

    auto gameRef = database->GetReference("lobbies").Child(lobbyManager.getCode()).Child("game_state");

    while (!match.isMatchOver()) {
        int currentIdx = match.getCurrentPlayerIndex();
        bool isMyTurn = (currentIdx == myIndex);
        
        std::cout << "\n=======================================================";
        std::cout << "\nTABLE: " << match.getTopCard().toString();
        if (!match.getDeclaredSuit().empty()) std::cout << " | SUIT: " << match.getDeclaredSuit();

        if (match.getCardsToDraw() > 0) {
            std::cout << "\n[!] PENALTY ACTIVE: Owe " << match.getCardsToDraw() << " cards!";
            std::cout << "\n[!] You must play a 7 or type 'D' to draw the penalty.";
        }
        
        std::cout << "\n-------------------------------------------------------\n";

        if (isMyTurn) {
            std::cout << ">>> YOUR TURN! (30s timeout) <<<\n";
            const std::vector<Card>& hand = match.getCurrentPlayer().getHand();
            for (size_t i = 0; i < hand.size(); ++i) std::cout << "[" << i << "] " << hand[i].toString() << "  ";
            
            std::cout << "\nAction [Index/D/P]: ";
            
            // ASYNC INPUT WITH 30s TIMER
            auto inputTask = std::async(std::launch::async, []() { std::string s; std::cin >> s; return s; });
            if (inputTask.wait_for(std::chrono::seconds(30)) == std::future_status::timeout) {
                std::cout << "\nTIMEOUT! Auto-passing...\n";
                match.attemptDraw(); match.attemptPass();
                lobbyManager.pushMove(myIndex, "P");
            } else {
                std::string input = inputTask.get();
                char cmd = toupper(input[0]);
                bool success = false;
                if (cmd == 'D') success = match.getCardsToDraw() > 0 ? match.attemptDrawPenalty() : match.attemptDraw();
                else if (cmd == 'P') success = match.attemptPass();
                else {
                    try {
                        int idx = std::stoi(input);
                        std::string nS = "";
                        if (hand.at(idx).getValue() == "A") { std::cout << "Suit: "; std::cin >> nS; }
                        success = match.attemptPlayCard(idx, nS);
                    } catch (...) { success = false; }
                }
                if (success) {
                    int finalIdx = -1;
                    // If the input was a number (card index), send that index to Firebase
                    try { finalIdx = std::stoi(input); } catch(...) { finalIdx = -1; }
                    
                    lobbyManager.pushMove(myIndex, input, finalIdx, match.getDeclaredSuit());
                    lobbyManager.syncTurnState(match);
                }                
                else std::cout << "Invalid move, try again!\n";
            }
        } 
        else {
            std::cout << "Waiting for " << playerNames[currentIdx] << "...\n";
            if (lobbyManager.isLocalHost() && currentLobby.players[currentIdx].isBot) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                
                // Host computes the bot move locally
                BotMoveData bData = bots[currentIdx]->takeTurn(match); 
                
                // Host pushes the exact move to Firebase so Guests can update their tables
                lobbyManager.pushMove(currentIdx, bData.type, bData.cardIndex, bData.suit);
                lobbyManager.syncTurnState(match);
            } else {
                // Poll for turn change - The listener above will automatically 
                // update 'match' and break this loop for us!
                while (match.getCurrentPlayerIndex() == currentIdx && !match.isMatchOver()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            }
        }
    }

    // --- 4. END & CLEANUP ---
    match.endMatchPointsCalc();
    std::cout << "\nMATCH OVER! Scores:\n";
    for (const Player& p : match.getPlayers()) std::cout << p.getName() << ": " << p.getScore() << "\n";

    if (lobbyManager.isLocalHost()) {
        std::cout << "Cleaning up cloud data...\n";
        database->GetReference("lobbies").Child(lobbyManager.getCode()).RemoveValue();
    }
    return 0;
}