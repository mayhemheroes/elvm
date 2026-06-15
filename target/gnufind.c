#include <assert.h>
#include <ir/ir.h>
#include <stdlib.h>
#include <string.h>
#include <target/util.h>

#define GF_DEBUG_LOG "/tmp/dbg.log "
#define GF_DEBUG_FLAG_REGS_PER_INST 1
#define GF_DEBUG_FLAG_PC_PER_INST 2
#define GF_DEBUG_FLAG_DUMP_INST 4
// Change it for debugging.
#define GF_DEBUG_FLAGS 0

#define GF_EXEC "-exec find "
#define GF_EXEC_END "\\; "
#define GF_EXEC_END_INNER "{} "

#define GF_WORD_SIZE 24

#define GF_BASE_LOG 1
#define GF_BASE (1 << GF_BASE_LOG)
#define GF_REG_SIZE (GF_WORD_SIZE / GF_BASE_LOG)
#define GF_MEM_LOWER_SIZE (GF_REG_SIZE / 2)
#define GF_MEM_UPPER_SIZE (GF_REG_SIZE - GF_MEM_LOWER_SIZE)
#define GF_MEM_LOWER (1 << GF_BASE_LOG * GF_MEM_LOWER_SIZE)
#define GF_MEM_UPPER (1 << GF_BASE_LOG * GF_MEM_UPPER_SIZE)
#define GF_MEM_STEP_SIZE 4
#define GF_MEM_STEP (1 << GF_BASE_LOG * GF_MEM_STEP_SIZE)
#define GF_MEM_LOWER_STAGE_MAX (GF_MEM_LOWER_SIZE / GF_MEM_STEP_SIZE)
#define GF_MEM_UPPER_STAGE_MAX (GF_MEM_UPPER_SIZE / GF_MEM_STEP_SIZE - 1)
#define GF_MEM_LAST_STAGE                                                      \
  (GF_MEM_LOWER_STAGE_MAX + 1 + GF_MEM_UPPER_STAGE_MAX + 1)
#define GF_MEM_STAGE_COUNT (GF_MEM_LAST_STAGE + 1)

#define GF_SEMI "\\;"
#define GF_DOT_SLASH_SEMI "./" GF_SEMI
#define GF_PUTC_FLAG_PREFIX "p"
#define GF_REG_PREFIX "r"
#define GF_MEM_PREFIX "m."
#define GF_MEM_SUFFIX ".m"
#define GF_MEM_LOWER_PREFIX "l"
#define GF_MEM_UPPER_PREFIX "u"
#define GF_TMP_PREFIX "t."
#define GF_CONST_PREFIX "n"
#define GF_PC_PREFIX "z"
#define GF_INPUT "input"
#define GF_LOOP "loop"
#define GF_EOF "e"
#define GF_SEP "s"
#define GF_INDEX_START 'A'
#define GF_INDEX_END ((char)(GF_INDEX_START + GF_REG_SIZE))
#define GF_CARRY "c"
#define GF_ARITH_DST "a"
#define GF_ARITH_SRC "b"
#define GF_CMP_DST "x"
#define GF_CMP_SRC "y"

// Touch it to trigger load operations. Touched in LOAD instruction.
#define GF_MEM_LOAD_FLAG GF_MEM_PREFIX "l"
// Touch it to trigger store operations. Touched in STORE instruction.
#define GF_MEM_STORE_FLAG GF_MEM_PREFIX "s"
// Touch it to trigger mem operations. Touched in LOAD/STORE instruction.
#define GF_MEM_FLAG GF_MEM_PREFIX "f"
// Specifies the value to store.
// Created in STORE instruction with size representing the digit.
#define GF_MEM_STORE_VAL GF_MEM_PREFIX "v"
// Specifies the upper value for src.
// Created in STORE instruction with size representing the digit.
#define GF_MEM_STORE_UPPER GF_MEM_PREFIX "u"
// Specifies to which register the value is loaded.
// Created in LOAD instruction.
#define GF_MEM_LOAD_DST GF_MEM_PREFIX "d"

#define GF_MEM_STORE_DST GF_MEM_PREFIX "a"
#define GF_MEM_STORE_SRC GF_MEM_PREFIX "b"
#define GF_MEM_LOAD_SRC GF_MEM_PREFIX "c"

#define GF_REG_COUNT 6
#define GF_REG_WITH_TMP_COUNT 8
#define GF_REG_FULL_COUNT 10
#define GF_REG_TMP_ID 6
#define GF_REG_TMP GF_REG_PREFIX "6"
#define GF_REG_TMP2_ID 7
#define GF_REG_TMP2 GF_REG_PREFIX "7"
#define GF_REG_CMP_ID 8
#define GF_REG_CMP GF_REG_PREFIX "8"
#define GF_REG_CMP2_ID 9
#define GF_REG_CMP2 GF_REG_PREFIX "9"

#define GF_TMP1 GF_TMP_PREFIX "1"
#define GF_TMP_FLAG GF_TMP_PREFIX "f"

#define GF_MEM_FREQ 10

#define GF_SEMI_MARKER_NORMAL 0
#define GF_SEMI_MARKER_MEM 1
#define GF_SEMI_MARKER_INST_STORE 2
#define GF_SEMI_MARKER_INST_LOAD 3
#define GF_SEMI_MARKER_INST_ADD 4
#define GF_SEMI_MARKER_INST_SUB 5
#define GF_SEMI_MARKER_INST_EQ 6
#define GF_SEMI_MARKER_INST_NE 7
#define GF_SEMI_MARKER_INST_LT 8
#define GF_SEMI_MARKER_INST_GT 9
#define GF_SEMI_MARKER_INST_LE 10
#define GF_SEMI_MARKER_INST_GE 11
#define GF_MEM_MARKER 1

// For the NORMAL instruction dispatch the semi file's byte size encodes the
// current pc as (pc + GF_PC_OFFSET). The offset keeps a pc-encoded size clear of
// the small delegate-op markers (<= GF_SEMI_MARKER_INST_GE), so a size of
// exactly 0 still unambiguously means "instruction finished, rebuild the pc
// encoding" and any size >= GF_PC_OFFSET means "ready to dispatch pc".
#define GF_PC_OFFSET (GF_SEMI_MARKER_INST_GE + 1)

#define GF_CONST_FILE_MAX_ABS 100

int g_gf_pc_bits = 0;

static void gf_check() {
  assert(strlen(GF_PUTC_FLAG_PREFIX) == 1);

  assert(GF_MEM_UPPER_SIZE > 0);
  assert(GF_REG_SIZE <= 26);
  assert(8 % GF_BASE_LOG == 0);
  assert(GF_BASE < 10);
  assert(GF_MEM_LOWER_SIZE % GF_MEM_STEP_SIZE == 0);
  assert(GF_MEM_UPPER_SIZE % GF_MEM_STEP_SIZE == 0);
}

static char *gf_exec_end(bool inner) {
  return inner ? GF_EXEC_END_INNER : GF_EXEC_END;
}

static char *gf_reg_name(int r) { return format(GF_REG_PREFIX "%d", r); }

static char *gf_digit_idx_part(int idx) {
  return format("%c", GF_INDEX_START + idx);
}

static char *gf_pc_file(int idx) {
  assert(idx < GF_WORD_SIZE);
  return format(GF_PC_PREFIX "%s", gf_digit_idx_part(idx));
}

static void gf_emit_exists(const char *name, bool inner) {
  emit_str("-exec find %s -quit %s ", name, gf_exec_end(inner));
}

static char *gf_num_as_path(int b) {
  static char buf[100];

  int p = 0;
  buf[p++] = '.';
  buf[p++] = '/';
  for (; b > 0; b >>= 1) {
    if (b % 2) {
      buf[p++] = '.';
      buf[p++] = '/';
    } else {
      buf[p++] = '/';
      buf[p++] = '.';
      buf[p++] = '/';
    }
  }
  buf[p++] = '/';
  buf[p++] = '/';
  buf[p++] = 0;
  assert(p <= 100);
  return buf;
}

