/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright (C) 2014 David Herrmann <dh.herrmann@gmail.com>

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "util.h"

typedef struct term_char term_char_t;
typedef struct term_charbuf term_charbuf_t;

typedef struct term_color term_color;
typedef struct term_attr term_attr;
typedef struct term_cell term_cell;
typedef struct term_line term_line;

typedef struct term_page term_page;
typedef struct term_history term_history;

typedef struct term_utf8 term_utf8;
typedef struct term_seq term_seq;
typedef struct term_parser term_parser;
typedef uint32_t term_charset[96];

/*
 * Miscellaneous
 * Sundry things and external helpers.
 */

int mk_wcwidth(wchar_t ucs4);
int mk_wcwidth_cjk(wchar_t ucs4);
int mk_wcswidth(const wchar_t *str, size_t len);
int mk_wcswidth_cjk(const wchar_t *str, size_t len);

/*
 * Ageing
 * Redrawing terminals is quite expensive. Therefore, we avoid redrawing on
 * each single modification and mark modified cells instead. This way, we know
 * which cells to redraw on the next frame. However, a single DIRTY flag is not
 * enough for double/triple buffered screens, hence, we use an AGE field for
 * each cell. If the cell is modified, we simply increase the age by one. Each
 * framebuffer can then remember its last rendered age and request an update of
 * all newer cells.
 * TERM_AGE_NULL is special. If used as cell age, the cell must always be
 * redrawn (forced update). If used as framebuffer age, all cells are drawn.
 * This way, we can allow integer wrap-arounds.
 */

typedef uint64_t term_age_t;

#define TERM_AGE_NULL 0

/*
 * Characters
 * Each cell in a terminal page contains only a single character. This is
 * usually a single UCS-4 value. However, Unicode allows combining-characters,
 * therefore, the number of UCS-4 characters per cell must be unlimited. The
 * term_char_t object wraps the internal combining char API so it can be
 * treated as a single object.
 */

struct term_char {
        /* never access this value directly */
        uint64_t _value;
};

struct term_charbuf {
        /* 3 bytes + zero-terminator */
        uint32_t buf[4];
};

#define TERM_CHAR_INIT(_val) ((term_char_t){ ._value = (_val) })
#define TERM_CHAR_NULL TERM_CHAR_INIT(0)

term_char_t term_char_set(term_char_t previous, uint32_t append_ucs4);
term_char_t term_char_merge(term_char_t base, uint32_t append_ucs4);
term_char_t term_char_dup(term_char_t ch);
term_char_t term_char_dup_append(term_char_t base, uint32_t append_ucs4);

const uint32_t *term_char_resolve(term_char_t ch, size_t *s, term_charbuf_t *b);
unsigned int term_char_lookup_width(term_char_t ch);

/* true if @ch is TERM_CHAR_NULL, otherwise false */
static inline bool term_char_is_null(term_char_t ch) {
        return ch._value == 0;
}

/* true if @ch is dynamically allocated and needs to be freed */
static inline bool term_char_is_allocated(term_char_t ch) {
        return !term_char_is_null(ch) && !(ch._value & 0x1);
}

/* true if (a == b), otherwise false; this is (a == b), NOT (*a == *b) */
static inline bool term_char_same(term_char_t a, term_char_t b) {
        return a._value == b._value;
}

/* true if (*a == *b), otherwise false; this is implied by (a == b) */
static inline bool term_char_equal(term_char_t a, term_char_t b) {
        const uint32_t *sa, *sb;
        term_charbuf_t ca, cb;
        size_t na, nb;

        sa = term_char_resolve(a, &na, &ca);
        sb = term_char_resolve(b, &nb, &cb);
        return na == nb && !memcmp(sa, sb, sizeof(*sa) * na);
}

/* free @ch in case it is dynamically allocated */
static inline term_char_t term_char_free(term_char_t ch) {
        if (term_char_is_allocated(ch))
                term_char_set(ch, 0);

        return TERM_CHAR_NULL;
}

/* gcc _cleanup_ helpers */
#define _term_char_free_ _cleanup_(term_char_freep)
static inline void term_char_freep(term_char_t *p) {
        term_char_free(*p);
}

