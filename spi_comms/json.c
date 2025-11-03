#include "json.h"
#include "flash_db.h"
#include "functions.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Small internal logger/escaper ---

static const char *json_escape(const char *s, char *tmp, size_t tmp_cap) {
  size_t w = 0;
  for (size_t i = 0; s[i] && w + 2 < tmp_cap; i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      tmp[w++] = '\\';
    }
    tmp[w++] = c;
  }
  tmp[w] = 0;
  return tmp;
}

static size_t appendf(char *dst, size_t cap, size_t *idx, const char *fmt,
                      ...) {
  if (*idx >= cap)
    return 0;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(dst + *idx, cap - *idx, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= cap - *idx) {
    *idx = cap;
    return 0;
  }
  *idx += (size_t)n;
  return (size_t)n;
}

// Compute byte capacity from JEDEC exponent-style code — returns 0 if unknown.
static uint64_t capacity_code_to_bytes(uint8_t code) {
  if (code >= 8 && code < 63) {
    return (1ULL << code);
  }
  return 0;
}

static void write_hex_array(char *dst, size_t cap, size_t *idx,
                            const uint8_t *buf, size_t n) {
  appendf(dst, cap, idx, "[");
  for (size_t i = 0; i < n; i++) {
    appendf(dst, cap, idx, "\"%02X\"%s", buf[i], (i + 1 < n ? "," : ""));
  }
  appendf(dst, cap, idx, "]");
}

// --- PUBLIC FUNCTION ---
size_t json_export_full_report(char *out, size_t cap, const uint8_t *report_buf,
                               size_t report_len) {
  if (!out || cap < 16)
    return 0;

  size_t idx = 0;
  appendf(out, cap, &idx, "{");

  // ---- DEVICE SUBSECTION (JEDEC) ----
  appendf(out, cap, &idx, "\"device\":{");

  uint8_t jedec_m = 0, jedec_t = 0, jedec_c = 0;
  uint8_t found_jedec = 0;

  char esc[128];
  size_t offset = 0;
  size_t count = get_safe_command_count();

  // First pass: locate JEDEC ID
  for (size_t i = 0; i < count; i++) {
    const opcode *cmd = get_command_by_index(i);
    if (!cmd)
      continue;
    if (offset + cmd->rx_data_len > report_len)
      break;

    if (cmd->opcode == 0x9F && cmd->rx_data_len >= 3) {
      jedec_m = report_buf[offset];
      jedec_t = report_buf[offset + 1];
      jedec_c = report_buf[offset + 2];
      found_jedec = 1;
    }
    offset += cmd->rx_data_len;
  }

  if (found_jedec) {
    const char *man = lookup_manufacturer(jedec_m);
    uint64_t cap_bytes = capacity_code_to_bytes(jedec_c);

    appendf(out, cap, &idx,
            "\"jedec\":{"
            "\"manufacturer_id\":\"%02X\","
            "\"manufacturer_name\":\"%s\","
            "\"memory_type\":\"%02X\","
            "\"capacity_code\":\"%02X\","
            "\"capacity_bytes\":\"%llu\""
            "}",
            jedec_m, json_escape(man, esc, sizeof esc), jedec_t, jedec_c,
            (unsigned long long)cap_bytes);
  }

  appendf(out, cap, &idx, "},"); // end "device"

  // ---- COMMANDS ARRAY ----
  appendf(out, cap, &idx, "\"commands\":[");

  offset = 0;
  for (size_t i = 0; i < count; i++) {
    const opcode *cmd = get_command_by_index(i);
    if (!cmd)
      break;
    if (offset + cmd->rx_data_len > report_len)
      break;

    appendf(out, cap, &idx, "{");

    appendf(
        out, cap, &idx, "\"name\":\"%s\",",
        json_escape(cmd->description ? cmd->description : "", esc, sizeof esc));
    appendf(out, cap, &idx, "\"opcode\":\"%02X\",", cmd->opcode);
    appendf(out, cap, &idx, "\"data\":");
    write_hex_array(out, cap, &idx, report_buf + offset, cmd->rx_data_len);

    appendf(out, cap, &idx, "}%s", (i + 1 < count ? "," : ""));
    offset += cmd->rx_data_len;
  }

  appendf(out, cap, &idx, "]"); // end array
  appendf(out, cap, &idx, "}"); // end root

  if (idx >= cap)
    return 0; // Buffer overflow/trunc

  return idx;
}