#if GF_DEBUG_FLAGS
#pragma GCC diagnostic ignored "-Wunused-function"

static void gf_debug_impl(char *sh, bool inner) {
  emit_str("\\( -exec sh -c \"( %s ) >> %s\" %s , -true \\) ", sh, GF_DEBUG_LOG,
           gf_exec_end(inner));
}

static void gf_debug_inner(char *sh) { gf_debug_impl(sh, true); }

static void gf_debug(char *sh) { gf_debug_impl(sh, false); }

static void gf_debug_outer(char *sh) {
  emit_str("find ");
  gf_debug(sh);
  emit_line("-quit");
}

static void gf_dump_inst(Inst *inst) {
  char *buf;
  size_t size;
  FILE *fp = open_memstream(&buf, &size);
  dump_inst_fp(inst, fp);
  fclose(fp);
  gf_debug(format("echo -n '%s'", buf));
  free(buf);
}

static void gf_dump_pc(int real_pc) {
  gf_debug(format("echo -n 'real_pc = %d ; pc = 0b'", real_pc));
  for (int i = g_gf_pc_bits - 1; i >= 0; i--) {
    char *c = gf_pc_file(i);
    emit_str("\\( ");

    gf_emit_exists(c, false);
    gf_debug("echo -n 1");
    emit_str("-o ");
    gf_debug("echo -n 0");

    emit_str("\\) ");
  }
  gf_debug("echo");
}

static void gf_dump_regs() {
  static const char *s[] = {"A", "B", "C", "D", "BP", "SP"};
  for (int r = 0; r < GF_REG_COUNT; r++) {
    char *name = gf_reg_name(r);

    gf_debug(format("echo -n '%s(%s) = rev( '", name, s[r]));

    emit_str(GF_EXEC "-files0-from %s ", name);
    for (int i = 0; i < GF_BASE; i++) {
      if (i) {
        emit_str("-o ");
      }
      emit_str("-path '%s*' ", gf_num_as_path(i));
      gf_debug_inner(format("echo -n %d", i));
    }
    emit_str(GF_EXEC_END);

    gf_debug("echo ' )'");
  }
}

#endif // GF_DEBUG_FLAGS

static char *gf_putc_flag_name(int c) {
  static char buf[20];
  int p = 0;
  buf[p++] = GF_PUTC_FLAG_PREFIX[0];
  for (int i = 0; i < 8 / GF_BASE_LOG; i++) {
    int v = (c & (GF_BASE - 1));
    if (GF_BASE > 10) {
      buf[p++] = '0' + (v / 10);
    }
    buf[p++] = '0' + (v % 10);
    c >>= GF_BASE_LOG;
  }
  buf[p++] = 0;
  return buf;
}

static char *gf_ones(int n) {
  static char buf[100];
  int p = 0;
  for (int i = 0; i < n; i++) {
    buf[p++] = '1';
  }
  buf[p++] = 0;
  return buf;
}

static char *gf_digit(int idx, int b) {
  assert(idx < GF_REG_SIZE);
  assert(b < GF_BASE);

  return format("%s%s", gf_num_as_path(b), gf_digit_idx_part(idx));
}

static char *gf_mem_store_value(int idx) {
  return format(GF_MEM_STORE_VAL "%d", idx);
}

static char *gf_mem_store_upper(int idx) {
  return format(GF_MEM_STORE_UPPER "%d", idx);
}

static char *gf_mem_load_src(int r) {
  assert(r < GF_REG_WITH_TMP_COUNT);
  return format(GF_MEM_LOAD_SRC "%d", r);
}

static char *gf_mem_load_dst(int r) {
  assert(r < GF_REG_COUNT);
  return format(GF_MEM_LOAD_DST "%d", r);
}

static void gf_emit_cur_is_digit_value(int b) {
  emit_str("-path '%s*' ", gf_num_as_path(b));
}

static void gf_emit_imm(int val) {
  emit_str("'");
  for (int i = 0; i < GF_REG_SIZE; i++) {
    int b = val & (GF_BASE - 1);
    emit_str("%s\\0", gf_digit(i, b));
    val >>= GF_BASE_LOG;
  }
  emit_str("' ");
}

static char *gf_const_file(int val) {
  return format(GF_CONST_PREFIX "%x", val);
}

static void gf_emit_make_const(int val) {
  emit_str("-fprintf %s ", gf_const_file(val));
  gf_emit_imm(val);
  emit_str(" ");
}

static char *gf_lower_or_upper(bool lower, int value, int start, int len,
                               bool prefix) {
  static char buf[100];
  int p = 0;

  int size = lower ? GF_MEM_LOWER_SIZE : GF_MEM_UPPER_SIZE;
  assert(start + len <= size);

  if (prefix) {
    p += sprintf(buf + p, lower ? GF_MEM_LOWER_PREFIX : GF_MEM_UPPER_PREFIX);
  }
  for (int i = start; i < start + len; i++) {
    p += sprintf(buf + p, "%s%d",
                 gf_digit_idx_part(i + (lower ? 0 : GF_MEM_LOWER_SIZE)),
                 value & (GF_BASE - 1));
    value >>= GF_BASE_LOG;
  }
  buf[p] = 0;

  assert(p < 100);

  return buf;
}

static void gf_emit_lower_or_upper(bool lower, int value, int start, int len,
                                   bool prefix) {
  emit_str("%s", gf_lower_or_upper(lower, value, start, len, prefix));
}

static char *gf_mem_file(int dst) {
  const char *l = gf_lower_or_upper(true, dst, 0, GF_MEM_LOWER_SIZE, true);
  return format("%s" GF_MEM_SUFFIX, l);
}

static void gf_emit_mem_data(int dst, int imm) {
  const char *u =
      gf_lower_or_upper(false, dst >> GF_BASE_LOG * GF_MEM_LOWER_SIZE, 0,
                        GF_MEM_UPPER_SIZE, true);

  emit_str("'");
  for (int i = 0; i < GF_REG_SIZE; i++) {
    int j = imm & (GF_BASE - 1);

    emit_str("%s%s\\0", gf_num_as_path(i * GF_BASE + j), u);

    imm >>= GF_BASE_LOG;
  }
  emit_str("' ");
}

static void gf_init_mem_files() {
  for (int lower = 0; lower <= 1; lower += 1) {
    char *prefix = lower ? GF_MEM_LOWER_PREFIX : GF_MEM_UPPER_PREFIX;

    emit_line("find -quit -fprint '%s'", prefix);

    int max_size = lower ? GF_MEM_LOWER_SIZE : GF_MEM_UPPER_SIZE;
    for (int start = 0; start < max_size; start += GF_MEM_STEP_SIZE) {
      emit_str("find . -regextype posix-extended -regex '\\./%s.{%d}' ", prefix,
               start * 2);
      emit_str(GF_EXEC "-quit ");
      for (int i = 0; i < GF_MEM_STEP; i++) {
        emit_str("-fprint '{}");
        gf_emit_lower_or_upper(lower, i, start, GF_MEM_STEP_SIZE, false);
        emit_str("' ");
      }
      emit_line(GF_EXEC_END);
    }
  }
}

static void gf_emit_delete(const char *name, bool inner) {
  emit_str("\\( -exec find %s -delete %s , -true \\) ", name,
           gf_exec_end(inner));
}

static void gf_emit_touch(const char *name, bool inner) {
  emit_str(GF_EXEC "-quit -fprint %s %s ", name, gf_exec_end(inner));
}

static void gf_emit_set_size(const char *name, int size, bool inner) {
  if (size == 0) {
    gf_emit_touch(name, inner);
    return;
  }
  emit_str(GF_EXEC "%c -fprintf %s '%s' %s ", GF_INDEX_START, name,
           gf_ones(size), gf_exec_end(inner));
}

