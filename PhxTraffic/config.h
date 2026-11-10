#pragma once

// Provisional values copied from the repository's e-ink examples.
// Verify the panel controller, wiring, and orientation before using this profile.
#define PHXTRAFFIC_DISPLAY_DRIVER_GXEPD2_213_B74 1
#define PHXTRAFFIC_DISPLAY_ROTATION 1

#define PHXTRAFFIC_SPI_SCK_PIN 18
#define PHXTRAFFIC_SPI_MISO_PIN 19
#define PHXTRAFFIC_SPI_MOSI_PIN 23
#define PHXTRAFFIC_DISPLAY_CS_PIN 5
#define PHXTRAFFIC_DISPLAY_DC_PIN 17
#define PHXTRAFFIC_DISPLAY_RST_PIN 16
#define PHXTRAFFIC_DISPLAY_BUSY_PIN 4

// Broad Phoenix metro bounds; adjust to include or exclude nearby communities.
#define PHXTRAFFIC_MIN_LATITUDE 33.10
#define PHXTRAFFIC_MAX_LATITUDE 33.95
#define PHXTRAFFIC_MIN_LONGITUDE -112.85
#define PHXTRAFFIC_MAX_LONGITUDE -111.30

#define PHXTRAFFIC_REFRESH_INTERVAL_MS (5UL * 60UL * 1000UL)
#define PHXTRAFFIC_MAX_EVENTS 3
#define PHXTRAFFIC_AZ511_ENDPOINT "https://az511.com/api/v2/get/event"
#define PHXTRAFFIC_API_KEY_PLACEHOLDER "REPLACE_WITH_APPROVED_AZ511_KEY"
