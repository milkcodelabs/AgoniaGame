#pragma once
#include <string>
#include <vector>
#include <functional>
#include "firebase/app.h"
#include "firebase/database.h"
#include "Match.h"

class FirebaseManager {
private:
    firebase::database::Database* database;
    firebase::database::DatabaseReference matchRef;
    
    // Listeners for real-time updates
    void setupListeners();

public:
    FirebaseManager(firebase::App* app, const std::string& dbUrl);
    
    // Push the current local engine state to the cloud
    void syncMatchState(const Match& match);
    
    // Listener callback - we will trigger this when someone else moves
    std::function<void(const std::string&)> onRemoteMove; 
};