static void gf_emit_dot_handle() {
  emit_str("-name . \\( ");
  emit_str("! -exec find " GF_MEM_FLAG " -quit \\; -prune ");
  emit_str("-o -false ");
  emit_str("\\) -o ");

  for (int stage = 0; stage < GF_MEM_STAGE_COUNT; stage++) {
    bool upper_last = stage == GF_MEM_LAST_STAGE;
    bool upper = (stage <= GF_MEM_UPPER_STAGE_MAX) || upper_last;
    bool lower = !upper;
    bool lower_last = stage == GF_MEM_LAST_STAGE - 1;

    int len = upper_last
                  ? GF_MEM_UPPER_SIZE
                  : (upper ? stage : stage - (GF_MEM_UPPER_STAGE_MAX + 1)) *
                        GF_MEM_STEP_SIZE;

    char *pref = lower ? GF_MEM_LOWER_PREFIX : GF_MEM_UPPER_PREFIX;
    char *p = gf_num_as_path(stage);
    char *v = gf_lower_or_upper(lower, 0, 0, len, false);

    emit_str("-path '%s*' -type f ", p);
    emit_str("-size %dc ", GF_MEM_MARKER);
    emit_str("-regex '%s%s.{%d}' \\( ", p, pref, strlen(v));

    if (!upper_last && !lower_last) {
      gf_emit_touch("{}", false);

      for (int j = 0; j < GF_MEM_STEP; j++) {
        char *append =
            gf_lower_or_upper(lower, j, len, GF_MEM_STEP_SIZE, false);
        emit_str("! " GF_EXEC "'{}%s' -quit " GF_EXEC_END, append);
        emit_str(GF_EXEC "-fprintf '{}%s' '%s' "
                         "-quit " GF_EXEC_END,
                 append, gf_ones(GF_MEM_MARKER));
        emit_str("-o ");
      }
      emit_str("-printf 'oops (mem 1): {} = %%p stage = %d' -quit ", stage);
    } else if (upper_last) {
      gf_emit_touch("{}", false);
      gf_emit_delete(GF_MEM_FLAG, false);
      gf_emit_set_size(GF_DOT_SLASH_SEMI, GF_SEMI_MARKER_NORMAL, false);
    } else { // lower
      assert(lower_last);

      gf_emit_touch("{}", false);

      static const char *mem = "{}" GF_MEM_SUFFIX;

      // Ensure mem exists
      emit_str("\\( ");
      gf_emit_exists(mem, false);
      emit_str("-o " GF_EXEC " -fprintf %s " GF_EOF " -quit " GF_EXEC_END, mem);
      emit_str("\\) ");

      gf_emit_exists(GF_MEM_STORE_FLAG, false);
      emit_str("\\( ");
      gf_emit_delete(GF_MEM_STORE_FLAG, false);

      // Start STORE

      emit_str(GF_EXEC "-files0-from %s ", mem);

      // Handle EOF
      emit_str("-path " GF_EOF " -fprintf " GF_TMP1 " '");
      for (int i = 0; i < GF_REG_SIZE; i++) {
        emit_str("%s\\0", gf_mem_store_value(i));
        for (int j = 0; j < GF_MEM_UPPER_SIZE; j++) {
          emit_str("%s\\0", gf_mem_store_upper(j));
        }
      }
      emit_str(GF_EOF "\\0' ");
      // Handle others
      emit_str("-o -fprint0 " GF_TMP1 " ");

      emit_str(GF_EXEC_END);

      emit_str(GF_EXEC "-files0-from " GF_TMP1 " ");
      for (int i = 0; i < GF_REG_SIZE; i++) {
        for (int j = 0; j < GF_BASE; j++) {
          // value -> path representing it
          emit_str("-name '%s' -size %dc ", gf_mem_store_value(i), j);
          emit_str("-fprintf %s '%s" GF_MEM_UPPER_PREFIX "' ", mem,
                   gf_num_as_path(i * GF_BASE + j));
          emit_str("-o ");
        }
      }
      for (int i = 0; i < GF_MEM_UPPER_SIZE; i++) {
        for (int j = 0; j < GF_BASE; j++) {
          emit_str("-name '%s' -size %dc ", gf_mem_store_upper(i), j);
          emit_str("-fprintf %s '%s%s' ", mem,
                   gf_lower_or_upper(false, j, i, 1, false),
                   i == GF_MEM_UPPER_SIZE - 1 ? "\\0" : "");
          emit_str("-o ");
        }
      }

      // Copy other files that are not marked.
      emit_str("-empty -fprint0 %s ", mem);
      emit_str(GF_EXEC_END);

      // End STORE

      emit_str("\\) -o ");
      gf_emit_exists(GF_MEM_LOAD_FLAG, false);
      emit_str("\\( ");
      gf_emit_delete(GF_MEM_LOAD_FLAG, false);
      // Start LOAD

      emit_str(GF_EXEC "-files0-from %s -size %dc \\( ", mem, GF_MEM_MARKER);

      for (int i = 0; i < GF_REG_SIZE; i++) {
        for (int j = 0; j < GF_BASE; j++) {
          char *num = gf_num_as_path(i * GF_BASE + j);
          emit_str("-path '%s*' ", num);
          emit_str("-fprintf " GF_TMP1 " '%s\\0' ", gf_digit(i, j));
          emit_str("-o ");
        }
      }
      emit_str("-printf 'oops (mem 3)' -quit \\) " GF_EXEC_END);

      emit_str("\\( ");
      emit_str(GF_EXEC GF_TMP1 " -empty -delete " GF_EXEC_END);
      gf_emit_exists(GF_TMP1, false);
      emit_str("-o " GF_EXEC "-files0-from %s -fprint0 " GF_TMP1
               " " GF_EXEC_END,
               gf_const_file(0));
      emit_str("\\) ");

      emit_str("\\( ");
      for (int r = 0; r < GF_REG_COUNT; r++) {
        gf_emit_exists(gf_mem_load_dst(r), false);
        gf_emit_delete(gf_mem_load_dst(r), false);
        emit_str(GF_EXEC "-files0-from " GF_TMP1 " -fprint0 %s " GF_EXEC_END,
                 gf_reg_name(r));
        emit_str("-o ");
      }
      emit_str("-printf 'oops (mem 4)' -quit \\) ");
      // End LOAD

      emit_str("\\) -o -printf 'oops (mem 2)' -quit ");
    }
    emit_str("\\) -o ");
  }
}

static void gf_emit_eof_handle() {
  emit_str("-path " GF_SEP " -printf '' -o ");
  emit_str("-path " GF_EOF " ");
  emit_str(GF_EXEC "-files0-from " GF_LOOP " -path " GF_EOF " -fprintf " GF_TMP1
                   " '" GF_SEP "\\0" GF_EOF "\\0' -o -path " GF_SEP
                   " -fprintf " GF_TMP1 " '" GF_SEP "\\0" GF_SEP
                   "\\0' " GF_EXEC_END);

  emit_str(GF_EXEC "-files0-from " GF_TMP1 " ");
  emit_str("-fprintf " GF_LOOP " '");
  for (int i = 0; i < GF_MEM_FREQ; i++) {
    emit_str(";\\0");
  }
  for (int i = 0; i < GF_MEM_STAGE_COUNT; i++) {
    emit_str("%s\\0", gf_num_as_path(i));
  }
  emit_str("%%f\\0' " GF_EXEC_END " -o ");
}

static void gf_emit_mov_imm(const char *dst, int val, bool inner) {
  if (val <= GF_CONST_FILE_MAX_ABS ||
      val >= (1 << GF_WORD_SIZE) - GF_CONST_FILE_MAX_ABS) {
    const char *src = gf_const_file(val);
    emit_str(GF_EXEC "-files0-from %s -fprint0 %s %s ", src, dst,
             gf_exec_end(inner));
    return;
  }

  emit_str(GF_EXEC "-fprintf %s ", dst);
  gf_emit_imm(val);
  emit_str("-quit %s ", gf_exec_end(inner));
}

static void gf_emit_mov_reg(const char *dst, const char *src, bool inner) {
  if (strcmp(dst, src) == 0)
    return;
  emit_str(GF_EXEC "-files0-from %s -fprintf %s '%%p\\0' %s ", src, dst,
           gf_exec_end(inner));
}

