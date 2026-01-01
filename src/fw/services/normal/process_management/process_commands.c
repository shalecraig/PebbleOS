/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "console/prompt.h"
#include "process_management/app_install_manager.h"
#include "process_management/app_manager.h"
#include "process_management/worker_manager.h"
#include "services/normal/app_cache.h"
#include "services/normal/blob_db/app_db.h"
#include "services/normal/filesystem/pfs.h"

//! @file process_commands.c
//!
//! Serial commands for process management

extern AppInstallId app_db_check_next_unique_id(void);

void command_app_remove(const char *id_str) {
  int32_t id = atoi(id_str);
  if (id == 0) {
    prompt_send_response("invalid app number");
    return;
  }

  AppInstallEntry entry;
  if (!app_install_get_entry_for_install_id(id, &entry)) {
    prompt_send_response("failed to get entry");
    return;
  }

  // should delete from blob db and fire off an event to AppInstallManager that does the rest
  app_db_delete((uint8_t *)&entry.uuid, sizeof(Uuid));
  prompt_send_response("OK");
}

bool prv_print_app_info(AppInstallEntry *entry, void *data) {
  if (app_install_id_from_system(entry->install_id)) {
    return true;
  }

  char buffer[120];

  char uuid_buffer[UUID_STRING_BUFFER_LENGTH];
  uuid_to_string(&entry->uuid, uuid_buffer);

  prompt_send_response_fmt(buffer, sizeof(buffer), "%"PRIi32": %s %s", entry->install_id,
      entry->name, uuid_buffer);
  return true;
}

void command_app_list(void) {
  app_install_enumerate_entries(prv_print_app_info, NULL);
}

void command_app_launch(const char *id_str) {
  int32_t id = atoi(id_str);
  if (id == 0) {
    prompt_send_response("invalid app number");
    return;
  }

  AppInstallEntry entry;
  bool success = app_install_get_entry_for_install_id(id, &entry);

  if (success) {
    app_manager_put_launch_app_event(&(AppLaunchEventConfig) { .id = id });
    prompt_send_response("OK");
  } else {
    prompt_send_response("No app with id");
  }
}

void command_worker_launch(const char *id_str) {
  int32_t id = atoi(id_str);
  if (id == 0) {
    prompt_send_response("invalid app number");
    return;
  }

  AppInstallEntry entry;
  bool success = app_install_get_entry_for_install_id(id, &entry);

  if (success && app_install_entry_has_worker(&entry)) {
    app_manager_put_launch_app_event(&(AppLaunchEventConfig) { .id = id });
    prompt_send_response("OK");
  } else {
    prompt_send_response("No worker with id");
  }
}

void command_app_status(const char *id_str) {
  int32_t id = atoi(id_str);
  if (id == 0) {
    prompt_send_response("invalid app number");
    return;
  }

  char buffer[128];

  // Check if app is installed
  AppInstallEntry entry;
  bool installed = app_install_get_entry_for_install_id(id, &entry);

  if (!installed) {
    prompt_send_response("NOT_INSTALLED");
    return;
  }

  // Check if app is currently running
  bool app_running = app_install_is_app_running(id);
  bool worker_running = app_install_is_worker_running(id);

  if (app_running || worker_running) {
    const char *type = app_running ? "app" : "worker";
    prompt_send_response_fmt(buffer, sizeof(buffer), "RUNNING %s %s", type, entry.name);
  } else {
    // App is installed but not running - could be idle or crashed previously
    prompt_send_response_fmt(buffer, sizeof(buffer), "INSTALLED %s", entry.name);
  }
}

void command_app_clear_db(void) {
  // Clear all 3rd party apps from the database and cache.
  // This is useful for ensuring a fresh state before reinstalling apps
  // in the emulator without having to restart QEMU.
  app_install_clear_app_db();
  prompt_send_response("OK");
}