/*
 * Attributes
 * Each cell in a terminal page can have its own set of attributes. These alter
 * the behavior of the renderer for this single cell. We use term_attr to
 * specify attributes.
 * The only non-obvious field is "ccode" for foreground and background colors.
 * This field contains the terminal color-code in case no full RGB information
 * was given by the host. It is also required for dynamic color palettes. If it
 * is set to TERM_CCODE_RGB, the "red", "green" and "blue" fields contain the
 * full RGB color.
 */

enum {
        /* special color-codes */
        TERM_CCODE_DEFAULT,                                             /* default foreground/background color */
        TERM_CCODE_256,                                                 /* 256color code */
        TERM_CCODE_RGB,                                                 /* color is specified as RGB */

        /* dark color-codes */
        TERM_CCODE_BLACK,
        TERM_CCODE_RED,
        TERM_CCODE_GREEN,
        TERM_CCODE_YELLOW,
        TERM_CCODE_BLUE,
        TERM_CCODE_MAGENTA,
        TERM_CCODE_CYAN,
        TERM_CCODE_WHITE,                                               /* technically: light grey */

        /* light color-codes */
        TERM_CCODE_LIGHT_BLACK          = TERM_CCODE_BLACK + 8,         /* technically: dark grey */
        TERM_CCODE_LIGHT_RED            = TERM_CCODE_RED + 8,
        TERM_CCODE_LIGHT_GREEN          = TERM_CCODE_GREEN + 8,
        TERM_CCODE_LIGHT_YELLOW         = TERM_CCODE_YELLOW + 8,
        TERM_CCODE_LIGHT_BLUE           = TERM_CCODE_BLUE + 8,
        TERM_CCODE_LIGHT_MAGENTA        = TERM_CCODE_MAGENTA + 8,
        TERM_CCODE_LIGHT_CYAN           = TERM_CCODE_CYAN + 8,
        TERM_CCODE_LIGHT_WHITE          = TERM_CCODE_WHITE + 8,

        TERM_CCODE_CNT,
};

struct term_color {
        uint8_t ccode;
        uint8_t c256;
        uint8_t red;
        uint8_t green;
        uint8_t blue;
};

struct term_attr {
        term_color fg;                          /* foreground color */
        term_color bg;                          /* background color */

        unsigned int bold : 1;                  /* bold font */
        unsigned int italic : 1;                /* italic font */
        unsigned int underline : 1;             /* underline text */
        unsigned int inverse : 1;               /* inverse fg/bg */
        unsigned int protect : 1;               /* protect from erase */
        unsigned int blink : 1;                 /* blink text */
        unsigned int hidden : 1;                /* hidden */
};

/*
 * Cells
 * The term_cell structure respresents a single cell in a terminal page. It
 * contains the stored character, the age of the cell and all its attributes.
 */

struct term_cell {
        term_char_t ch;         /* stored char or TERM_CHAR_NULL */
        term_age_t age;         /* cell age or TERM_AGE_NULL */
        term_attr attr;         /* cell attributes */
        unsigned int cwidth;    /* cached term_char_lookup_width(cell->ch) */
};

/*
 * Lines
 * Instead of storing cells in a 2D array, we store them in an array of
 * dynamically allocated lines. This way, scrolling can be implemented very
 * fast without moving any cells at all. Similarly, the scrollback-buffer is
 * much simpler to implement.
 * We use term_line to store a single line. It contains an array of cells, a
 * fill-state which remembers the amount of blanks on the right side, a
 * separate age just for the line which can overwrite the age for all cells,
 * and some management data.
 */

struct term_line {
        term_line *lines_next;          /* linked-list for histories */
        term_line *lines_prev;          /* linked-list for histories */

        unsigned int width;             /* visible width of line */
        unsigned int n_cells;           /* # of allocated cells */
        term_cell *cells;               /* cell-array */

        term_age_t age;                 /* line age */
        unsigned int fill;              /* # of valid cells; starting left */
};

int term_line_new(term_line **out);
term_line *term_line_free(term_line *line);

#define _term_line_free_ _cleanup_(term_line_freep)
DEFINE_TRIVIAL_CLEANUP_FUNC(term_line*, term_line_free);