static void gf_emit_mov(Value dst, Value src) {
  if (src.type == IMM) {
    gf_emit_mov_imm(gf_reg_name(dst.reg), src.imm, false);
  } else {
    gf_emit_mov_reg(gf_reg_name(dst.reg), gf_reg_name(src.reg), false);
  }
}

static char *gf_arith_src(int r) {
  assert(r < GF_REG_FULL_COUNT);
  return format(GF_ARITH_SRC "%d", r);
}

static char *gf_arith_dst(int r) {
  assert(r < GF_REG_FULL_COUNT);
  return format(GF_ARITH_DST "%d", r);
}

static char *gf_cmp_src(int r) {
  assert(r < GF_REG_WITH_TMP_COUNT);
  return format(GF_CMP_SRC "%d", r);
}

static char *gf_cmp_dst(int r) {
  assert(r < GF_REG_WITH_TMP_COUNT);
  return format(GF_CMP_DST "%d", r);
}

static void gf_emit_add_or_sub_reg(int dst, int src, bool add) {
  gf_emit_touch(gf_arith_dst(dst), false);
  gf_emit_touch(gf_arith_src(src), false);

  gf_emit_set_size(GF_DOT_SLASH_SEMI,
                   add ? GF_SEMI_MARKER_INST_ADD : GF_SEMI_MARKER_INST_SUB,
                   false);
}

static void gf_emit_add_or_sub_imm(int dst, int imm, bool add) {
  gf_emit_mov_imm(GF_REG_TMP, imm, false);
  gf_emit_add_or_sub_reg(dst, GF_REG_TMP_ID, add);
}

static void gf_emit_add_or_sub_reg_impl(bool add) {
  gf_emit_set_size(GF_DOT_SLASH_SEMI, GF_SEMI_MARKER_NORMAL, false);

  // First command
  emit_str("\\( ");
  for (int r = 0; r < GF_REG_FULL_COUNT; r++) {
    if (r) {
      emit_str("-o ");
    }
    gf_emit_exists(gf_arith_dst(r), false);

    char *dst = gf_reg_name(r);

    emit_str(GF_EXEC "-files0-from %s \\( ", dst);
    for (int i = 0; i < GF_BASE; i++) {
      if (i) {
        emit_str("-o ");
      }
      gf_emit_cur_is_digit_value(i);
      emit_str("\\( ");
      for (int j = 0; j < GF_REG_SIZE; j++) {
        if (j) {
          emit_str("-o ");
        }
        emit_str("-name %c ", GF_INDEX_START + j);
        emit_str("-fprintf %c '%s' ", GF_INDEX_START + j, gf_ones(i));
      }
      emit_str("\\) ");
    }
    emit_str("\\) " GF_EXEC_END);
  }
  emit_str("\\) ");

  // Second command
  gf_emit_delete(GF_CARRY, false);

  // Third command
  emit_str("\\( ");
  for (int r = 0; r < GF_REG_FULL_COUNT; r++) {
    if (r) {
      emit_str("-o ");
    }
    gf_emit_exists(gf_arith_src(r), false);
    gf_emit_delete(gf_arith_src(r), false);

    char *src = gf_reg_name(r);

    emit_str(GF_EXEC "-files0-from %s ", src);

    int sign = add ? 1 : -1;
    for (int c = sign; (c == sign) || (c == 0); c -= sign) {
      if (c == sign) {
        gf_emit_exists(GF_CARRY, true);
      } else {
        emit_str("-o ");
      }
      emit_str("\\( ");
      for (int i = 0; i < GF_BASE; i++) {
        if (i) {
          emit_str("-o ");
        }
        gf_emit_cur_is_digit_value(i);
        emit_str("\\( ");
        for (int j = 0; j < GF_BASE; j++) {
          if (j) {
            emit_str("-o ");
          }
          emit_str("-size %dc ", j);
          int v = (c + i * sign + j);
          int nc = 0;
          if (add && v >= GF_BASE) {
            nc = 1;
            v -= GF_BASE;
          } else if (!add && v < 0) {
            nc = -1;
            v += GF_BASE;
          }
          if (nc && !c) {
            gf_emit_touch(GF_CARRY, true);
          } else if (!nc && c) {
            gf_emit_delete(GF_CARRY, true);
          }
          emit_str("-fprintf " GF_TMP1 " '%s%%f\\0' ", gf_num_as_path(v));
        }
        emit_str("\\) ");
      }
      emit_str("\\) ");
    }
    emit_str(GF_EXEC_END);
  }
  emit_str("\\) ");

  // Fourth command
  emit_str("\\( ");
  for (int r = 0; r < GF_REG_FULL_COUNT; r++) {
    if (r) {
      emit_str("-o ");
    }
    gf_emit_exists(gf_arith_dst(r), false);
    gf_emit_delete(gf_arith_dst(r), false);

    char *dst = gf_reg_name(r);
    emit_str(GF_EXEC "-files0-from " GF_TMP1 " -fprint0 %s " GF_EXEC_END, dst);
  }
  emit_str("\\) ");
}

static void gf_emit_add_reg(int dst, int src) {
  gf_emit_add_or_sub_reg(dst, src, true);
}

static void gf_emit_add_imm(int dst, int imm) {
  gf_emit_add_or_sub_imm(dst, imm, true);
}

static void gf_emit_add(Value dst, Value src) {
  if (src.type == IMM) {
    gf_emit_add_imm(dst.reg, src.imm);
  } else {
    gf_emit_add_reg(dst.reg, src.reg);
  }
}

static void gf_emit_sub_reg(int dst, int src, bool delegate) {
  gf_emit_add_or_sub_reg(dst, src, false);

  if (!delegate) {
    gf_emit_add_or_sub_reg_impl(false);
  }
}

static void gf_emit_sub(Value dst, Value src) {
  if (src.type == IMM) {
    gf_emit_add_or_sub_imm(dst.reg, src.imm, false);
  } else {
    gf_emit_sub_reg(dst.reg, src.reg, true);
  }
}

static void gf_emit_load_reg(int dst, int src) {
  gf_emit_touch(gf_mem_load_src(src), false);
  gf_emit_touch(gf_mem_load_dst(dst), false);

  gf_emit_set_size(GF_DOT_SLASH_SEMI, GF_SEMI_MARKER_INST_LOAD, false);
}

static void gf_emit_load_reg_impl() {
  // Update flags
  gf_emit_touch(GF_MEM_LOAD_FLAG, false);
  gf_emit_touch(GF_MEM_FLAG, false);
  gf_emit_set_size(GF_DOT_SLASH_SEMI, GF_SEMI_MARKER_MEM, false);

  // Handle src
  emit_str("\\( ");
  for (int r = 0; r < GF_REG_WITH_TMP_COUNT; r++) {
    if (r) {
      emit_str("-o ");
    }
    char *marker = gf_mem_load_src(r);
    char *src = gf_reg_name(r);

    gf_emit_exists(marker, false);
    gf_emit_delete(marker, false);
    emit_str("\\( ");

    // Update lower (src)
    gf_emit_set_size(GF_MEM_LOWER_PREFIX, GF_MEM_MARKER, false);
    for (int i = GF_MEM_STEP_SIZE; i <= GF_MEM_LOWER_SIZE;
         i += GF_MEM_STEP_SIZE) {
      char *first = gf_digit_idx_part(0);
      char *last = gf_digit_idx_part(i);
      emit_str(GF_EXEC "-files0-from %s -name %s -fprintf " GF_TMP1
                       " '" GF_MEM_LOWER_PREFIX "' , -name %s -quit -o ",
               src, first, last);
      for (int j = 0; j < GF_BASE; j++) {
        emit_str("-path '%s*' ", gf_num_as_path(j));
        emit_str("-fprintf " GF_TMP1 " '%%f%d' ", j);
        emit_str("-o ");
      }
      emit_str("-printf 'oops (load)' " GF_EXEC_END);

      emit_str(GF_EXEC "-files0-from " GF_TMP1 " -delete " GF_EXEC_END);
    }
    // Update upper (src)
    gf_emit_set_size(GF_MEM_UPPER_PREFIX, GF_MEM_MARKER, false);
    for (int i = GF_MEM_STEP_SIZE; i <= GF_MEM_UPPER_SIZE;
         i += GF_MEM_STEP_SIZE) {
      char *first = gf_digit_idx_part(GF_MEM_LOWER_SIZE);
      char *last = gf_digit_idx_part(GF_MEM_LOWER_SIZE + i);
      emit_str(GF_EXEC "-files0-from %s -regextype posix-extended "
                       "-name %s -fprintf " GF_TMP1 " '" GF_MEM_UPPER_PREFIX
                       "' , -name %s -quit -o ",
               src, first, last);
      emit_str("-regex '.*[%s-%s]' \\( ", first, last);
      for (int j = 0; j < GF_BASE; j++) {
        emit_str("-path '%s*' ", gf_num_as_path(j));
        emit_str("-fprintf " GF_TMP1 " '%%f%d' ", j);
        emit_str("-o ");
      }
      emit_str("-printf 'oops (load)' \\) " GF_EXEC_END);

      emit_str(GF_EXEC "-files0-from " GF_TMP1 " -delete " GF_EXEC_END);
    }

    emit_str("\\) ");
  }
  emit_str("\\) ");
}

