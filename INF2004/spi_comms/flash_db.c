#include "flash_db.h"
#include <stddef.h>
// y did i not plan to split them up from the beginning :( )

static const manufacturer_t manufacturer_db[] = {
    {0xBF, "SST / Microchip"},
    {0xEF, "Winbond"},
    {0xC8, "GigaDevice"},
    {0x20, "Micron / ST"},
    {0x01, "Spansion / Cypress"},
    {0xC2, "Macronix"},
    {0x1F, "Atmel / Adesto"},
    {0x9D, "ISSI"},
    {0x37, "AMIC"},
    {0x8C, "ESMT"},
    {0x85, "Puya"},
    {0xA1, "Fudan"},
    {0x0B, "XTX"},
    {0x68, "Boya"},
    {0x5E, "Zbit"},
};

const char *lookup_manufacturer(uint8_t id) {
  for (size_t i = 0; i < sizeof(manufacturer_db) / sizeof(manufacturer_db[0]);
       i++) {
    if (manufacturer_db[i].id == id) {
      return manufacturer_db[i].name;
    }
  }
  return "Unknown Manufacturer";
}
