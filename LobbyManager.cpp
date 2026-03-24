#include "LobbyManager.h"
#include <iostream>
#include <random>
#include <ctime>
#include "match.h"

LobbyManager::LobbyManager(firebase::database::Database* database) : db(database) {
    srand(static_cast<unsigned int>(time(0)));
    
    // Wire up the internal listener to the public callback
    listener.onUpdate = [this](const firebase::database::DataSnapshot& snapshot) {
        if (!snapshot.exists()) return;

        LobbyData data;
        data.code = currentLobbyCode;
        data.status = snapshot.Child("status").value().string_value();
        data.isPrivate = snapshot.Child("is_private").value().bool_value();
        
        auto playersSnap = snapshot.Child("players");
        for (auto& child : playersSnap.children()) {
            PlayerInfo p;
            p.name = child.Child("name").value().string_value();
            p.isBot = child.Child("is_bot").value().bool_value();
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

    currentLobbyRef.SetValue(lobbyInit).OnCompletion([this, playerName](const firebase::Future<void>&) {
        // After creating, join yourself to the players list
        firebase::database::DatabaseReference p1 = currentLobbyRef.Child("players").PushChild();
        std::map<std::string, firebase::Variant> pData;
        pData["name"] = playerName;
        pData["is_bot"] = false;
        p1.SetValue(pData);

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
    firebase::database::DatabaseReference selfRef = currentLobbyRef.Child("players").PushChild();
    std::map<std::string, firebase::Variant> pData;
    pData["name"] = playerName;
    pData["is_bot"] = false;
    
    selfRef.SetValue(pData).OnCompletion([this](const firebase::Future<void>&) {
        currentLobbyRef.AddValueListener(&listener);
        std::cout << "Joined lobby " << currentLobbyCode << " successfully.\n";
    });
}

void LobbyManager::findPublicLobbies() {
    std::cout << "\nSearching for open games...\n";
    db->GetReference("lobbies")
      .OrderByChild("status")
      .EqualTo("waiting") // Safer than querying the boolean
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
          std::cout << "\nSelection: "; // Re-print prompt so the UI doesn't look frozen
      });
}

void LobbyManager::fillWithBots() {
    if (!isHost) return;

    currentLobbyRef.Child("players").GetValue().OnCompletion([this](const firebase::Future<firebase::database::DataSnapshot>& done) {
        auto& snapshot = *done.result();
        int currentCount = (int)snapshot.children_count();
        int botsNeeded = 4 - currentCount;

        for (int i = 0; i < botsNeeded; ++i) {
            auto botRef = currentLobbyRef.Child("players").PushChild();
            std::map<std::string, firebase::Variant> bData;
            bData["name"] = "Bot " + std::to_string(i + 1);
            bData["is_bot"] = true;
            bData["difficulty"] = "moderate";
            botRef.SetValue(bData);
        }
    });
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

    currentLobbyRef.Child("game_state").Child("lastMove").SetValue(move);
}

void LobbyManager::listenForMoves(std::function<void(int, std::string, int, std::string)> onMoveReceived) {
    moveListener.onMove = onMoveReceived;
    currentLobbyRef.Child("game_state").Child("lastMove").AddValueListener(&moveListener);
}

void LobbyManager::syncTurnState(const Match& match) {
    if (!isHost) return; // Optional: Or let any player update it if you prefer distributed authority. Let's let the player who just moved update it!
    
    // Actually, let's allow whoever just made a move to push the new state
    std::map<std::string, firebase::Variant> state;
    state["currentPlayerIndex"] = match.getCurrentPlayerIndex();
    state["cardsToDraw"] = match.getCardsToDraw();
    state["declaredSuit"] = match.getDeclaredSuit();
    state["topCard"] = match.getTopCard().toString();
    
    currentLobbyRef.Child("game_state").UpdateChildren(state);
}