static void gf_emit_load(Value dst, Value src) {
  if (src.type == IMM) {
    gf_emit_mov_imm(GF_REG_TMP, src.imm, false);
    gf_emit_load_reg(dst.reg, GF_REG_TMP_ID);
  } else {
    gf_emit_load_reg(dst.reg, src.reg);
  }
}

static char *gf_store_src(int r) {
  assert(r < GF_REG_WITH_TMP_COUNT);
  return format(GF_MEM_STORE_SRC "%d", r);
}

static char *gf_store_dst(int r) {
  assert(r < GF_REG_WITH_TMP_COUNT);
  return format(GF_MEM_STORE_DST "%d", r);
}

static void gf_emit_store_reg(int src, int dst) {
  gf_emit_touch(gf_store_src(src), false);
  gf_emit_touch(gf_store_dst(dst), false);

  gf_emit_set_size(GF_DOT_SLASH_SEMI, GF_SEMI_MARKER_INST_STORE, false);
}

static void gf_emit_store_reg_impl() {
  // Update flags
  gf_emit_touch(GF_MEM_STORE_FLAG, false);
  gf_emit_touch(GF_MEM_FLAG, false);
  gf_emit_set_size(GF_DOT_SLASH_SEMI, GF_SEMI_MARKER_MEM, false);

  // Handle dst
  emit_str("\\( ");
  for (int r = 0; r < GF_REG_WITH_TMP_COUNT; r++) {
    char *marker = gf_store_dst(r);
    char *dst = gf_reg_name(r);
    gf_emit_exists(marker, false);
    gf_emit_delete(marker, false);

    emit_str("\\( ");

    // Update lower (dst)
    gf_emit_set_size(GF_MEM_LOWER_PREFIX, GF_MEM_MARKER, false);
    for (int i = GF_MEM_STEP_SIZE; i <= GF_MEM_LOWER_SIZE;
         i += GF_MEM_STEP_SIZE) {
      char *first = gf_digit_idx_part(0);
      char *last = gf_digit_idx_part(i);
      emit_str(GF_EXEC "-files0-from %s -name %s -fprintf " GF_TMP1
                       " '" GF_MEM_LOWER_PREFIX "' , -name %s -quit -o ",
               dst, first, last);
      for (int j = 0; j < GF_BASE; j++) {
        if (j) {
          emit_str("-o ");
        }
        emit_str("-path '%s*' ", gf_num_as_path(j));
        emit_str("-fprintf " GF_TMP1 " '%%f%d' ", j);
      }
      emit_str(GF_EXEC_END);

      emit_str(GF_EXEC "-files0-from " GF_TMP1 " -delete " GF_EXEC_END);
    }
    // Update upper (dst)
    gf_emit_set_size(GF_MEM_UPPER_PREFIX, GF_MEM_MARKER, false);
    for (int i = GF_MEM_STEP_SIZE; i <= GF_MEM_UPPER_SIZE;
         i += GF_MEM_STEP_SIZE) {
      char *first = gf_digit_idx_part(GF_MEM_LOWER_SIZE);
      char *last = gf_digit_idx_part(GF_MEM_LOWER_SIZE + i);
      emit_str(GF_EXEC "-files0-from %s -regextype posix-extended "
                       "-name %s -fprintf " GF_TMP1 " '" GF_MEM_UPPER_PREFIX
                       "' , -name %s -quit -o ",
               dst, first, last);
      emit_str("-regex '.*[%s-%s]' \\( ", first, last);
      for (int j = 0; j < GF_BASE; j++) {
        if (j) {
          emit_str("-o ");
        }
        emit_str("-path '%s*' ", gf_num_as_path(j));
        emit_str("-fprintf " GF_TMP1 " '%%f%d' ", j);
      }
      emit_str("\\) " GF_EXEC_END);

      emit_str(GF_EXEC "-files0-from " GF_TMP1 " -delete " GF_EXEC_END);
    }
    emit_str(GF_EXEC "-files0-from %s ", dst);
    for (int i = 0; i < GF_MEM_UPPER_SIZE; i++) {
      emit_str("-name %s \\( ", gf_digit_idx_part(GF_MEM_LOWER_SIZE + i));
      for (int j = 0; j < GF_BASE; j++) {
        if (j) {
          emit_str("-o ");
        }
        emit_str("-path '%s*' -fprintf %s '%s' ", gf_num_as_path(j),
                 gf_mem_store_upper(i), gf_ones(j));
      }
      emit_str("\\) -o ");
    }
    emit_str("-fprintf 'oops (store 2)\\n' -quit ");
    emit_str(GF_EXEC_END);

    emit_str("\\) -o ");
  }
  emit_str("-fprintf 'oops (store 1)\\n' -quit \\) ");

  // Handle src
  emit_str("\\( ");
  for (int r = 0; r < GF_REG_WITH_TMP_COUNT; r++) {
    char *marker = gf_store_src(r);
    char *src = gf_reg_name(r);
    gf_emit_exists(marker, false);
    gf_emit_delete(marker, false);

    // Update value
    emit_str(GF_EXEC "-files0-from %s ", src);
    for (int i = 0; i < GF_BASE; i++) {
      if (i) {
        emit_str("-o ");
      }
      emit_str("-path '%s*' \\( ", gf_num_as_path(i));
      for (int j = 0; j < GF_REG_SIZE; j++) {
        if (j) {
          emit_str("-o ");
        }
        emit_str("-name %s ", gf_digit_idx_part(j));
        emit_str("-fprintf %s '%s' ", gf_mem_store_value(j), gf_ones(i));
      }
      emit_str("\\) ");
    }
    emit_str(GF_EXEC_END);

    emit_str("-o ");
  }
  emit_str("-fprintf 'oops (store 2)\\n' -quit \\) ");
}

static void gf_emit_store(Value src, Value dst) {
  if (dst.type == IMM) {
    gf_emit_mov_imm(GF_REG_TMP, dst.imm, false);
    gf_emit_store_reg(src.reg, GF_REG_TMP_ID);
  } else {
    gf_emit_store_reg(src.reg, dst.reg);
  }
}

static void gf_emit_putc_reg(const char *a) {
  char name_upto = (char)(GF_INDEX_START + (8 / GF_BASE_LOG));
  // First command
  emit_str(GF_EXEC "-files0-from %s ", a);
  emit_str("-name %c -fprintf " GF_TMP1 " " GF_PUTC_FLAG_PREFIX
           " , -name %c -quit -o \\( ",
           GF_INDEX_START, name_upto);
  for (int i = 0; i < GF_BASE; i++) {
    gf_emit_cur_is_digit_value(i);
    if (GF_BASE > 10) {
      emit_str("-fprintf " GF_TMP1 " %02d -o ", i);
    } else {
      emit_str("-fprintf " GF_TMP1 " %d -o ", i);
    }
  }
  emit_str("-printf 'oops (putc 1)' \\) \\; ");

  // Second command
  emit_str(GF_EXEC "-files0-from " GF_TMP1 " ");
  for (int i = 0; i < 256; i++) {
    emit_str("-name %s ", gf_putc_flag_name(i));
    emit_str("-printf '\\%03o' -o ", i);
  }
  emit_str("-printf 'oops (putc 2)' \\; ");
}

