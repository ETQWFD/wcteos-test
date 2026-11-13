#include "efi.h"

#define PS2_DATA_PORT        0x60
#define PS2_STATUS_PORT      0x64

#define PS2_ST_OUTPUT_FULL   0x01
#define PS2_ST_INPUT_FULL    0x02
#define PS2_ST_AUX_DATA      0x20

#define PS2_CMD_READ_CFG     0x20
#define PS2_CMD_WRITE_CFG    0x60
#define PS2_CMD_DISABLE_KBD  0xAD
#define PS2_CMD_DISABLE_AUX  0xA7
#define PS2_CMD_ENABLE_AUX   0xA8
#define PS2_CMD_WRITE_AUX    0xD4

#define PS2_ACK              0xFA
#define PS2_RESEND           0xFE
#define PS2_ERROR            0xFC

#define PS2_CFG_KBD_IRQ      0x01
#define PS2_CFG_AUX_IRQ      0x02
#define PS2_CFG_KBD_CLK_OFF  0x10
#define PS2_CFG_AUX_CLK_OFF  0x20

#define MOUSE_SET_DEFAULTS   0xF6
#define MOUSE_SET_SAMPLE     0xF3
#define MOUSE_SET_RES        0xE8
#define MOUSE_ENABLE         0xF4

#define PS2_TIMEOUT          100000
#define PS2_ACK_RETRIES      3

static inline unsigned char p_inb(unsigned short p)
{
    unsigned char v;
    __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void p_outb(unsigned short p, unsigned char v)
{
    __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(p));
}
static void io_wait(void) { p_outb(0x80, 0); }

static int ps2_wait_write(void)
{
    for (int t = 0; t < PS2_TIMEOUT; ++t)
        if (!(p_inb(PS2_STATUS_PORT) & PS2_ST_INPUT_FULL)) return 1;
    return 0;
}
static int ps2_wait_read(void)
{
    for (int t = 0; t < PS2_TIMEOUT; ++t)
        if (p_inb(PS2_STATUS_PORT) & PS2_ST_OUTPUT_FULL) return 1;
    return 0;
}
static int ps2_cmd(unsigned char cmd)
{
    if (!ps2_wait_write()) return 0;
    p_outb(PS2_STATUS_PORT, cmd);
    io_wait();
    return 1;
}
static int ps2_cmd_arg(unsigned char cmd, unsigned char arg)
{
    if (!ps2_cmd(cmd)) return 0;
    if (!ps2_wait_write()) return 0;
    p_outb(PS2_DATA_PORT, arg);
    io_wait();
    return 1;
}
static int ps2_flush_output(void)
{
    int n = 0;
    while ((p_inb(PS2_STATUS_PORT) & PS2_ST_OUTPUT_FULL) && n++ < 64)
        (void)p_inb(PS2_DATA_PORT);
    return n;
}
static int ps2_read_ack(void)
{
    for (int t = 0; t < 16; ++t) {
        if (!ps2_wait_read()) return 0;
        unsigned char v = p_inb(PS2_DATA_PORT);
        if (v == PS2_ACK)    return 1;
        if (v == PS2_RESEND) return 0;
        if (v == PS2_ERROR)  return 0;
    }
    return 0;
}

static int ps2_aux_write(unsigned char v)
{
    if (!ps2_cmd(PS2_CMD_WRITE_AUX)) return 0;
    if (!ps2_wait_write()) return 0;
    p_outb(PS2_DATA_PORT, v);
    io_wait();
    return 1;
}
static int ps2_aux_send(unsigned char v)
{
    for (int i = 0; i < PS2_ACK_RETRIES; ++i) {
        if (!ps2_aux_write(v)) continue;
        for (int t = 0; t < 16; ++t) {
            if (!ps2_wait_read()) break;
            unsigned char r = p_inb(PS2_DATA_PORT);
            if (r == PS2_ACK)    return 1;
            if (r == PS2_RESEND) break;
            if (r == PS2_ERROR)  return 0;
        }
    }
    return 0;
}

static unsigned char g_pkt[3];
static int g_pkt_i = 0;
static int g_ready = 0;
static int g_overflow = 0;

int ps2_mouse_init(void)
{
    unsigned char old_cfg = 0, cfg;

    ps2_cmd(PS2_CMD_DISABLE_KBD);
    ps2_cmd(PS2_CMD_DISABLE_AUX);
    ps2_flush_output();

    if (!ps2_cmd(PS2_CMD_READ_CFG)) return 0;
    if (!ps2_wait_read()) return 0;
    old_cfg = p_inb(PS2_DATA_PORT);

    cfg = old_cfg;
    cfg &= (unsigned char)~(PS2_CFG_KBD_IRQ | PS2_CFG_AUX_IRQ);
    cfg &= (unsigned char)~(PS2_CFG_KBD_CLK_OFF | PS2_CFG_AUX_CLK_OFF);
    if (!ps2_cmd_arg(PS2_CMD_WRITE_CFG, cfg)) return 0;

    if (!ps2_cmd(PS2_CMD_ENABLE_AUX)) goto fail_cfg;

    if (!ps2_aux_send(MOUSE_SET_DEFAULTS)) goto fail_cfg;

    if (!ps2_aux_send(MOUSE_SET_SAMPLE)) goto fail_cfg;
    if (!ps2_aux_send(100))              goto fail_cfg;

    if (!ps2_aux_send(MOUSE_SET_RES)) goto fail_cfg;
    if (!ps2_aux_send(3))             goto fail_cfg;

    if (!ps2_aux_send(MOUSE_ENABLE)) goto fail_cfg;

    ps2_flush_output();

    g_pkt_i    = 0;
    g_overflow = 0;
    g_ready    = 1;
    return 1;

fail_cfg:
    ps2_cmd_arg(PS2_CMD_WRITE_CFG, old_cfg);
    g_ready = 0;
    return 0;
}

int ps2_mouse_ready(void) { return g_ready; }

int ps2_mouse_poll(int *dx, int *dy, int *btn)
{
    if (!g_ready) return 0;
    int guard = 0;
    while (guard++ < 64) {
        unsigned char st = p_inb(PS2_STATUS_PORT);
        if (!(st & PS2_ST_OUTPUT_FULL)) break;

        unsigned char v = p_inb(PS2_DATA_PORT);

        if (!(st & PS2_ST_AUX_DATA)) continue;

        if (g_pkt_i == 0 && !(v & 0x08)) { g_pkt_i = 0; continue; }

        g_pkt[g_pkt_i++] = v;
        if (g_pkt_i != 3) continue;

        g_pkt_i = 0;
        int b = g_pkt[0];

        if (b & 0xC0) { g_overflow = 1; continue; }

        int ddx = (int)g_pkt[1] - ((b & 0x10) ? 256 : 0);
        int ddy = (int)g_pkt[2] - ((b & 0x20) ? 256 : 0);

        *dx  = ddx;
        *dy  = -ddy;
        *btn = (b & 0x01) | ((b & 0x04) >> 1) | ((b & 0x02) << 1);
        return 1;
    }
    return 0;
}

int ps2_mouse_overflowed(void) { int o = g_overflow; g_overflow = 0; return o; }

int ps2_mouse_step(void) { return g_ready; }