int term_line_reserve(term_line *line, unsigned int width, const term_attr *attr, term_age_t age, unsigned int protect_width);
void term_line_set_width(term_line *line, unsigned int width);
void term_line_write(term_line *line, unsigned int pos_x, term_char_t ch, unsigned int cwidth, const term_attr *attr, term_age_t age, bool insert_mode);
void term_line_insert(term_line *line, unsigned int from, unsigned int num, const term_attr *attr, term_age_t age);
void term_line_delete(term_line *line, unsigned int from, unsigned int num, const term_attr *attr, term_age_t age);
void term_line_append_combchar(term_line *line, unsigned int pos_x, uint32_t ucs4, term_age_t age);
void term_line_erase(term_line *line, unsigned int from, unsigned int num, const term_attr *attr, term_age_t age, bool keep_protected);
void term_line_reset(term_line *line, const term_attr *attr, term_age_t age);

void term_line_link(term_line *line, term_line **first, term_line **last);
void term_line_link_tail(term_line *line, term_line **first, term_line **last);
void term_line_unlink(term_line *line, term_line **first, term_line **last);

#define TERM_LINE_LINK(_line, _head) term_line_link((_line), &(_head)->lines_first, &(_head)->lines_last)
#define TERM_LINE_LINK_TAIL(_line, _head) term_line_link_tail((_line), &(_head)->lines_first, &(_head)->lines_last)
#define TERM_LINE_UNLINK(_line, _head) term_line_unlink((_line), &(_head)->lines_first, &(_head)->lines_last)

/*
 * Pages
 * A page represents the 2D table containing all cells of a terminal. It stores
 * lines as an array of pointers so scrolling becomes a simple line-shuffle
 * operation.
 * Scrolling is always targeted only at the scroll-region defined via scroll_idx
 * and scroll_num. The fill-state keeps track of the number of touched lines in
 * the scroll-region. @width and @height describe the visible region of the page
 * and are guaranteed to be allocated at all times.
 */

struct term_page {
        term_age_t age;                 /* page age */

        term_line **lines;              /* array of line-pointers */
        term_line **line_cache;         /* cache for temporary operations */
        unsigned int n_lines;           /* # of allocated lines */

        unsigned int width;             /* width of visible area */
        unsigned int height;            /* height of visible area */
        unsigned int scroll_idx;        /* scrolling-region start index */
        unsigned int scroll_num;        /* scrolling-region length in lines */
        unsigned int scroll_fill;       /* # of valid scroll-lines */
};

int term_page_new(term_page **out);
term_page *term_page_free(term_page *page);

#define _term_page_free_ _cleanup_(term_page_freep)
DEFINE_TRIVIAL_CLEANUP_FUNC(term_page*, term_page_free);

term_cell *term_page_get_cell(term_page *page, unsigned int x, unsigned int y);

int term_page_reserve(term_page *page, unsigned int cols, unsigned int rows, const term_attr *attr, term_age_t age);
void term_page_resize(term_page *page, unsigned int cols, unsigned int rows, const term_attr *attr, term_age_t age, term_history *history);
void term_page_write(term_page *page, unsigned int pos_x, unsigned int pos_y, term_char_t ch, unsigned int cwidth, const term_attr *attr, term_age_t age, bool insert_mode);
void term_page_insert_cells(term_page *page, unsigned int from_x, unsigned int from_y, unsigned int num, const term_attr *attr, term_age_t age);
void term_page_delete_cells(term_page *page, unsigned int from_x, unsigned int from_y, unsigned int num, const term_attr *attr, term_age_t age);
void term_page_append_combchar(term_page *page, unsigned int pos_x, unsigned int pos_y, uint32_t ucs4, term_age_t age);
void term_page_erase(term_page *page, unsigned int from_x, unsigned int from_y, unsigned int to_x, unsigned int to_y, const term_attr *attr, term_age_t age, bool keep_protected);
void term_page_reset(term_page *page, const term_attr *attr, term_age_t age);

void term_page_set_scroll_region(term_page *page, unsigned int idx, unsigned int num);
void term_page_scroll_up(term_page *page, unsigned int num, const term_attr *attr, term_age_t age, term_history *history);
void term_page_scroll_down(term_page *page, unsigned int num, const term_attr *attr, term_age_t age, term_history *history);
void term_page_insert_lines(term_page *page, unsigned int pos_y, unsigned int num, const term_attr *attr, term_age_t age);
void term_page_delete_lines(term_page *page, unsigned int pos_y, unsigned int num, const term_attr *attr, term_age_t age);