static void gf_emit_putc(Value src) {
  if (src.type == IMM) {
    gf_emit_mov_imm(GF_REG_TMP, src.imm, false);
    gf_emit_putc_reg(GF_REG_TMP);
  } else {
    gf_emit_putc_reg(gf_reg_name(src.reg));
  }
}

static void gf_emit_getc(Value dst) {
  char *dst_name = gf_reg_name(dst.reg);
  gf_emit_mov_imm(dst_name, 0, false);
  gf_emit_delete(GF_TMP_FLAG, false);

  emit_str(GF_EXEC "-files0-from " GF_INPUT " ");
  gf_emit_exists(GF_TMP_FLAG, true);
  emit_str("-fprintf " GF_TMP1 " '%%f\\0' -o ");
  gf_emit_touch(GF_TMP_FLAG, true);
  emit_str("\\( ");
  for (int i = 0; i < 256; i++) {
    emit_str("-name %d ", i);
    gf_emit_mov_imm(dst_name, i, true);
    emit_str("-o ");
  }
  emit_str("-printf 'oops (getc)' \\) \\; ");

  gf_emit_mov_reg(GF_INPUT, GF_TMP1, false);
}

static void gf_emit_jmp_reg(char *a) {
  assert(GF_BASE == 2);

  char *last = gf_digit_idx_part(g_gf_pc_bits);

  emit_str(GF_EXEC "-files0-from %s ", a);
  emit_str("-name %s -quit ", last);
  for (int i = 0; i < GF_BASE; i++) {
    emit_str("-o ");
    gf_emit_cur_is_digit_value(i);
    emit_str("\\( ");
    for (int j = 0; j < g_gf_pc_bits; j++) {
      if (j) {
        emit_str("-o ");
      }
      emit_str("-name %s ", gf_digit_idx_part(j));
      if (i == 0) {
        gf_emit_delete(gf_pc_file(j), true);
      } else {
        gf_emit_touch(gf_pc_file(j), true);
      }
    }
    emit_str("\\) ");
  }
  emit_str(GF_EXEC_END);
}

static void gf_emit_jmp_imm(int pc, int j) {
  for (int i = 0; i < g_gf_pc_bits; i++) {
    bool from = (pc & (1 << i)) != 0;
    bool to = (j & (1 << i)) != 0;
    if (!from && to) {
      gf_emit_touch(gf_pc_file(i), false);
    } else if (from && !to) {
      gf_emit_delete(gf_pc_file(i), false);
    }
  }
}

static void gf_emit_jmp(Value jmp, int cur_pc) {
  if (jmp.type == IMM) {
    gf_emit_jmp_imm(cur_pc, jmp.imm);
  } else {
    gf_emit_jmp_reg(gf_reg_name(jmp.reg));
  }
}

static void gf_emit_is_zero(const char *a) {
  emit_str("\\( ");
  gf_emit_delete(GF_TMP_FLAG, false);
  emit_str(GF_EXEC "-files0-from %s ! ", a);
  gf_emit_cur_is_digit_value(0);
  gf_emit_touch(GF_TMP_FLAG, true);
  emit_str("-quit \\; ");
  emit_str("! -exec find " GF_TMP_FLAG " -quit \\; ");
  emit_str("\\) ");
}

static void gf_emit_set_is_zero(const char *a) {
  emit_str("\\( ");
  gf_emit_is_zero(a);
  gf_emit_mov_imm(a, 1, false);
  emit_str("-o ");
  gf_emit_mov_imm(a, 0, false);
  emit_str("\\) ");
}

static void gf_emit_set_is_non_zero(const char *a) {
  emit_str("\\( ");
  gf_emit_is_zero(a);
  gf_emit_mov_imm(a, 0, false);
  emit_str("-o ");
  gf_emit_mov_imm(a, 1, false);
  emit_str("\\) ");
}

static void gf_emit_cmp_reg_impl(Op op) {
  gf_emit_set_size(GF_DOT_SLASH_SEMI, GF_SEMI_MARKER_NORMAL, false);

  bool swap = op == GT || op == LE;
  if (swap) {
    op = op == GT ? LT : GE;
  }

  const int lhs = GF_REG_CMP_ID;
  const int rhs = GF_REG_CMP2_ID;
  const char *lhs_name = GF_REG_CMP;
  const char *rhs_name = GF_REG_CMP2;

  emit_str("\\( ");
  for (int r = 0; r < GF_REG_WITH_TMP_COUNT; r++) {
    if (r) {
      emit_str("-o ");
    }
    char *marker = gf_cmp_dst(r);
    gf_emit_exists(marker, false);

    char *dst = gf_reg_name(r);

    if (swap) {
      gf_emit_mov_reg(rhs_name, dst, false);
    } else {
      gf_emit_mov_reg(lhs_name, dst, false);
    }
  }
  emit_str("\\) ");

  emit_str("\\( ");
  for (int r = 0; r < GF_REG_WITH_TMP_COUNT; r++) {
    if (r) {
      emit_str("-o ");
    }
    char *marker = gf_cmp_src(r);
    gf_emit_exists(marker, false);
    gf_emit_delete(marker, false);

    char *src = gf_reg_name(r);

    if (swap) {
      gf_emit_mov_reg(lhs_name, src, false);
    } else {
      gf_emit_mov_reg(rhs_name, src, false);
    }
  }
  emit_str("\\) ");

  gf_emit_sub_reg(lhs, rhs, false);

  switch (op) {
  case EQ:
    gf_emit_set_is_zero(lhs_name);
    break;
  case NE:
    gf_emit_set_is_non_zero(lhs_name);
    break;
  case LT:
    emit_str("\\( ");
    gf_emit_exists(GF_CARRY, false);
    gf_emit_mov_imm(lhs_name, 1, false);
    emit_str("-o ");
    gf_emit_mov_imm(lhs_name, 0, false);
    emit_str("\\) ");
    break;
  case GE:
    emit_str("\\( ");
    gf_emit_exists(GF_CARRY, false);
    gf_emit_mov_imm(lhs_name, 0, false);
    emit_str("-o ");
    gf_emit_mov_imm(lhs_name, 1, false);
    emit_str("\\) ");
    break;
  default:
    error("oops (cmp)");
  }

  emit_str("\\( ");
  for (int r = 0; r < GF_REG_WITH_TMP_COUNT; r++) {
    if (r) {
      emit_str("-o ");
    }
    char *marker = gf_cmp_dst(r);
    gf_emit_exists(marker, false);
    gf_emit_delete(marker, false);

    char *dst = gf_reg_name(r);
    gf_emit_mov_reg(dst, lhs_name, false);
  }
  emit_str("\\) ");
}

static void gf_emit_cmp_reg(int dst, int src, Op op, bool delegate) {
  gf_emit_touch(gf_cmp_dst(dst), false);
  gf_emit_touch(gf_cmp_src(src), false);

  int marker;
  switch (op) {
  case EQ:
    marker = GF_SEMI_MARKER_INST_EQ;
    break;
  case NE:
    marker = GF_SEMI_MARKER_INST_NE;
    break;
  case LT:
    marker = GF_SEMI_MARKER_INST_LT;
    break;
  case GT:
    marker = GF_SEMI_MARKER_INST_GT;
    break;
  case LE:
    marker = GF_SEMI_MARKER_INST_LE;
    break;
  case GE:
    marker = GF_SEMI_MARKER_INST_GE;
    break;
  default:
    error("oops");
  }

  gf_emit_set_size(GF_DOT_SLASH_SEMI, marker, false);

  if (!delegate) {
    gf_emit_cmp_reg_impl(op);
  }
}

