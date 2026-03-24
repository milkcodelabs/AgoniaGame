#include <iostream>
#include <memory>
#include <map>
#include <vector>
#include <SDL.h> 

#include "firebase/app.h"
#include "firebase/database.h"

#include "Match.h"
#include "Player.h"
#include "AIBot.h"
#include "LobbyManager.h"
#include "GameWindow.h" 

int main(int argc, char* argv[]) {
    std::cout << "Starting Agonia UI Engine...\n";

    // --- 1. INITIALIZE FIREBASE ---
    firebase::App* app = firebase::App::Create();
    firebase::database::Database* database = firebase::database::Database::GetInstance(app, "https://agoniagame-default-rtdb.europe-west1.firebasedatabase.app/");
    database->set_persistence_enabled(false);

    // --- 2. INITIALIZE GRAPHICS ---
    GameWindow window("Agonia", 1920, 1080);
    if (!window.init()) {
        std::cerr << "Failed to initialize graphics!\n";
        return 1;
    }

    // --- 3. GAME STATE VARIABLES ---
    AppState currentState = AppState::NAME_INPUT; 
    LobbyManager lobbyManager(database);
    std::unique_ptr<Match> currentMatch = nullptr;
    
    std::string myName = ""; 
    std::string currentHostName = "";
    int myIndex = -1;
    std::map<int, std::unique_ptr<AIBot>> bots;
    
    std::vector<PlayerInfo> currentLobbyPlayers; 
    std::vector<PublicLobbyInfo> publicLobbies; 
    
    int pendingCardIndex = -1; 
    
    // Non-blocking timers
    Uint32 botTurnStartTime = 0; 
    bool botTimerStarted = false;
    Uint32 turnStartTime = SDL_GetTicks();
    int lastTurnIndex = -1; 

    // --- 4. UI CALLBACK BINDINGS ---
    window.onKickPlayerClicked = [&](std::string targetName) {
        lobbyManager.kickPlayer(targetName);
    };
    
    window.onNameEntered = [&](std::string name) {
        myName = name.empty() ? "Guest" : name;
        currentState = AppState::MAIN_MENU;
    };

    window.onMenuOptionSelected = [&](int choice) {
        if (choice == 1) { lobbyManager.createLobby(myName, false); currentState = AppState::LOBBY; }
        else if (choice == 2) { lobbyManager.createLobby(myName, true); currentState = AppState::LOBBY; }
        else if (choice == 3) { currentState = AppState::JOIN_INPUT; }
        else if (choice == 4) { 
            currentState = AppState::FIND_LOBBY;
            publicLobbies.clear();
            
            database->GetReference("lobbies").OrderByChild("status").EqualTo("waiting").GetValue()
            .OnCompletion([&](const firebase::Future<firebase::database::DataSnapshot>& done) {
                if (done.error() == 0 && done.result()->exists()) {
                    for (auto& lobby : done.result()->children()) {
                        if (!lobby.Child("is_private").value().bool_value()) {
                            PublicLobbyInfo info;
                            info.code = lobby.key();
                            info.hostName = lobby.Child("host_name").value().string_value();
                            info.playerCount = (int)lobby.Child("players").children_count();
                            publicLobbies.push_back(info);
                        }
                    }
                }
            });
        }
        else if (choice == -1) {
            if (currentState == AppState::LOBBY) {
                lobbyManager.leaveLobby();
                currentState = AppState::MAIN_MENU;
            } else {
                currentState = AppState::MAIN_MENU;
            }
        }
    };

    window.onJoinCodeEntered = [&](std::string code) {
        lobbyManager.joinLobby(code, myName);
        currentState = AppState::LOBBY;
    };

    window.onFillBotsClicked = [&]() {
        lobbyManager.fillWithBots();
    };

    window.onStartGameClicked = [&]() {
        lobbyManager.startGame();
    };

    window.onCardPlayed = [&](int cardIndex) {
        if (currentState == AppState::PLAYING && currentMatch && currentMatch->getCurrentPlayerIndex() == myIndex) {
            const auto& hand = currentMatch->getCurrentPlayer().getHand();
            
            if (hand[cardIndex].getValue() == "A") {
                pendingCardIndex = cardIndex;
                window.triggerSuitSelection(); 
            } else {
                bool success = currentMatch->attemptPlayCard(cardIndex, "");
                if (success) {
                    lobbyManager.pushMove(myIndex, std::to_string(cardIndex), cardIndex, currentMatch->getDeclaredSuit());
                    lobbyManager.syncTurnState(*currentMatch);
                } else {
                    window.triggerNotification("Invalid move! Card must match suit or value.");
                }
            }
        }
    };

    window.onSuitSelected = [&](std::string suit) {
        if (pendingCardIndex != -1 && currentMatch) {
            bool success = currentMatch->attemptPlayCard(pendingCardIndex, suit);
            if (success) {
                lobbyManager.pushMove(myIndex, std::to_string(pendingCardIndex), pendingCardIndex, suit);
                lobbyManager.syncTurnState(*currentMatch);
                window.clearCardSelection(); 
            }
            pendingCardIndex = -1;
        }
    };

    window.onDrawClicked = [&]() {
        if (currentState == AppState::PLAYING && currentMatch && currentMatch->getCurrentPlayerIndex() == myIndex) {
            bool success = (currentMatch->getCardsToDraw() > 0) ? currentMatch->attemptDrawPenalty() : currentMatch->attemptDraw();
            if (success) {
                lobbyManager.pushMove(myIndex, "D", -1, "");
                lobbyManager.syncTurnState(*currentMatch);
                window.clearCardSelection();
            }
        }
    };

    window.onPassClicked = [&]() {
        if (currentState == AppState::PLAYING && currentMatch && currentMatch->getCurrentPlayerIndex() == myIndex) {
            bool success = currentMatch->attemptPass();
            if (success) {
                lobbyManager.pushMove(myIndex, "P", -1, "");
                lobbyManager.syncTurnState(*currentMatch);
                window.clearCardSelection();
            } else {
                window.triggerNotification("You must draw a card first!");
            }
        }
    };


    // --- 5. NETWORK LISTENER & LOBBY SYNC ---
    
    // THE BUG FIX: We store the network logic in a lambda, but DO NOT attach it to Firebase 
    // until we actually have a valid game state and a valid currentLobbyRef.
    auto networkMoveHandler = [&](int pIdx, std::string type, int cIdx, std::string suit) {
        if (!currentMatch || pIdx == myIndex) return;
        if (lobbyManager.isLocalHost() && bots.count(pIdx) > 0) return; 

        std::cout << "\n[NETWORK] Received Move -> Player: " << pIdx << " | Type: " << type << "\n";

        if (type == "D" || type == "d") {
            (currentMatch->getCardsToDraw() > 0) ? currentMatch->attemptDrawPenalty() : currentMatch->attemptDraw();
        } else if (type == "P" || type == "p") {
            currentMatch->attemptPass();
        } else {
            bool success = currentMatch->attemptPlayCard(cIdx, suit);
            if (!success) std::cout << "[CRITICAL DESYNC] Remote move rejected by local rules!\n";
        }
    };

    lobbyManager.onLobbyUpdated = [&](const LobbyData& data) {
        // Did the host delete the lobby?
        if (data.status == "deleted") {
            if (currentState == AppState::LOBBY || currentState == AppState::PLAYING) {
                currentState = AppState::MAIN_MENU;
                window.triggerNotification("The host closed the lobby.");
            }
            return;
        }

        currentLobbyPlayers = data.players; 
        currentHostName = data.hostName;

        // Am I still in the player list? (Handling being kicked)
        if (currentState == AppState::LOBBY && !lobbyManager.isLocalHost()) {
            bool amIHere = false;
            for (const auto& p : data.players) { if (p.name == myName) amIHere = true; }
            if (!amIHere) {
                lobbyManager.leaveLobby();
                currentState = AppState::MAIN_MENU;
                window.triggerNotification("You were kicked from the lobby.");
                return;
            }
        }

        if (currentState == AppState::LOBBY && data.status == "playing") {
            std::cout << "Game started! Preparing match...\n";
            
            std::vector<std::string> playerNames;
            for (int i = 0; i < data.players.size(); ++i) {
                playerNames.push_back(data.players[i].name);
                if (data.players[i].name == myName && !data.players[i].isBot) myIndex = i;
                if (data.players[i].isBot) bots[i] = std::make_unique<AIBot>(i, Difficulty::INTERMEDIATE);
            }
            
            currentMatch = std::make_unique<Match>(playerNames);

            if (lobbyManager.isLocalHost()) {
                currentMatch->getDeck().shuffle();
                lobbyManager.syncInitialMatch(*currentMatch);
                currentMatch->dealInitialCards();
                
                // ATTACH THE LISTENER SAFELY
                lobbyManager.listenForMoves(networkMoveHandler);
                currentState = AppState::PLAYING;
            } else {
                auto deckRef = database->GetReference("lobbies").Child(lobbyManager.getCode()).Child("game_state").Child("deck");
                deckRef.GetValue().OnCompletion([&, networkMoveHandler](const firebase::Future<firebase::database::DataSnapshot>& future) {
                    if (future.error() == 0 && future.result()->exists()) {
                        std::vector<std::string> syncedDeck;
                        int deckSize = (int)future.result()->children_count();
                        for (int i = 0; i < deckSize; ++i) {
                            syncedDeck.push_back(future.result()->Child(std::to_string(i)).value().string_value());
                        }
                        currentMatch->getDeck().loadFromSerialized(syncedDeck);
                        currentMatch->dealInitialCards();
                        
                        // ATTACH THE LISTENER SAFELY ONCE DECK IS LOADED
                        lobbyManager.listenForMoves(networkMoveHandler);
                        currentState = AppState::PLAYING; 
                    }
                });
            }
        }
    };


    // --- 6. THE 60 FPS NON-BLOCKING LOOP ---
    
    while (window.isRunning()) {
        Uint32 frameStart = SDL_GetTicks();

        window.processInput(currentState, currentMatch.get(), myIndex);

        // 2. Game Logic Updates
        if (currentState == AppState::PLAYING && currentMatch && !currentMatch->isMatchOver()) {
            int currentIdx = currentMatch->getCurrentPlayerIndex();
            
            if (currentIdx != lastTurnIndex) {
                lastTurnIndex = currentIdx;
                turnStartTime = SDL_GetTicks(); 
                botTimerStarted = false;        
                window.clearCardSelection(); 
            }
            
            if (lobbyManager.isLocalHost() && bots.count(currentIdx) > 0) {
                if (!botTimerStarted) {
                    botTurnStartTime = SDL_GetTicks();
                    botTimerStarted = true;
                } else if (SDL_GetTicks() - botTurnStartTime > 1500) { 
                    BotMoveData bData = bots[currentIdx]->takeTurn(*currentMatch); 
                    lobbyManager.pushMove(currentIdx, bData.type, bData.cardIndex, bData.suit);
                    lobbyManager.syncTurnState(*currentMatch);
                    botTimerStarted = false; 
                }
            }
            else if (currentIdx == myIndex) {
                if (SDL_GetTicks() - turnStartTime > 30000) { 
                    std::cout << "TIMEOUT! Auto-passing...\n";
                    currentMatch->attemptDraw(); 
                    currentMatch->attemptPass();
                    lobbyManager.pushMove(myIndex, "P", -1, "");
                    lobbyManager.syncTurnState(*currentMatch);
                }
            }
        } else if (currentState == AppState::PLAYING && currentMatch && currentMatch->isMatchOver()) {
            currentState = AppState::GAME_OVER;
            currentMatch->endMatchPointsCalc();
        }

        // 3. Render State
        window.render(currentState, currentMatch.get(), myIndex, lobbyManager.getCode(), currentLobbyPlayers, publicLobbies, currentHostName);
        
        // 4. Cap at ~60 FPS
        Uint32 frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < 16) {
            SDL_Delay(16 - frameTime);
        }
    }

    if (lobbyManager.isLocalHost()) {
         database->GetReference("lobbies").Child(lobbyManager.getCode()).RemoveValue();
    }
    return 0;
}