/*
 * Histories
 * Scroll-back buffers use term_history objects to store scroll-back lines. A
 * page is independent of the history used. All page operations that modify a
 * history take it as separate argument. You're free to pass NULL at all times
 * if no history should be used.
 * Lines are stored in a linked list as no complex operations are ever done on
 * history lines, besides pushing/poping. Note that history lines do not have a
 * guaranteed minimum length. Any kind of line might be stored there. Missing
 * cells should be cleared to the background color.
 */

struct term_history {
        term_line *lines_first;
        term_line *lines_last;
        unsigned int n_lines;
        unsigned int max_lines;
};

int term_history_new(term_history **out);
term_history *term_history_free(term_history *history);

#define _term_history_free_ _cleanup_(term_history_freep)
DEFINE_TRIVIAL_CLEANUP_FUNC(term_history*, term_history_free);

void term_history_clear(term_history *history);
void term_history_trim(term_history *history, unsigned int max);
void term_history_push(term_history *history, term_line *line);
term_line *term_history_pop(term_history *history, unsigned int reserve_width, const term_attr *attr, term_age_t age);
unsigned int term_history_peek(term_history *history, unsigned int max, unsigned int reserve_width, const term_attr *attr, term_age_t age);

/*
 * UTF-8
 * The UTF-decoder and encoder are adjusted for terminals and provide proper
 * fallbacks for invalid UTF-8. In terminals it's quite usual to use fallbacks
 * instead of rejecting invalid input. This way, old legacy applications still
 * work (this is especially important for 7bit/ASCII DEC modes).
 */

struct term_utf8 {
        uint32_t chars[5];
        uint32_t ucs4;

        unsigned int i_bytes : 3;
        unsigned int n_bytes : 3;
        unsigned int valid : 1;
};

size_t term_utf8_encode(char *out_utf8, uint32_t g);
const uint32_t *term_utf8_decode(term_utf8 *p, size_t *out_len, char c);

/*
 * Parsers
 * The term_parser object parses control-sequences for both host and terminal
 * side. Based on this parser, there is a set of command-parsers that take a
 * term_seq sequence and returns the command it represents. This is different
 * for host and terminal side so a different set of parsers is provided.
 */

enum {
        TERM_SEQ_NONE,                  /* placeholder, no sequence parsed */

        TERM_SEQ_IGNORE,                /* no-op character */
        TERM_SEQ_GRAPHIC,               /* graphic character */
        TERM_SEQ_CONTROL,               /* control character */
        TERM_SEQ_ESCAPE,                /* escape sequence */
        TERM_SEQ_CSI,                   /* control sequence function */
        TERM_SEQ_DCS,                   /* device control string */
        TERM_SEQ_OSC,                   /* operating system control */

        TERM_SEQ_CNT
};

enum {
        /* these must be kept compatible to (1U << (ch - 0x20)) */

        TERM_SEQ_FLAG_SPACE             = (1U <<  0),   /* char:   */
        TERM_SEQ_FLAG_BANG              = (1U <<  1),   /* char: ! */
        TERM_SEQ_FLAG_DQUOTE            = (1U <<  2),   /* char: " */
        TERM_SEQ_FLAG_HASH              = (1U <<  3),   /* char: # */
        TERM_SEQ_FLAG_CASH              = (1U <<  4),   /* char: $ */
        TERM_SEQ_FLAG_PERCENT           = (1U <<  5),   /* char: % */
        TERM_SEQ_FLAG_AND               = (1U <<  6),   /* char: & */
        TERM_SEQ_FLAG_SQUOTE            = (1U <<  7),   /* char: ' */
        TERM_SEQ_FLAG_POPEN             = (1U <<  8),   /* char: ( */
        TERM_SEQ_FLAG_PCLOSE            = (1U <<  9),   /* char: ) */
        TERM_SEQ_FLAG_MULT              = (1U << 10),   /* char: * */
        TERM_SEQ_FLAG_PLUS              = (1U << 11),   /* char: + */
        TERM_SEQ_FLAG_COMMA             = (1U << 12),   /* char: , */
        TERM_SEQ_FLAG_MINUS             = (1U << 13),   /* char: - */
        TERM_SEQ_FLAG_DOT               = (1U << 14),   /* char: . */
        TERM_SEQ_FLAG_SLASH             = (1U << 15),   /* char: / */

        /* 16-35 is reserved for numbers; unused */