static void gf_emit_cmp(Value dst, Value src, Op op) {
  int src_r;
  if (src.type == IMM) {
    src_r = GF_REG_TMP_ID;
    gf_emit_mov_imm(GF_REG_TMP, src.imm, false);
  } else {
    src_r = src.reg;
  }
  gf_emit_cmp_reg(dst.reg, src_r, op, true);
}

static void gf_emit_jeq_zero(int dst, Value jmp, int cur_pc, bool equal) {
  emit_str("\\( ");
  if (!equal) {
    emit_str("! ");
  }
  gf_emit_is_zero(gf_reg_name(dst));
  gf_emit_jmp(jmp, cur_pc);
  emit_str(" -o -printf '' \\) ");
}

static void gf_emit_jcmp(Value dst, Value src, Value jmp, Op op, int cur_pc) {
  int src_r;
  char *src_name;
  if (src.type == IMM) {
    src_r = GF_REG_TMP_ID;
    src_name = GF_REG_TMP;
    gf_emit_mov_imm(src_name, src.imm, false);
  } else {
    src_r = src.reg;
    src_name = gf_reg_name(src.reg);
  }

  Op cmp;
  switch (op) {
  case JEQ:
    cmp = EQ;
    break;
  case JNE:
    cmp = NE;
    break;
  case JLT:
    cmp = LT;
    break;
  case JGT:
    cmp = GT;
    break;
  case JLE:
    cmp = LE;
    break;
  case JGE:
    cmp = GE;
    break;
  default:
    error("oops (jcmp)");
  }
  gf_emit_mov_reg(GF_REG_TMP2, gf_reg_name(dst.reg), false);
  gf_emit_cmp_reg(GF_REG_TMP2_ID, src_r, cmp, false);

  emit_str("\\( ");
  gf_emit_is_zero(GF_REG_TMP2);
  emit_str("-o ");
  gf_emit_jmp(jmp, cur_pc);
  emit_str("\\) ");
}

static void gf_emit_inst(Inst *inst, int cur_pc) {
  switch (inst->op) {
  case MOV:
    gf_emit_mov(inst->dst, inst->src); // don't use inner
    break;

  case ADD:
    gf_emit_add(inst->dst, inst->src); // use inner
    break;

  case SUB:
    gf_emit_sub(inst->dst, inst->src); // use inner
    break;

  case LOAD:
    gf_emit_load(inst->dst, inst->src);
    break;

  case STORE:
    gf_emit_store(inst->dst, inst->src);
    break;

  case PUTC:
    gf_emit_putc(inst->src); // don't use inner
    break;

  case GETC:
    gf_emit_getc(inst->dst); // use inner
    break;

  case EXIT:
    emit_str("-quit ");
    break;

  case JEQ: // 8
    if (inst->src.type == IMM && inst->src.imm == 0) {
      gf_emit_jeq_zero(inst->dst.reg, inst->jmp, cur_pc, true);
      break;
    }
    // fallthrough

  case JNE: // 9
    if (inst->src.type == IMM && inst->src.imm == 0) {
      gf_emit_jeq_zero(inst->dst.reg, inst->jmp, cur_pc, false);
      break;
    }
    // fallthrough

  case JLT: // 10
  case JGT: // 11
  case JLE: // 12
  case JGE: // 13
    gf_emit_jcmp(inst->dst, inst->src, inst->jmp, inst->op, cur_pc);
    break;

  case JMP:
    gf_emit_jmp(inst->jmp, cur_pc); // don't use inner
    break;

  case EQ: // 16
  case NE: // 17
  case LT: // 18
  case GT: // 19
  case LE: // 20
  case GE: // 21
    gf_emit_cmp(inst->dst, inst->src, inst->op);
    break;

  case DUMP:
    break;

  default:
    error(format("oops (unimplemented) %d", inst->op));
  }
}

static void gf_init_state(bool has_getc) {
  emit_line("#!/bin/bash");
  emit_line("set -f # Disable globbing\n");

  emit_line("if [[ -z \"${GF_UNSAFE}\" ]]; then");
  emit_line("  DIR=$(mktemp -d)");
  emit_line("  if [[ ! -d \"$DIR\" ]]; then");
  emit_line("    exit 1");
  emit_line("  fi");
  emit_line("  cd \"$DIR\" || exit 1 # For safety");
  emit_line("  trap \"rm -r $DIR\" EXIT");
  emit_line("fi");

#if GF_DEBUG_FLAGS
  emit_line("rm -f " GF_DEBUG_LOG);
#endif

  emit_line("\n# Main program");

  // Init constants
  emit_str("find ");
  for (int i = 0; i <= GF_CONST_FILE_MAX_ABS; i++) {
    gf_emit_make_const(i);
    if (i > 0) {
      gf_emit_make_const((1 << GF_WORD_SIZE) - i);
    }
  }
  emit_line("-quit");

  gf_init_mem_files();
  // Init ; to be empty.
  emit_line("find -quit -fprint \\; -fprint " GF_EOF " -fprint " GF_SEP);
  // Init others
  emit_str("find \\; -fprintf " GF_INPUT " '' -fprintf " GF_LOOP " '" GF_EOF
           "\\0' ");
  for (int i = 0; i < GF_REG_FULL_COUNT; i++) {
    gf_emit_mov_imm(gf_reg_name(i), 0, false);
  }
  for (int i = 0; i < 256; i++) {
    gf_emit_mov_imm(gf_putc_flag_name(i), 0, false);
  }
  emit_str("-quit ");
  // Keep the following files empty.
  for (int i = 0; i < GF_REG_SIZE; i++) {
    emit_str("-fprint %c ", GF_INDEX_START + i);
    for (int j = 0; j < GF_BASE; j++) {
      emit_str("-fprint %c%d ", GF_INDEX_START + i, j);
    }
  }
  for (int i = 0; i < 256; i++) {
    emit_str("-fprint %d ", i);
  }
  emit_line("");

  if (has_getc) {
    emit_line("find -files0-from - -fprintf %s '%%f\\0'", GF_INPUT);
  }
}

// Set the semi file's byte size to (pc + GF_PC_OFFSET), encoding the current pc
// (read from the z-bit files) so the dispatch tree can route with inline -size
// tests instead of one nested `find zX -quit` per pc bit. Every pc-bit file is
// passed as a start path; each that exists contributes 2^i bytes. The leading
// '.' (always present) seeds GF_PC_OFFSET bytes and, via -fprintf truncating on
// first open, resets the size, so pc == 0 yields size GF_PC_OFFSET. Emitted once
// (in the build arm), so its O(pc) literal size does not blow up the program.
static void gf_emit_set_semi_pc() {
  emit_str(GF_EXEC ". ");
  for (int i = 0; i < g_gf_pc_bits; i++) {
    emit_str("%s ", gf_pc_file(i));
  }
  emit_str("-maxdepth 0 ");
  emit_str("\\( -path . -fprintf " GF_DOT_SLASH_SEMI " '");
  for (int j = 0; j < GF_PC_OFFSET; j++) {
    emit_str("1");
  }
  emit_str("' \\) ");
  for (int i = 0; i < g_gf_pc_bits; i++) {
    emit_str("-o \\( -name %s -fprintf " GF_DOT_SLASH_SEMI " '", gf_pc_file(i));
    for (int j = 0; j < (1 << i); j++) {
      emit_str("1");
    }
    emit_str("' \\) ");
  }
  emit_str("-o -true " GF_EXEC_END);
}

