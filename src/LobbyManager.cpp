#include "LobbyManager.h"
#include <iostream>
#include <random>
#include <ctime>
#include "match.h"

LobbyManager::LobbyManager(firebase::database::Database* database) : db(database) {
    srand(static_cast<unsigned int>(time(0)));
    
    // Wire up the internal listener to the public callback
    listener.onUpdate = [this](const firebase::database::DataSnapshot& snapshot) {
        if (!snapshot.exists()) {
            LobbyData deletedData;
            deletedData.status = "deleted"; 
            if (onLobbyUpdated) onLobbyUpdated(deletedData);
            return;
        }

        LobbyData data;
        data.code = currentLobbyCode;
        
        auto statusVal = snapshot.Child("status").value();
        data.status = statusVal.is_string() ? statusVal.string_value() : "";
        
        if (data.status.empty()) {
            LobbyData deletedData;
            deletedData.status = "deleted";
            if (onLobbyUpdated) onLobbyUpdated(deletedData);
            return;
        }

        auto privVal = snapshot.Child("is_private").value();
        data.isPrivate = privVal.is_bool() ? privVal.bool_value() : false;
        
        auto hostVal = snapshot.Child("host_name").value();
        data.hostName = hostVal.is_string() ? hostVal.string_value() : "Unknown";
        
        auto tScoreVal = snapshot.Child("target_score").value();
        data.targetScore = tScoreVal.is_int64() ? (int)tScoreVal.int64_value() : 50;

        auto playersSnap = snapshot.Child("players");
        for (auto& child : playersSnap.children()) {
            PlayerInfo p;
            auto nameVal = child.Child("name").value();
            p.name = nameVal.is_string() ? nameVal.string_value() : "Unknown";
            
            auto botVal = child.Child("is_bot").value();
            p.isBot = botVal.is_bool() ? botVal.bool_value() : false;
            
            data.players.push_back(p);
        }

        if (onLobbyUpdated) onLobbyUpdated(data);
    };
}

LobbyManager::~LobbyManager() {
    if (currentLobbyRef.is_valid()) {
        currentLobbyRef.RemoveValueListener(&listener);
    }
}

std::string LobbyManager::generateRandomCode(int length) {
    const std::string chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::string code = "";
    for (int i = 0; i < length; ++i) code += chars[rand() % chars.length()];
    return code;
}

void LobbyManager::setTargetScore(int score) {
    if (isHost && currentLobbyRef.is_valid()) {
        currentLobbyRef.Child("target_score").SetValue(score);
    }
}

void LobbyManager::createLobby(const std::string& playerName, bool isPrivate) {
    currentLobbyCode = generateRandomCode();
    localPlayerName = playerName;
    isHost = true;

    currentLobbyRef = db->GetReference("lobbies").Child(currentLobbyCode);

    currentLobbyRef.OnDisconnect()->RemoveValue();

    std::map<std::string, firebase::Variant> lobbyInit;
    lobbyInit["status"] = "waiting";
    lobbyInit["is_private"] = isPrivate;
    lobbyInit["host_name"] = playerName;
    lobbyInit["target_score"] = 50;

    currentLobbyRef.SetValue(lobbyInit).OnCompletion([this, playerName](const firebase::Future<void>&) {
        myPlayerRef = currentLobbyRef.Child("players").PushChild();
        std::map<std::string, firebase::Variant> pData;
        pData["name"] = playerName;
        pData["is_bot"] = false;
        myPlayerRef.SetValue(pData);
        currentLobbyRef.AddValueListener(&listener);
        std::cout << "Lobby created! Code: " << currentLobbyCode << "\n";
    });
}

void LobbyManager::joinLobby(const std::string& code, const std::string& playerName) {
    currentLobbyCode = code;
    localPlayerName = playerName;
    isHost = false;
    currentLobbyRef = db->GetReference("lobbies").Child(code);

    // Add self to players list
    myPlayerRef = currentLobbyRef.Child("players").PushChild();
    std::map<std::string, firebase::Variant> pData;
    pData["name"] = playerName;
    pData["is_bot"] = false;
    
    myPlayerRef.SetValue(pData).OnCompletion([this](const firebase::Future<void>&) {
        currentLobbyRef.AddValueListener(&listener);
        std::cout << "Joined lobby " << currentLobbyCode << " successfully.\n";
    });
    myPlayerRef.Child("is_bot").OnDisconnect()->SetValue(firebase::Variant(true));
}

