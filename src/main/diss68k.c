/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "generator.h"
#include "cpu68k.h"

/* forward references */

void diss68k_getoperand(char *text, t_ipc *ipc, t_iib *iib, t_type type);

/* functions */

int diss68k_gettext(t_ipc *ipc, char *text)
{
  t_iib *iib;
  char *p, *c;
  char src[64], dst[64];
  char mnemonic[64];

  *text = '\0';

  iib = cpu68k_iibtable[ipc->opcode];

  if (iib == nullptr)
    return 0;

  diss68k_getoperand(dst, ipc, iib, tp_dst);
  diss68k_getoperand(src, ipc, iib, tp_src);

  if ((iib->mnemonic == i_Bcc) || (iib->mnemonic == i_BSR) ||
      (iib->mnemonic == i_DBcc)) {
    snprintf(src, sizeof(src), "$%08x", ipc->src);
  }

  snprintf(mnemonic, sizeof(mnemonic), "%s",
           mnemonic_table[iib->mnemonic].name);

  if ((p = strstr(mnemonic, "cc")) != nullptr) {
    if (iib->mnemonic == i_Bcc && iib->cc == 0) {
      p[0] = 'R';
      p[1] = 'A';
    } else {
      c = condition_table[iib->cc];
      /* Safe: condition codes are 2 chars max, replacing "cc" */
      size_t remaining = sizeof(mnemonic) - (p - mnemonic);
      snprintf(p, remaining, "%s", c);
    }
  }

  /* Append size suffix - mnemonic is max ~10 chars, plenty of room */
  size_t len = strlen(mnemonic);
  switch (iib->size) {
  case sz_byte:
    snprintf(mnemonic + len, sizeof(mnemonic) - len, ".B");
    break;
  case sz_word:
    snprintf(mnemonic + len, sizeof(mnemonic) - len, ".W");
    break;
  case sz_long:
    snprintf(mnemonic + len, sizeof(mnemonic) - len, ".L");
    break;
  default:
    break;
  }

  /* text buffer assumed to be at least 128 bytes (typical usage) */
  snprintf(text, 128, "%-10s %s%s%s", mnemonic, src, dst[0] ? "," : "", dst);

  return 1;
}

/* text buffer is 64 bytes (from caller) */
#define OPERAND_SIZE 64

void diss68k_getoperand(char *text, t_ipc *ipc, t_iib *iib, t_type type)
{
  int bitpos;
  uint32 val;

  if (type == tp_src) {
    bitpos = iib->sbitpos;
    val = ipc->src;
  } else {
    bitpos = iib->dbitpos;
    val = ipc->dst;
  }

  switch (type == tp_src ? iib->stype : iib->dtype) {
  case dt_Dreg:
    snprintf(text, OPERAND_SIZE, "D%d", (ipc->opcode >> bitpos) & 7);
    break;
  case dt_Areg:
    snprintf(text, OPERAND_SIZE, "A%d", (ipc->opcode >> bitpos) & 7);
    break;
  case dt_Aind:
    snprintf(text, OPERAND_SIZE, "(A%d)", (ipc->opcode >> bitpos) & 7);
    break;
  case dt_Ainc:
    snprintf(text, OPERAND_SIZE, "(A%d)+", (ipc->opcode >> bitpos) & 7);
    break;
  case dt_Adec:
    snprintf(text, OPERAND_SIZE, "-(A%d)", (ipc->opcode >> bitpos) & 7);
    break;
  case dt_Adis:
    snprintf(text, OPERAND_SIZE, "$%04x(A%d)", (uint16)val,
             (ipc->opcode >> bitpos) & 7);
    break;
  case dt_Aidx:
    snprintf(text, OPERAND_SIZE, "$%02x(A%d,Rx.X)", (uint8)val,
             (ipc->opcode >> bitpos) & 7);
    break;
  case dt_AbsW:
    snprintf(text, OPERAND_SIZE, "$%08x", val);
    break;
  case dt_AbsL:
    snprintf(text, OPERAND_SIZE, "$%08x", val);
    break;
  case dt_Pdis:
    snprintf(text, OPERAND_SIZE, "$%08x(pc)", val);
    break;
  case dt_Pidx:
    snprintf(text, OPERAND_SIZE, "$%08x(pc, Rx.X)", val);
    break;
  case dt_ImmB:
    snprintf(text, OPERAND_SIZE, "#$%02x", val);
    break;
  case dt_ImmW:
    snprintf(text, OPERAND_SIZE, "#$%04x", val);
    break;
  case dt_ImmL:
    snprintf(text, OPERAND_SIZE, "#$%08x", val);
    break;
  case dt_ImmS:
    snprintf(text, OPERAND_SIZE, "#%d", iib->immvalue);
    break;
  case dt_Imm3:
    snprintf(text, OPERAND_SIZE, "#%d", (ipc->opcode >> bitpos) & 7);
    break;
  case dt_Imm4:
    snprintf(text, OPERAND_SIZE, "#%d", (ipc->opcode >> bitpos) & 15);
    break;
  case dt_Imm8:
    snprintf(text, OPERAND_SIZE, "#%d", (ipc->opcode >> bitpos) & 255);
    break;
  case dt_Imm8s:
    snprintf(text, OPERAND_SIZE, "#%d", (sint32)val);
    break;
  default:
    *text = '\0';
    break;
  }
}