void gf_emit_insts(Inst **start_inst, int start, int end, int oob_pc) {
  int last = end - 1;
  assert(start <= last);

  if (start == last) {
    int pc = start;
    int next_pc = oob_pc;
    for (Inst *inst = start_inst[pc]; inst; inst = inst->next) {
      if (inst->pc != pc) {
        next_pc = inst->pc;
        break;
      }
    }
    gf_emit_jmp_imm(pc, next_pc);
    emit_str("\\\n");

    int last_op = -1;
    for (Inst *inst = start_inst[pc]; inst && inst->pc == pc;
         inst = inst->next) {

#if GF_DEBUG_FLAGS & GF_DEBUG_FLAG_PC_PER_INST
      gf_dump_pc(pc);
#endif
#if GF_DEBUG_FLAGS & GF_DEBUG_FLAG_REGS_PER_INST
      gf_dump_regs();
#endif

      gf_emit_inst(inst, next_pc);
      last_op = inst->op;

#if GF_DEBUG_FLAGS & GF_DEBUG_FLAG_DUMP_INST
      gf_dump_inst(inst);
#endif

      emit_str("\\\n");
    }

    // Marker-delegate ops (arith/mem/standalone cmp) leave a semi marker for
    // their impl arm, which resets the semi to 0 when done. Every other op
    // leaves the semi still pc-encoded (or already 0), so reset it to 0 here to
    // make the next visit rebuild the encoding for the new pc.
    int op_marker = (1 << ADD) | (1 << SUB) | (1 << LOAD) | (1 << STORE) |
                    (1 << EQ) | (1 << NE) | (1 << LT) | (1 << GT) | (1 << LE) |
                    (1 << GE);
    if (last_op < 0 || !((1 << last_op) & op_marker)) {
      gf_emit_set_size(GF_DOT_SLASH_SEMI, GF_SEMI_MARKER_NORMAL, false);
      emit_str("\\\n");
    }
    return;
  }

  int high = 30;
  while (((start >> high) & 1) == (((last >> high) & 1))) {
    high--;
  }
  assert(((start >> high) & 1) == 0);

  int mid = start | (1 << high);

  // pc bit `high` is set  <=>  pc >= mid  <=>  semi size (= pc + GF_PC_OFFSET)
  // > mid + GF_PC_OFFSET - 1. This is an inline -size test on the semi file find
  // is already positioned on, replacing a per-bit nested `find zX -quit`.
  emit_str("-size +%dc ", mid + GF_PC_OFFSET - 1);
  emit_str("\\( ");

  gf_emit_insts(start_inst, mid, end, oob_pc);

  emit_str("\\) -o \\( ");

  gf_emit_insts(start_inst, start, mid, oob_pc);

  emit_str("\\) ");
}

void target_gnufind(Module *module) {
  gf_check();

  int max_pc = 0;
  bool has_getc = false;

  for (Inst *inst = module->text; inst; inst = inst->next) {
    if (inst->op == GETC) {
      has_getc = true;
    }

    if (inst->pc > max_pc) {
      max_pc = inst->pc;
    }
  }

  int aux = 0;
  int orig_inst_pc = -1;
  // TODO: remove renumber after jumps by evaluating jump as false.
  int jumps = (1 << JEQ | 1 << JNE | 1 << JLT | 1 << JGT | 1 << JLE | 1 << JGE);
  int op_delegate = (1 << ADD) | (1 << SUB) | (1 << LOAD) | (1 << STORE) |
                    (1 << EQ) | (1 << NE) | (1 << LT) | (1 << GT) | (1 << LE) |
                    (1 << GE) | jumps;
  for (Inst *inst = module->text; inst->next; inst = inst->next) {
    bool is_border = orig_inst_pc < inst->next->pc;
    orig_inst_pc = inst->next->pc;
    bool delegate = ((1 << inst->op) & op_delegate) != 0;
    bool next_pc_is_new_aux = !is_border && delegate;
    bool next_pc_is_same_as_cur = !is_border && !delegate;
    if (next_pc_is_new_aux) {
      inst->next->pc = max_pc + (++aux);
    } else if (next_pc_is_same_as_cur) {
      inst->next->pc = inst->pc;
    }
  }
  int oob_pc = max_pc + aux + 1;
  assert(oob_pc < (1 << GF_WORD_SIZE));

  // Initialize global variables.
  g_gf_pc_bits = 1;
  while ((1 << g_gf_pc_bits) < oob_pc) {
    g_gf_pc_bits += 1;
  }

  gf_init_state(has_getc);

  // Init memory with data
  int dst = 0;
  emit_str("find ");
  int max_lower = -1;
  for (Data *data = module->data; data; data = data->next, dst++) {
    int lower = dst & ((1 << GF_BASE_LOG * GF_MEM_LOWER_SIZE) - 1);
    if (max_lower < lower) {
      max_lower = lower;
    }
    char *m = gf_mem_file(dst);
    emit_str("-fprintf %s ", m);
    gf_emit_mem_data(dst, data->v);
  }
  for (int i = 0; i <= max_lower; i++) {
    emit_str("-fprintf %s " GF_EOF " ", gf_mem_file(i));
  }
  emit_line("-printf '' -quit");

  emit_str("find -regextype posix-extended -files0-from " GF_LOOP " ");

  gf_emit_dot_handle();
  emit_str("\\\n");

  gf_emit_eof_handle();
  emit_str("\\\n");

  emit_str("-path \\; \\( "); // semi

  // Store impl
  emit_str("-size %dc \\( ", GF_SEMI_MARKER_INST_STORE);
  gf_emit_store_reg_impl();
  emit_str("\\) -o ");
  emit_str("\\\n");

  // Load impl
  emit_str("-size %dc \\( ", GF_SEMI_MARKER_INST_LOAD);
  gf_emit_load_reg_impl();
  emit_str("\\) -o ");
  emit_str("\\\n");

  // Add impl
  emit_str("-size %dc \\( ", GF_SEMI_MARKER_INST_ADD);
  gf_emit_add_or_sub_reg_impl(true);
  emit_str("\\) -o ");
  emit_str("\\\n");

  // Sub impl
  emit_str("-size %dc \\( ", GF_SEMI_MARKER_INST_SUB);
  gf_emit_add_or_sub_reg_impl(false);
  emit_str("\\) -o ");
  emit_str("\\\n");

  // EQ
  emit_str("-size %dc \\( ", GF_SEMI_MARKER_INST_EQ);
  gf_emit_cmp_reg_impl(EQ);
  emit_str("\\) -o ");
  emit_str("\\\n");

  // NE
  emit_str("-size %dc \\( ", GF_SEMI_MARKER_INST_NE);
  gf_emit_cmp_reg_impl(NE);
  emit_str("\\) -o ");
  emit_str("\\\n");

  // LT
  emit_str("-size %dc \\( ", GF_SEMI_MARKER_INST_LT);
  gf_emit_cmp_reg_impl(LT);
  emit_str("\\) -o ");
  emit_str("\\\n");

  // GT
  emit_str("-size %dc \\( ", GF_SEMI_MARKER_INST_GT);
  gf_emit_cmp_reg_impl(GT);
  emit_str("\\) -o ");
  emit_str("\\\n");

  // GE
  emit_str("-size %dc \\( ", GF_SEMI_MARKER_INST_GE);
  gf_emit_cmp_reg_impl(GE);
  emit_str("\\) -o ");
  emit_str("\\\n");

  // LE
  emit_str("-size %dc \\( ", GF_SEMI_MARKER_INST_LE);
  gf_emit_cmp_reg_impl(LE);
  emit_str("\\) -o ");
  emit_str("\\\n");

  // BUILD: an instruction just finished (semi size 0). Rebuild the pc encoding
  // (semi size = pc + GF_PC_OFFSET) from the z-bit files for the next dispatch.
  emit_str("-size %dc \\( ", GF_SEMI_MARKER_NORMAL);
  gf_emit_set_semi_pc();
  emit_str("\\) -o ");
  emit_str("\\\n");

  // DISPATCH: semi size encodes the pc (>= GF_PC_OFFSET). Route to the
  // instruction with an inline -size binary search over the semi file.
  emit_str("-size +%dc \\( ", GF_PC_OFFSET - 1);
  Inst **start_inst = (Inst **)calloc(oob_pc, sizeof(Inst *));
  for (Inst *inst = module->text; inst; inst = inst->next) {
    if (start_inst[inst->pc] == NULL) {
      start_inst[inst->pc] = inst;
    }
#if GF_DEBUG_FLAGS & GF_DEBUG_FLAG_DUMP_INST
    dump_inst(inst);
#endif
  }
  gf_emit_insts(start_inst, 0, oob_pc, oob_pc);
  emit_str("\\) ");

  emit_str("\\) "); // semi
  emit_line("");

  emit_line("find -quit"); // exit 0
}
