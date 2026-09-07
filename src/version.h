#pragma once

// Bump this with every release you intend to publish an OTA update for.
// checkForUpdate() (see api/update_check.h) compares this against the
// "version" field in the hosted manifest JSON to decide whether an update
// is actually newer - so this string has to change for the update prompt
// to ever trigger.
#define FIRMWARE_VERSION "1.0.8"