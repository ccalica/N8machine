/**
 * N8 keyboard constants and key injection parsing.
 */

export const NAMED_KEYS = {
  enter: 0x0D, return: 0x0D, cr: 0x0D,
  backspace: 0x08, bs: 0x08,
  tab: 0x09,
  esc: 0x1B, escape: 0x1B,
  delete: 0x0F, del: 0x0F,
  space: 0x20,
  up: 0x01, down: 0x02, left: 0x03, right: 0x04,
  home: 0x05, end: 0x06, pageup: 0x0A, pagedown: 0x0B, insert: 0x0E,
  printscreen: 0x10, pause: 0x11,
  f1: 0x80, f2: 0x81, f3: 0x82, f4: 0x83, f5: 0x84, f6: 0x85,
  f7: 0x86, f8: 0x87, f9: 0x88, f10: 0x89, f11: 0x8A, f12: 0x8B,
};

/**
 * Convert a printable ASCII character to an N8 keycode.
 * @param {string} ch - Single character
 * @returns {{ keycode: number, modifiers: number }|null}
 */
export function charToKeycode(ch) {
  const code = ch.charCodeAt(0);
  if (code >= 0x20 && code <= 0x7E) {
    return { keycode: code, modifiers: 0x00 };
  }
  return null;
}

/**
 * Parse an inline keyboard injection string into key events.
 * Syntax: bare text + [named_key] or [0xNN] sequences.
 * Example: "go north[enter]" → [{keycode: 0x67, mod: 0}, ..., {keycode: 0x0D, mod: 0}]
 *
 * @param {string} input - Inline key string
 * @param {number} [extraMod=0] - Extra modifier flags to OR into each key
 * @param {function} [parseAddr] - Address parser for [0xNN] syntax
 * @returns {{ keys: Array<{keycode: number, modifiers: number}>, error: string|null }}
 */
export function parseKeyInput(input, extraMod = 0, parseAddr) {
  const keys = [];
  let i = 0;
  while (i < input.length) {
    if (input[i] === '\\' && i + 1 < input.length && input[i + 1] === 'n') {
      keys.push({ keycode: 0x0D, modifiers: extraMod });
      i += 2;
    } else if (input[i] === '[') {
      const close = input.indexOf(']', i + 1);
      if (close === -1) {
        const kc = charToKeycode(input[i]);
        if (!kc) return { keys: [], error: `Cannot map character: ${input[i]}` };
        keys.push({ keycode: kc.keycode, modifiers: kc.modifiers | extraMod });
        i++;
        continue;
      }
      const tag = input.slice(i + 1, close);
      const tagLower = tag.toLowerCase();
      if (tagLower in NAMED_KEYS) {
        keys.push({ keycode: NAMED_KEYS[tagLower], modifiers: extraMod });
      } else {
        // Only accept explicit hex: 0x, $, or all-hex digits
        let addr = NaN;
        if (parseAddr) {
          addr = parseAddr(tag);
        } else if (/^(0[xX]|\$)[0-9a-fA-F]+$/.test(tag)) {
          addr = parseInt(tag.replace(/^(\$|0[xX])/, ''), 16);
        } else if (/^[0-9a-fA-F]+$/.test(tag)) {
          addr = parseInt(tag, 16);
        }
        if (!isNaN(addr) && addr <= 0xFF) {
          keys.push({ keycode: addr, modifiers: extraMod });
        } else {
          return { keys: [], error: `Unknown key name: [${tag}]` };
        }
      }
      i = close + 1;
    } else {
      const kc = charToKeycode(input[i]);
      if (!kc) return { keys: [], error: `Cannot map character: ${input[i]}` };
      keys.push({ keycode: kc.keycode, modifiers: kc.modifiers | extraMod });
      i++;
    }
  }
  return { keys, error: null };
}