        /* COLON is reserved            = (1U << 26),      char: : */
        /* SEMICOLON is reserved        = (1U << 27),      char: ; */
        TERM_SEQ_FLAG_LT                = (1U << 28),   /* char: < */
        TERM_SEQ_FLAG_EQUAL             = (1U << 29),   /* char: = */
        TERM_SEQ_FLAG_GT                = (1U << 30),   /* char: > */
        TERM_SEQ_FLAG_WHAT              = (1U << 31),   /* char: ? */
};

enum {
        TERM_CMD_NONE,                          /* placeholder */
        TERM_CMD_GRAPHIC,                       /* graphics character */

        TERM_CMD_BEL,                           /* bell */
        TERM_CMD_BS,                            /* backspace */
        TERM_CMD_CBT,                           /* cursor-backward-tabulation */
        TERM_CMD_CHA,                           /* cursor-horizontal-absolute */
        TERM_CMD_CHT,                           /* cursor-horizontal-forward-tabulation */
        TERM_CMD_CNL,                           /* cursor-next-line */
        TERM_CMD_CPL,                           /* cursor-previous-line */
        TERM_CMD_CR,                            /* carriage-return */
        TERM_CMD_CUB,                           /* cursor-backward */
        TERM_CMD_CUD,                           /* cursor-down */
        TERM_CMD_CUF,                           /* cursor-forward */
        TERM_CMD_CUP,                           /* cursor-position */
        TERM_CMD_CUU,                           /* cursor-up */
        TERM_CMD_DA1,                           /* primary-device-attributes */
        TERM_CMD_DA2,                           /* secondary-device-attributes */
        TERM_CMD_DA3,                           /* tertiary-device-attributes */
        TERM_CMD_DC1,                           /* device-control-1 */
        TERM_CMD_DC3,                           /* device-control-3 */
        TERM_CMD_DCH,                           /* delete-character */
        TERM_CMD_DECALN,                        /* screen-alignment-pattern */
        TERM_CMD_DECANM,                        /* ansi-mode */
        TERM_CMD_DECBI,                         /* back-index */
        TERM_CMD_DECCARA,                       /* change-attributes-in-rectangular-area */
        TERM_CMD_DECCRA,                        /* copy-rectangular-area */
        TERM_CMD_DECDC,                         /* delete-column */
        TERM_CMD_DECDHL_BH,                     /* double-width-double-height-line: bottom half */
        TERM_CMD_DECDHL_TH,                     /* double-width-double-height-line: top half */
        TERM_CMD_DECDWL,                        /* double-width-single-height-line */
        TERM_CMD_DECEFR,
        TERM_CMD_DECELF,
        TERM_CMD_DECELR,
        TERM_CMD_DECERA,
        TERM_CMD_DECFI,
        TERM_CMD_DECFRA,
        TERM_CMD_DECIC,
        TERM_CMD_DECID,
        TERM_CMD_DECINVM,
        TERM_CMD_DECKBD,
        TERM_CMD_DECKPAM,
        TERM_CMD_DECKPNM,
        TERM_CMD_DECLFKC,
        TERM_CMD_DECLL,
        TERM_CMD_DECLTOD,
        TERM_CMD_DECPCTERM,
        TERM_CMD_DECPKA,
        TERM_CMD_DECPKFMR,
        TERM_CMD_DECRARA,
        TERM_CMD_DECRC,
        TERM_CMD_DECREQTPARM,
        TERM_CMD_DECRPKT,
        TERM_CMD_DECRQCRA,
        TERM_CMD_DECRQDE,
        TERM_CMD_DECRQKT,
        TERM_CMD_DECRQLP,
        TERM_CMD_DECRQM_ANSI,
        TERM_CMD_DECRQM_DEC,
        TERM_CMD_DECRQPKFM,
        TERM_CMD_DECRQPSR,
        TERM_CMD_DECRQTSR,
        TERM_CMD_DECRQUPSS,
        TERM_CMD_DECSACE,
        TERM_CMD_DECSASD,
        TERM_CMD_DECSC,
        TERM_CMD_DECSCA,
        TERM_CMD_DECSCL,
        TERM_CMD_DECSCP,
        TERM_CMD_DECSCPP,
        TERM_CMD_DECSCS,
        TERM_CMD_DECSCUSR,
        TERM_CMD_DECSDDT,
        TERM_CMD_DECSDPT,
        TERM_CMD_DECSED,
        TERM_CMD_DECSEL,
        TERM_CMD_DECSERA,
        TERM_CMD_DECSFC,
        TERM_CMD_DECSKCV,
        TERM_CMD_DECSLCK,
        TERM_CMD_DECSLE,
        TERM_CMD_DECSLPP,
        TERM_CMD_DECSLRM_OR_SC,
        TERM_CMD_DECSMBV,
        TERM_CMD_DECSMKR,
        TERM_CMD_DECSNLS,
        TERM_CMD_DECSPP,
        TERM_CMD_DECSPPCS,
        TERM_CMD_DECSPRTT,
        TERM_CMD_DECSR,
        TERM_CMD_DECSRFR,
        TERM_CMD_DECSSCLS,
        TERM_CMD_DECSSDT,
        TERM_CMD_DECSSL,
        TERM_CMD_DECST8C,
        TERM_CMD_DECSTBM,
        TERM_CMD_DECSTR,
        TERM_CMD_DECSTRL,
        TERM_CMD_DECSWBV,
        TERM_CMD_DECSWL,
        TERM_CMD_DECTID,
        TERM_CMD_DECTME,
        TERM_CMD_DECTST,
        TERM_CMD_DL,
        TERM_CMD_DSR_ANSI,
        TERM_CMD_DSR_DEC,
        TERM_CMD_ECH,
        TERM_CMD_ED,
        TERM_CMD_EL,
        TERM_CMD_ENQ,
        TERM_CMD_EPA,
        TERM_CMD_FF,
        TERM_CMD_HPA,
        TERM_CMD_HPR,
        TERM_CMD_HT,
        TERM_CMD_HTS,
        TERM_CMD_HVP,
        TERM_CMD_ICH,
        TERM_CMD_IL,
        TERM_CMD_IND,
        TERM_CMD_LF,
        TERM_CMD_LS1R,
        TERM_CMD_LS2,
        TERM_CMD_LS2R,
        TERM_CMD_LS3,
        TERM_CMD_LS3R,
        TERM_CMD_MC_ANSI,
        TERM_CMD_MC_DEC,
        TERM_CMD_NEL,
        TERM_CMD_NP,
        TERM_CMD_NULL,
        TERM_CMD_PP,
        TERM_CMD_PPA,
        TERM_CMD_PPB,
        TERM_CMD_PPR,
        TERM_CMD_RC,
        TERM_CMD_REP,
        TERM_CMD_RI,
        TERM_CMD_RIS,
        TERM_CMD_RM_ANSI,
        TERM_CMD_RM_DEC,
        TERM_CMD_S7C1T,
        TERM_CMD_S8C1T,
        TERM_CMD_SCS,
        TERM_CMD_SD,
        TERM_CMD_SGR,
        TERM_CMD_SI,
        TERM_CMD_SM_ANSI,
        TERM_CMD_SM_DEC,
        TERM_CMD_SO,
        TERM_CMD_SPA,
        TERM_CMD_SS2,
        TERM_CMD_SS3,
        TERM_CMD_ST,
        TERM_CMD_SU,
        TERM_CMD_SUB,
        TERM_CMD_TBC,
        TERM_CMD_VPA,
        TERM_CMD_VPR,
        TERM_CMD_VT,
        TERM_CMD_XTERM_CLLHP,                   /* xterm-cursor-lower-left-hp-bugfix */
        TERM_CMD_XTERM_IHMT,                    /* xterm-initiate-highlight-mouse-tracking*/
        TERM_CMD_XTERM_MLHP,                    /* xterm-memory-lock-hp-bugfix */
        TERM_CMD_XTERM_MUHP,                    /* xterm-memory-unlock-hp-bugfix */
        TERM_CMD_XTERM_RPM,                     /* xterm-restore-private-mode */
        TERM_CMD_XTERM_RRV,                     /* xterm-reset-resource-value */
        TERM_CMD_XTERM_RTM,                     /* xterm-reset-title-mode */
        TERM_CMD_XTERM_SACL1,                   /* xterm-set-ansi-conformance-level-1 */
        TERM_CMD_XTERM_SACL2,                   /* xterm-set-ansi-conformance-level-2 */
        TERM_CMD_XTERM_SACL3,                   /* xterm-set-ansi-conformance-level-3 */
        TERM_CMD_XTERM_SDCS,                    /* xterm-set-default-character-set */
        TERM_CMD_XTERM_SGFX,                    /* xterm-sixel-graphics */
        TERM_CMD_XTERM_SPM,                     /* xterm-set-private-mode */
        TERM_CMD_XTERM_SRV,                     /* xterm-set-resource-value */
        TERM_CMD_XTERM_STM,                     /* xterm-set-title-mode */
        TERM_CMD_XTERM_SUCS,                    /* xterm-set-utf8-character-set */
        TERM_CMD_XTERM_WM,                      /* xterm-window-management */

