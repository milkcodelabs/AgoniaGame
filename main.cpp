#include <iostream>
#include <memory>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <SDL.h> 

#include "firebase/app.h"
#include "firebase/database.h"

#include "Match.h"
#include "Player.h"
#include "AIBot.h"
#include "LobbyManager.h"
#include "GameWindow.h" 

// --- THREAD SAFETY DISPATCHER ---
std::mutex queueMutex;
std::vector<std::function<void()>> taskQueue;

void runOnMainThread(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(queueMutex);
    taskQueue.push_back(task);
}

int main(int argc, char* argv[]) {
    std::cout << "Starting Agonia UI Engine...\n";
    std::cout << "Initializing Firebase...\n";

    // 1. Manually configure the options instead of reading the JSON file
    firebase::AppOptions options;
    options.set_app_id("1:553280452101:android:9f3251b8b14ec5b476ce6c"); 
    options.set_api_key("AIzaSyB6Htx5s6xBhEoLC_BjrRH_nspCEMaRBNY");     
    options.set_project_id("agoniagame");                 
    options.set_database_url("https://agoniagame-default-rtdb.europe-west1.firebasedatabase.app/");

    firebase::App* app = firebase::App::Create(options);

    if (!app) {
        std::cerr << "ERROR: Failed to initialize Firebase App!\n";
        return 1;
    }

    firebase::database::Database* database = firebase::database::Database::GetInstance(app);
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
    int currentTargetScore = 50;
    bool sortBySuit = true;
    
    // Non-blocking timers
    Uint32 botTurnStartTime = 0; 
    bool botTimerStarted = false;
    Uint32 turnStartTime = SDL_GetTicks();
    int lastTurnIndex = -1; 
    bool localHasDrawnThisTurn = false;
    bool isFillingBots = false;
    
    // --- Delta Time Tracker ---
    Uint32 lastFrameTime = SDL_GetTicks(); 

    // --- 4. UI CALLBACK BINDINGS ---
    window.onToggleScoreClicked = [&]() {
        lobbyManager.setTargetScore(currentTargetScore == 50 ? 100 : 50);
    };

    window.onSortClicked = [&]() {
        sortBySuit = !sortBySuit; 
    };

    window.onNextRoundClicked = [&]() {
        if (lobbyManager.isLocalHost() && currentMatch && !window.isLoadingNextRound) {
            window.isLoadingNextRound = true; 
            unsigned int newRoundSeed = static_cast<unsigned int>(std::time(nullptr) + rand()); 

            auto movesRef = database->GetReference("lobbies").Child(lobbyManager.getCode()).Child("game_state").Child("moves");
            window.playSound(Sfx::SHUFFLE);
            movesRef.RemoveValue().OnCompletion([&, newRoundSeed](const firebase::Future<void>&) {
                lobbyManager.pushMove(myIndex, "RT", (int)newRoundSeed, "");
            });
        }
    };

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
                    std::vector<PublicLobbyInfo> tempLobbies;
                    for (auto& lobby : done.result()->children()) {
                        auto privVal = lobby.Child("is_private").value();
                        if (privVal.is_bool() && !privVal.bool_value()) {
                            PublicLobbyInfo info;
                            info.code = lobby.key();
                            auto hName = lobby.Child("host_name").value();
                            info.hostName = hName.is_string() ? hName.string_value() : "Unknown";
                            info.playerCount = (int)lobby.Child("players").children_count();
                            tempLobbies.push_back(info);
                        }
                    }
                    runOnMainThread([&, tempLobbies]() {
                        publicLobbies = tempLobbies;
                    });
                }
            });
        }
        else if (choice == -1) {
            // If we are returning from ANY multiplayer state, nuke everything.
            if (currentState == AppState::LOBBY || currentState == AppState::PLAYING || currentState == AppState::GAME_OVER) {
                lobbyManager.leaveLobby();
                currentMatch.reset(); 
                bots.clear();
            }
            currentState = AppState::MAIN_MENU;
        }
    };

    window.onJoinCodeEntered = [&](std::string code) {
        lobbyManager.joinLobby(code, myName);
        currentState = AppState::LOBBY;
    };

    window.onFillBotsClicked = [&]() {
        if (currentLobbyPlayers.size() < 4 && !isFillingBots) {
            isFillingBots = true;
            lobbyManager.fillWithBots(currentLobbyPlayers.size());
            std::thread([&]() { std::this_thread::sleep_for(std::chrono::seconds(2)); isFillingBots = false; }).detach();
        }
    };

    window.onStartGameClicked = [&]() {
        lobbyManager.startGame();
    };

    window.onCardPlayed = [&](int cardIndex) {
        if (currentState == AppState::PLAYING && currentMatch && currentMatch->getCurrentPlayerIndex() == myIndex) {
            const auto& hand = currentMatch->getCurrentPlayer().getHand();
            Card playedCard = hand[cardIndex];
            
            std::string cardId = playedCard.getValue() + "|" + playedCard.getSuit();

            // 1. Check EXACTLY why a move might be invalid before doing anything else
            std::string errorMsg = currentMatch->getInvalidReason(playedCard);

            if (!errorMsg.empty()) {
                // Move is illegal. Tell them exactly why. 
                // (This automatically plays Sfx::FAIL based on our last update!)
                window.triggerNotification(errorMsg);
            } 
            else if (playedCard.getValue() == "A") {
                // Move is legal AND it's an Ace. Now it's safe to open the menu.
                pendingCardIndex = cardIndex;
                window.triggerSuitSelection(); 
            } 
            else {
                // Move is legal and a normal card. Play it.
                bool success = currentMatch->attemptPlayCard(cardIndex, "");
                if (success) {
                    lobbyManager.pushMove(myIndex, cardId, cardIndex, currentMatch->getDeclaredSuit());
                    lobbyManager.syncTurnState(*currentMatch);
                }
            }
        }
    };

    window.onSuitSelected = [&](std::string suit) {
        if (pendingCardIndex != -1 && currentMatch) {
            const auto& hand = currentMatch->getCurrentPlayer().getHand();
            std::string cardId = hand[pendingCardIndex].getValue() + "|" + hand[pendingCardIndex].getSuit();

            bool success = currentMatch->attemptPlayCard(pendingCardIndex, suit);
            if (success) {
                lobbyManager.pushMove(myIndex, cardId, pendingCardIndex, suit);
                lobbyManager.syncTurnState(*currentMatch);
                window.clearCardSelection(); 
            } else {
                // THE FIX: Catch the invalid move (like Ace on Ace) and alert the player!
                window.triggerNotification("Invalid move! You cannot play an Ace on an Ace.");
                window.clearCardSelection(); 
            }
            pendingCardIndex = -1;
        }
    };

    window.onDrawClicked = [&]() {
        if (currentState == AppState::PLAYING && currentMatch && currentMatch->getCurrentPlayerIndex() == myIndex) {
            if (localHasDrawnThisTurn) {
                window.triggerNotification("You can only draw once per turn!");
                return;
            }
            
            bool success = (currentMatch->getCardsToDraw() > 0) ? currentMatch->attemptDrawPenalty() : currentMatch->attemptDraw();
            if (success) {
                localHasDrawnThisTurn = true;
                lobbyManager.pushMove(myIndex, "D", -1, "");
                lobbyManager.syncTurnState(*currentMatch);
                window.clearCardSelection();
            } else {
                window.triggerNotification("The deck is completely empty!");
            }
        }
    };

    window.onPassClicked = [&]() {
        if (currentState == AppState::PLAYING && currentMatch && currentMatch->getCurrentPlayerIndex() == myIndex) {
            bool success = currentMatch->attemptPass();
            if (success) {
                window.playSound(Sfx::PASS);
                lobbyManager.pushMove(myIndex, "P", -1, "");
                lobbyManager.syncTurnState(*currentMatch);
                window.clearCardSelection();
            } else {
                window.triggerNotification("You must draw a card first!");
            }
        }
    };


    // --- 5. NETWORK LISTENER & LOBBY SYNC ---
    
    auto networkMoveHandler = [&](int pIdx, std::string type, int cIdx, std::string suit) {
        runOnMainThread([&, pIdx, type, cIdx, suit]() {
            if (!currentMatch) return;

            if (type == "RT") {
                std::cout << "[SYSTEM] Starting next round with seed: " << cIdx << "\n";
                currentMatch->resetForNextRound((unsigned int)cIdx);
                
                window.isLoadingNextRound = false;
                window.playSound(Sfx::SHUFFLE);
                currentState = AppState::PLAYING;
                lastTurnIndex = -1; 
                localHasDrawnThisTurn = false;
                window.clearCardSelection();
                return; 
            }

            if (pIdx == myIndex || (lobbyManager.isLocalHost() && bots.count(pIdx) > 0)) return; 

            std::cout << "\n[NETWORK] Received Move -> Player: " << pIdx << " | Type: " << type << "\n";

            if (type == "D" || type == "d") {
                (currentMatch->getCardsToDraw() > 0) ? currentMatch->attemptDrawPenalty() : currentMatch->attemptDraw();
            } else if (type == "P" || type == "p") {
                window.playSound(Sfx::PASS);
                currentMatch->attemptPass();
            } else if (type == "FS") {
                std::cout << "[SYSTEM] Executing Failsafe Turn Advance!\n";
                currentMatch->advanceTurn(1);
            } else {
                int realIdx = -1;
                const auto& remoteHand = currentMatch->getPlayer(pIdx).getHand();
                for (int i = 0; i < remoteHand.size(); ++i) {
                    if (remoteHand[i].getValue() + "|" + remoteHand[i].getSuit() == type) {
                        realIdx = i;
                        break;
                    }
                }

                if (realIdx != -1) {
                    bool success = currentMatch->attemptPlayCard(realIdx, suit);
                    if (!success) std::cout << "[CRITICAL DESYNC] Remote move rejected by local rules!\n";
                } else {
                    std::cout << "[CRITICAL DESYNC] Could not find card '" << type << "' in Player " << pIdx << "'s hand!\n";
                }
            }
        });
    };

    lobbyManager.onLobbyUpdated = [&](const LobbyData& data) {
        runOnMainThread([&, data]() {
            if (data.status == "deleted") {
                // THE FIX: Added GAME_OVER to the state check
                if (currentState == AppState::LOBBY || currentState == AppState::PLAYING || currentState == AppState::GAME_OVER) {
                    lobbyManager.leaveLobby(); 
                    currentMatch.reset();      
                    bots.clear();              
                    
                    currentState = AppState::MAIN_MENU;
                    window.triggerNotification("The host closed the lobby.");
                }
                return;
            }

            currentLobbyPlayers = data.players; 
            currentHostName = data.hostName;
            currentTargetScore = data.targetScore;

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

            if (currentState == AppState::PLAYING) {
                if (lobbyManager.isLocalHost()) {
                    for (int i = 0; i < data.players.size(); ++i) {
                        if (data.players[i].isBot && bots.count(i) == 0) {
                            std::cout << "[SYSTEM] " << data.players[i].name << " disconnected. AI taking over.\n";
                            bots[i] = std::make_unique<AIBot>(i, Difficulty::INTERMEDIATE);
                            window.triggerNotification(data.players[i].name + " left. A bot took over!");
                        }
                    }
                }
                return; 
            }

            if (currentState == AppState::LOBBY && data.status == "playing") {
                if (currentMatch) return;
                std::cout << "Game started! Preparing match...\n";
                
                std::vector<std::string> playerNames;
                for (int i = 0; i < data.players.size(); ++i) {
                    playerNames.push_back(data.players[i].name);
                    if (data.players[i].name == myName && !data.players[i].isBot) myIndex = i;
                    if (data.players[i].isBot) bots[i] = std::make_unique<AIBot>(i, Difficulty::INTERMEDIATE);
                }
                
                currentMatch = std::make_unique<Match>(playerNames);

                // --- BIND NEW ANIMATION EVENTS TO THE MATCH ---
                currentMatch->onCardPlayedEvent = [&](int pIdx, Card card) {
                    window.playSound(Sfx::PLAY_CARD);
                    float startX = 1920 / 2.0f, startY = 1080 / 2.0f; // Default center
                    float targetX = 1920 / 2.0f - 75.0f, targetY = 1080 / 2.0f - 100.0f; // Pile coordinates

                    if (pIdx == -1) { // -1 signifies the Deck (Dealer)
                        startX = 1920 / 2.0f - 300.0f;
                    } else {
                        int numPlayers = currentMatch->getPlayers().size();
                        int relativePos = (pIdx - myIndex + numPlayers) % numPlayers;
                        
                        // Calculate position based on relative location
                        if (relativePos == 0)      { startY = 1080.0f - 180.0f; } // Bottom
                        else if ((numPlayers == 2 && relativePos == 1) || (numPlayers == 4 && relativePos == 2)) 
                                                   { startY = 50.0f; } // Top
                        else if (numPlayers >= 3 && relativePos == 1) 
                                                   { startX = 50.0f; } // Left
                        else                       { startX = 1920.0f - 150.0f; } // Right
                    }
                    window.triggerAnimation(card, startX, startY, targetX, targetY);
                };

                currentMatch->onCardDrawnEvent = [&](int pIdx) {
                    window.playSound(Sfx::DRAW);
                    float startX = 1920 / 2.0f - 300.0f, startY = 1080 / 2.0f; // Deck coordinates
                    float targetX = 1920 / 2.0f, targetY = 1080 / 2.0f;

                    int numPlayers = currentMatch->getPlayers().size();
                    int relativePos = (pIdx - myIndex + numPlayers) % numPlayers;
                    
                    if (relativePos == 0)      { targetY = 1080.0f - 180.0f; } // Bottom
                    else if ((numPlayers == 2 && relativePos == 1) || (numPlayers == 4 && relativePos == 2)) 
                                               { targetY = 50.0f; } // Top
                    else if (numPlayers >= 3 && relativePos == 1) 
                                               { targetX = 50.0f; } // Left
                    else                       { targetX = 1920.0f - 150.0f; } // Right

                    // Create a generic card back to slide across the screen
                    Card dummyCard("0", "Hidden"); 
                    window.triggerAnimation(dummyCard, startX, startY, targetX, targetY);
                };
                // ----------------------------------------------

                if (lobbyManager.isLocalHost()) {
                    window.playSound(Sfx::SHUFFLE);
                    window.playSound(Sfx::START);
                    unsigned int initialSeed = static_cast<unsigned int>(std::time(nullptr));
                    currentMatch->setSeed(initialSeed);
                    currentMatch->getDeck().shuffle(initialSeed);
                    
                    lobbyManager.syncInitialMatch(*currentMatch);
                    currentMatch->dealInitialCards();
                    
                    lobbyManager.listenForMoves(networkMoveHandler);
                    currentState = AppState::PLAYING;
                } else {
                    auto deckRef = database->GetReference("lobbies").Child(lobbyManager.getCode()).Child("game_state").Child("deck");
                    deckRef.GetValue().OnCompletion([&, networkMoveHandler](const firebase::Future<firebase::database::DataSnapshot>& future) {
                        if (future.error() == 0 && future.result()->exists()) {
                            
                            std::vector<std::string> syncedDeck;
                            int deckSize = (int)future.result()->children_count();
                            for (int i = 0; i < deckSize; ++i) {
                                auto cardVal = future.result()->Child(std::to_string(i)).value();
                                if(cardVal.is_string()) syncedDeck.push_back(cardVal.string_value());
                            }
                            
                            runOnMainThread([&, syncedDeck, networkMoveHandler]() {
                                window.playSound(Sfx::SHUFFLE);
                                window.playSound(Sfx::START);
                                currentMatch->getDeck().loadFromSerialized(syncedDeck);
                                currentMatch->dealInitialCards();
                                lobbyManager.listenForMoves(networkMoveHandler);
                                currentState = AppState::PLAYING; 
                            });
                        }
                    });
                }
            }
        });
    };


    // --- 6. THE 60 FPS NON-BLOCKING LOOP ---
    
    while (window.isRunning()) {
        Uint32 frameStart = SDL_GetTicks();
        
        // --- Calculate Delta Time for Animations ---
        float dt = (frameStart - lastFrameTime) / 1000.0f;
        lastFrameTime = frameStart;

        // --- Execute pending background tasks safely ---
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            for (auto& task : taskQueue) {
                task();
            }
            taskQueue.clear();
        }

        window.processInput(currentState, currentMatch.get(), myIndex);

        // 2. Game Logic Updates
        if (currentState == AppState::PLAYING && currentMatch && !currentMatch->isMatchOver()) {
            int currentIdx = currentMatch->getCurrentPlayerIndex();
            
            if (currentIdx != lastTurnIndex) {
                lastTurnIndex = currentIdx;
                turnStartTime = SDL_GetTicks(); 
                botTimerStarted = false;        
                localHasDrawnThisTurn = false;
                window.clearCardSelection(); 
                if (currentIdx == myIndex) {
                    window.playSound(Sfx::YOUR_TURN);
                }
            }
            
            if (lobbyManager.isLocalHost() && bots.count(currentIdx) > 0) {
                if (!botTimerStarted) {
                    botTurnStartTime = SDL_GetTicks();
                    botTimerStarted = true;
                } else if (SDL_GetTicks() - botTurnStartTime > 1500) { 
                    BotMoveData bData = bots[currentIdx]->takeTurn(*currentMatch); 
                    
                    std::string networkType = bData.type;
                    if (networkType != "D" && networkType != "P") {
                        Card playedCard = currentMatch->getTopCard();
                        networkType = playedCard.getValue() + "|" + playedCard.getSuit();
                    }
                    
                    lobbyManager.pushMove(currentIdx, networkType, bData.cardIndex, bData.suit);
                    lobbyManager.syncTurnState(*currentMatch);
                    botTimerStarted = false; 
                }
            }
            else if (currentIdx == myIndex) {
                if (SDL_GetTicks() - turnStartTime > 30000) { 
                    std::cout << "TIMEOUT! Auto-passing...\n";
                    if (!localHasDrawnThisTurn) {
                        bool drew = (currentMatch->getCardsToDraw() > 0) ? currentMatch->attemptDrawPenalty() : currentMatch->attemptDraw();
                        if (drew) lobbyManager.pushMove(myIndex, "D", -1, "");
                    }
                    window.playSound(Sfx::PASS);
                    currentMatch->attemptPass();
                    lobbyManager.pushMove(myIndex, "P", -1, "");
                    lobbyManager.syncTurnState(*currentMatch);
                }
            } 
            else if (lobbyManager.isLocalHost() && bots.count(currentIdx) == 0) {
                if (SDL_GetTicks() - turnStartTime > 35000) {
                    std::cout << "[SYSTEM] Ghost Client Detected! Host forcing skip for Player " << currentIdx << "\n";
                    if (!currentMatch->getPlayer(currentIdx).getHasDrawnThisTurn()) {
                        lobbyManager.pushMove(currentIdx, "D", -1, "");
                    }
                    lobbyManager.pushMove(currentIdx, "P", -1, "");
                    turnStartTime = SDL_GetTicks(); 
                }
                
                if (SDL_GetTicks() - turnStartTime > 45000) {
                    std::cout << "[FAILSAFE] Critical Stall Detected! Forcing Game State Forward.\n";
                    lobbyManager.pushMove(currentIdx, "FS", -1, ""); 
                    lobbyManager.syncTurnState(*currentMatch);
                    turnStartTime = SDL_GetTicks();
                }
            }
        } else if (currentState == AppState::PLAYING && currentMatch && currentMatch->isMatchOver()) {
            currentState = AppState::GAME_OVER;
            currentMatch->endMatchPointsCalc();
            bool tournamentOver = false;
            for (const Player& p : currentMatch->getPlayers()) {
                if (p.getScore() >= currentTargetScore) tournamentOver = true;
            }
            if (tournamentOver) window.playSound(Sfx::TOURNAMENT_END);
            else window.playSound(Sfx::ROUND_END);
        }

        if (currentMatch && myIndex != -1) {
            currentMatch->getPlayer(myIndex).sortHand(sortBySuit);
        }

        // --- 3. Pass dt to the Render State ---
        window.render(dt, currentState, currentMatch.get(), myIndex, myName, lobbyManager.getCode(), currentLobbyPlayers, publicLobbies, currentHostName, currentTargetScore, sortBySuit);
        
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