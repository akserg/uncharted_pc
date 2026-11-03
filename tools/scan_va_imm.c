/* M5 triage: disassemble a range of a raw linked image, or list immediates/displacements that derive from the PS4
 * user base 0x1000000000 (Zydis linear sweep of the executable segments).
 *  usage: scan-va-imm dis image.raw START END
 *         scan-va-imm imm image.raw start:size [start:size ...]
 * image.raw is the image part of out/<exe>/boot-linked.bin (see tools/scan_va_imm.py). */
#include <Zydis/Zydis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static unsigned char *img; static long n;
static void fmt(ZydisDecodedInstruction *in, ZydisDecodedOperand *ops, uint64_t at) {
  char buf[160]; ZydisFormatter f; ZydisFormatterInit(&f, ZYDIS_FORMATTER_STYLE_INTEL);
  ZydisFormatterFormatInstruction(&f, in, ops, in->operand_count_visible, buf, sizeof buf, at, NULL);
  printf("%#8llx: ", (unsigned long long)at);
  for (int b = 0; b < in->length && b < 10; ++b) printf("%02x", img[at + b]);
  printf("%*s%s\n", 22 - 2 * (in->length < 10 ? in->length : 10), "", buf);
}
int main(int argc, char **argv) {
  FILE *f = fopen(argv[2], "rb"); fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
  img = malloc(n); if (fread(img, 1, n, f) != (size_t)n) return 1; fclose(f);
  ZydisDecoder d; ZydisDecoderInit(&d, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  if (!strcmp(argv[1], "dis")) {
    uint64_t a = strtoull(argv[3], 0, 0), e = strtoull(argv[4], 0, 0);
    while (a < e) { ZydisDecodedInstruction in; ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
      if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&d, img + a, 15, &in, ops))) { printf("%#llx: ??\n", (unsigned long long)a); ++a; continue; }
      fmt(&in, ops, a); a += in.length; }
    return 0;
  }
  for (int s = 3; s < argc; ++s) {
    uint64_t st = strtoull(argv[s], 0, 0); char *c = strchr(argv[s], ':'); uint64_t sz = strtoull(c + 1, 0, 0);
    for (uint64_t off = 0; off < sz;) {
      ZydisDecodedInstruction in; ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
      if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&d, img + st + off, sz - off < 15 ? sz - off : 15, &in, ops))) { ++off; continue; }
      int hit = 0;
      for (int i = 0; i < in.operand_count_visible; ++i) {
        if (ops[i].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
          uint64_t v = ops[i].imm.value.u;
          /* 0x10'0000'0000 itself, 0x10 with a 32-bit shift context, 0x1'0000'0000 (4 GiB), or 0x7f'ffff'ffff-style masks */
          if (v == 0x1000000000ull || v == 0x10ull * 0x100000000ull || v == 0x2000000000ull || v == 0x7000000000ull ||
              (v >= 0x1000000000ull && v < 0x8000000000ull)) hit = 1;
        } else if (ops[i].type == ZYDIS_OPERAND_TYPE_MEMORY && ops[i].mem.disp.has_displacement && ops[i].mem.base != ZYDIS_REGISTER_RIP) {
          uint64_t v = (uint64_t)ops[i].mem.disp.value;
          if (v >= 0x1000000000ull && v < 0x8000000000ull) hit = 1;
        }
      }
      /* shifts by 32..40 (an address >> 36 or 0x10 << 32 style computation) */
      if ((in.mnemonic == ZYDIS_MNEMONIC_SHL || in.mnemonic == ZYDIS_MNEMONIC_SHR || in.mnemonic == ZYDIS_MNEMONIC_SAR) &&
          in.operand_count_visible >= 2 && ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && ops[1].imm.value.u >= 32 && ops[1].imm.value.u <= 40) hit = 1;
      if (hit) fmt(&in, ops, st + off);
      off += in.length;
    }
  }
  return 0;
}
