#pragma once

void pets_init();
void setup_pets_properties();
void handle_open_pets();
void handle_pets_browse(int page);
void handle_pet_download(int index);
void handle_pet_apply();
void handle_pet_toggle();
void handle_pets_back();
bool pets_requires_restart();