void LobbyManager::findPublicLobbies() {
    std::cout << "\nSearching for open games...\n";
    db->GetReference("lobbies")
      .OrderByChild("status")
      .EqualTo("waiting")
      .GetValue()
      .OnCompletion([](const firebase::Future<firebase::database::DataSnapshot>& done) {
          if (done.error() == 0 && done.result()->exists()) {
              auto& snapshot = *done.result();
              bool found = false;
              for (auto& lobby : snapshot.children()) {
                  // Filter out private lobbies locally
                  if (!lobby.Child("is_private").value().bool_value()) {
                      std::cout << " -> Lobby [" << lobby.key() << "] | Host: " 
                                << lobby.Child("host_name").value().string_value() << "\n";
                      found = true;
                  }
              }
              if (!found) std::cout << "No public lobbies found.\n";
          } else {
              std::cout << "No open lobbies right now.\n";
          }
          std::cout << "\nSelection: ";
      });
}

void LobbyManager::fillWithBots(int currentCount) {
    if (!isHost) return;

    int botsNeeded = 4 - currentCount;
    if (botsNeeded <= 0) return; // Failsafe

    for (int i = 0; i < botsNeeded; ++i) {
        auto botRef = currentLobbyRef.Child("players").PushChild();
        std::map<std::string, firebase::Variant> bData;
        
        bData["name"] = "Player " + std::to_string(currentCount + i); 
        bData["is_bot"] = true;
        bData["difficulty"] = "moderate";
        
        botRef.SetValue(bData);
    }
}

void LobbyManager::startGame() {
    if (isHost) {
        currentLobbyRef.Child("status").SetValue("playing");
    }
}

void LobbyManager::syncInitialMatch(Match& match) {
    if (!isHost) return; 
    
    firebase::database::DatabaseReference gameRef = currentLobbyRef.Child("game_state");
    
    // 1. Serialize the Deck
    std::vector<std::string> serializedDeck = match.getDeck().serialize();
    std::vector<firebase::Variant> fbDeck;
    for (const auto& cardStr : serializedDeck) {
        fbDeck.push_back(firebase::Variant(cardStr));
    }

    // 2. Build the state map
    std::map<std::string, firebase::Variant> state;
    state["currentPlayerIndex"] = 0;
    state["cardsToDraw"] = 0;
    state["declaredSuit"] = "";
    state["deck"] = fbDeck; // Push the full 52-card deck
    
    // Wipe out any old moves from previous games if re-using a lobby code
    gameRef.Child("moves").RemoveValue(); 

    gameRef.UpdateChildren(state);
}

void LobbyManager::pushMove(int pIdx, std::string type, int cIdx, std::string suit) {
    std::map<std::string, firebase::Variant> move;
    move["player"] = pIdx;
    move["type"] = type;
    move["cardIndex"] = cIdx;
    move["suit"] = suit;
    std::map<std::string, firebase::Variant> timestampMap;
    timestampMap[".sv"] = "timestamp";
    move["timestamp"] = timestampMap;

    // THE BUG FIX: PushChild() creates a new list entry instead of overwriting a single node
    currentLobbyRef.Child("game_state").Child("moves").PushChild().SetValue(move);
}

void LobbyManager::listenForMoves(std::function<void(int, std::string, int, std::string)> onMoveReceived) {
    // THE FIX: Remove any existing listener before attaching a new one
    currentLobbyRef.Child("game_state").Child("moves").RemoveChildListener(&moveListener);
    
    moveListener.onMove = onMoveReceived;
    currentLobbyRef.Child("game_state").Child("moves").AddChildListener(&moveListener);
}

void LobbyManager::syncTurnState(const Match& match) {
    if (!isHost) return; 
    
    std::map<std::string, firebase::Variant> state;
    state["currentPlayerIndex"] = match.getCurrentPlayerIndex();
    state["cardsToDraw"] = match.getCardsToDraw();
    state["declaredSuit"] = match.getDeclaredSuit();
    state["topCard"] = match.getTopCard().toString();
    
    currentLobbyRef.Child("game_state").UpdateChildren(state);
}

void LobbyManager::leaveLobby() {
    if (currentLobbyRef.is_valid()) {
        currentLobbyRef.RemoveValueListener(&listener);
        currentLobbyRef.Child("game_state").Child("moves").RemoveChildListener(&moveListener);
        
        if (isHost) {
            currentLobbyRef.RemoveValue(); // Destroys the whole lobby
        } else if (myPlayerRef.is_valid()) {
            myPlayerRef.RemoveValue();     // Removes just the guest
        }
    }
    currentLobbyCode = "";
}

void LobbyManager::kickPlayer(const std::string& targetName) {
    if (!isHost || !currentLobbyRef.is_valid()) return;
    
    // Find the player by name and remove their node
    currentLobbyRef.Child("players").GetValue().OnCompletion([this, targetName](const firebase::Future<firebase::database::DataSnapshot>& done) {
        if (done.error() == 0 && done.result()->exists()) {
            for (auto& child : done.result()->children()) {
                if (child.Child("name").value().string_value() == targetName) {
                    currentLobbyRef.Child("players").Child(child.key()).RemoveValue();
                    break;
                }
            }
        }
    });
}