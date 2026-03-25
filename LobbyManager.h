#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include "firebase/database.h"

class Match;

struct PlayerInfo {
    std::string name;
    bool isBot;
    std::string difficulty;
};

struct LobbyData {
    std::string code;
    std::string hostId;
    std::string status; // "waiting" or "playing"
    std::string hostName;
    int targetScore = 50;
    bool isPrivate;
    std::vector<PlayerInfo> players;
};

class LobbyManager {
private:
    firebase::database::Database* db;
    firebase::database::DatabaseReference currentLobbyRef;
    firebase::database::DatabaseReference myPlayerRef;
    std::string currentLobbyCode;
    std::string localPlayerName;
    bool isHost = false;

    // Firebase Listener Class
    class LobbyValueListener : public firebase::database::ValueListener {
    public:
        std::function<void(const firebase::database::DataSnapshot&)> onUpdate;
        void OnValueChanged(const firebase::database::DataSnapshot& snapshot) override {
            if (onUpdate) onUpdate(snapshot);
        }
        void OnCancelled(const firebase::database::Error& error, const char* message) override {}
    };

    LobbyValueListener listener;
    std::string generateRandomCode(int length = 4);

    // THE BUG FIX: Change "ChildEventListener" to just "ChildListener"
    class MoveChildListener : public firebase::database::ChildListener {
    public:
        std::function<void(int, std::string, int, std::string)> onMove;
        
        void OnChildAdded(const firebase::database::DataSnapshot& snapshot, const char* previous_sibling) override {
            if (!snapshot.exists() || !snapshot.has_children()) return;
            
            // Safe parsing for Firebase numbers
            auto pVal = snapshot.Child("player").value();
            int pIdx = pVal.is_int64() ? (int)pVal.int64_value() : (pVal.is_double() ? (int)pVal.double_value() : -1);
            
            auto cVal = snapshot.Child("cardIndex").value();
            int cIdx = cVal.is_int64() ? (int)cVal.int64_value() : (cVal.is_double() ? (int)cVal.double_value() : -1);

            std::string type = snapshot.Child("type").value().string_value();
            std::string suit = snapshot.Child("suit").value().string_value();
            
            if (onMove && pIdx != -1) onMove(pIdx, type, cIdx, suit);
        }
        void OnChildChanged(const firebase::database::DataSnapshot& snapshot, const char* previous_sibling) override {}
        void OnChildRemoved(const firebase::database::DataSnapshot& snapshot) override {}
        void OnChildMoved(const firebase::database::DataSnapshot& snapshot, const char* previous_sibling) override {}
        void OnCancelled(const firebase::database::Error& error, const char* message) override {}
    };
    
    MoveChildListener moveListener;

public:
    LobbyManager(firebase::database::Database* database);
    ~LobbyManager();

    // Callbacks for the UI/Main loop
    std::function<void(const LobbyData&)> onLobbyUpdated;

    void setTargetScore(int score);
    void createLobby(const std::string& playerName, bool isPrivate);
    void joinLobby(const std::string& code, const std::string& playerName);
    void findPublicLobbies();
    void fillWithBots(int currentCount);
    void startGame();
    void syncInitialMatch(Match& match); 
    void pushMove(int playerIndex, std::string type, int cardIndex = -1, std::string declaredSuit = "");
    void listenForMoves(std::function<void(int, std::string, int, std::string)> onMoveReceived);
    void leaveLobby();
    void kickPlayer(const std::string& targetName);

    bool isLocalHost() const { return isHost; }
    std::string getCode() const { return currentLobbyCode; }
    void syncTurnState(const Match& match);
};