        TERM_CMD_CNT
};

enum {
        /*
         * Charsets: DEC marks charsets according to "Digital Equ. Corp.".
         *           NRCS marks charsets according to the "National Replacement
         *           Character Sets". ISO marks charsets according to ISO-8859.
         * The USERDEF charset is special and can be modified by the host.
         */

        TERM_CHARSET_NONE,

        /* 96-compat charsets */
        TERM_CHARSET_ISO_LATIN1_SUPPLEMENTAL,
        TERM_CHARSET_BRITISH_NRCS = TERM_CHARSET_ISO_LATIN1_SUPPLEMENTAL,
        TERM_CHARSET_ISO_LATIN2_SUPPLEMENTAL,
        TERM_CHARSET_AMERICAN_NRCS = TERM_CHARSET_ISO_LATIN2_SUPPLEMENTAL,
        TERM_CHARSET_ISO_LATIN5_SUPPLEMENTAL,
        TERM_CHARSET_ISO_GREEK_SUPPLEMENTAL,
        TERM_CHARSET_ISO_HEBREW_SUPPLEMENTAL,
        TERM_CHARSET_ISO_LATIN_CYRILLIC,

        TERM_CHARSET_96_CNT,

        /* 94-compat charsets */
        TERM_CHARSET_DEC_SPECIAL_GRAPHIC = TERM_CHARSET_96_CNT,
        TERM_CHARSET_DEC_SUPPLEMENTAL,
        TERM_CHARSET_DEC_TECHNICAL,
        TERM_CHARSET_CYRILLIC_DEC,
        TERM_CHARSET_DUTCH_NRCS,
        TERM_CHARSET_FINNISH_NRCS,
        TERM_CHARSET_FRENCH_NRCS,
        TERM_CHARSET_FRENCH_CANADIAN_NRCS,
        TERM_CHARSET_GERMAN_NRCS,
        TERM_CHARSET_GREEK_DEC,
        TERM_CHARSET_GREEK_NRCS,
        TERM_CHARSET_HEBREW_DEC,
        TERM_CHARSET_HEBREW_NRCS,
        TERM_CHARSET_ITALIAN_NRCS,
        TERM_CHARSET_NORWEGIAN_DANISH_NRCS,
        TERM_CHARSET_PORTUGUESE_NRCS,
        TERM_CHARSET_RUSSIAN_NRCS,
        TERM_CHARSET_SCS_NRCS,
        TERM_CHARSET_SPANISH_NRCS,
        TERM_CHARSET_SWEDISH_NRCS,
        TERM_CHARSET_SWISS_NRCS,
        TERM_CHARSET_TURKISH_DEC,
        TERM_CHARSET_TURKISH_NRCS,

        TERM_CHARSET_94_CNT,

        /* special charsets */
        TERM_CHARSET_USERPREF_SUPPLEMENTAL = TERM_CHARSET_94_CNT,

        TERM_CHARSET_CNT,
};

extern term_charset term_unicode_lower;
extern term_charset term_unicode_upper;
extern term_charset term_dec_supplemental_graphics;
extern term_charset term_dec_special_graphics;

#define TERM_PARSER_ARG_MAX (16)
#define TERM_PARSER_ST_MAX (4096)

struct term_seq {
        unsigned int type;
        unsigned int command;
        uint32_t terminator;
        unsigned int intermediates;
        unsigned int charset;
        unsigned int n_args;
        int args[TERM_PARSER_ARG_MAX];
        unsigned int n_st;
        char *st;
};

struct term_parser {
        term_seq seq;
        size_t st_alloc;
        unsigned int state;

        bool is_host : 1;
};

int term_parser_new(term_parser **out, bool host);
term_parser *term_parser_free(term_parser *parser);
int term_parser_feed(term_parser *parser, const term_seq **seq_out, uint32_t raw);

#define _term_parser_free_ _cleanup_(term_parser_freep)
DEFINE_TRIVIAL_CLEANUP_FUNC(term_parser*, term_parser_free);