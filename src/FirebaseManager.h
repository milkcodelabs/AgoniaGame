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
    
    void setupListeners();

public:
    FirebaseManager(firebase::App* app, const std::string& dbUrl);
    
    void syncMatchState(const Match& match);
    
    std::function<void(const std::string&)> onRemoteMove; 
};