/* dumpline buffer should be at least 256 bytes */
#define DUMPLINE_SIZE 256

int diss68k_getdumpline(uint32 addr68k, uint8 *addr, char *dumpline)
{
  t_ipc ipc;
  t_iib *iibp = cpu68k_iibtable[LOCENDIAN16(*(uint16 *)addr)];
  int words, i;
  char dissline[128];
  size_t pos = 0;

  if (addr68k < 256) {
    snprintf(dissline, sizeof(dissline), "dc.l $%08x",
             LOCENDIAN32(*(uint32 *)addr));
    words = 2;
  } else {
    cpu68k_ipc(addr68k, addr, iibp, &ipc);
    if (!diss68k_gettext(&ipc, dissline))
      snprintf(dissline, sizeof(dissline), "Illegal Instruction");
    words = ipc.wordlen;
  }

  pos += snprintf(dumpline + pos, DUMPLINE_SIZE - pos, "%6x : %04x ", addr68k,
                  (addr[0] << 8) + addr[1]);
  for (i = 1; i < words && pos < DUMPLINE_SIZE - 6; i++) {
    pos +=
        snprintf(dumpline + pos, DUMPLINE_SIZE - pos, "%04x ",
                 (addr[i * 2] << 8) + addr[i * 2 + 1]);
  }
  /* Pad to column 29 */
  while (pos < 29 && pos < DUMPLINE_SIZE - 1) {
    dumpline[pos++] = ' ';
  }
  pos += snprintf(dumpline + pos, DUMPLINE_SIZE - pos, ": ");
  /* ASCII dump */
  for (i = 0; i < words && pos < DUMPLINE_SIZE - 2; i++) {
    dumpline[pos++] = isalnum(addr[i * 2]) ? addr[i * 2] : '.';
    dumpline[pos++] = isalnum(addr[i * 2 + 1]) ? addr[i * 2 + 1] : '.';
  }
  /* Pad to column 39 */
  while (pos < 39 && pos < DUMPLINE_SIZE - 1) {
    dumpline[pos++] = ' ';
  }
  dumpline[pos] = '\0';

  if (iibp) {
    snprintf(dumpline + pos, DUMPLINE_SIZE - pos, " : %4d : %s\n",
             iibp->funcnum, dissline);
  } else {
    snprintf(dumpline + pos, DUMPLINE_SIZE - pos, " :      : %s\n", dissline);
  }

  return words;
}
