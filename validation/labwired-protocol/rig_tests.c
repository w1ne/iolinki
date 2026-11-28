/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent observable regression harness for the actual iolinki C stack.
 * Virtual PHY injection tests protocol behavior; its synthetic clock is not tA.
 */
#include "rig_tests.h"
#include "iolinki/device.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t logical_us = 1000;
uint64_t iolink_time_get_us(void) { return logical_us; }
uint32_t iolink_time_get_ms(void) { return (uint32_t)(logical_us / 1000U); }
static unsigned checks, failures, selected, replies;
static uint8_t blocks[256][32];
typedef struct { uint8_t rx[64], tx[64]; size_t count, pos, tx_count; } link_t;
static link_t link;
static iolink_device_ctx_t device;
static iolink_device_config_t config;

// A.1.6 parity-matrix oracle, separate from the stack's checksum function.
static uint8_t parity(uint8_t x) {
  x ^= (uint8_t)(x >> 4); x ^= (uint8_t)(x >> 2); x ^= (uint8_t)(x >> 1);
  return x & 1U;
}
static uint8_t checksum(const uint8_t *bytes, size_t n) {
  static const uint8_t rows[6] = {0x03,0x0c,0x30,0xc0,0x55,0xaa};
  uint8_t x = 0x52, result = 0;
  for (size_t i=0;i<n;i++) x ^= bytes[i];
  for (unsigned bit=0;bit<6;bit++) result |= (uint8_t)(parity(x & rows[bit]) << bit);
  return result;
}
static uint8_t payload(size_t offset) {
  return (uint8_t)((offset * 13U) ^ (offset >> 3U) ^ 0x5aU);
}
static void check(int condition, const char *name) {
  checks++;
  if (!condition) { failures++; rig_emit(name); }
}
static int receive(void *u, uint8_t *b) {
  link_t *l = u;
  if (l->pos == l->count) return 0;
  *b = l->rx[l->pos++]; return 1;
}
static int send_reply(void *u, const uint8_t *data, size_t n) {
  link_t *l = u;
  rig_stage(2);
  if (n > sizeof(l->tx)) return -1;
  memcpy(l->tx, data, n); l->tx_count = n; replies++;
  return (int)n;
}
static const uint8_t *select_input(void *u,const uint8_t *out,uint8_t out_len,
                                  uint8_t in_len,bool *valid) {
  (void)u; selected++;
  if (out_len != 1 || in_len != 32) { *valid=false; return NULL; }
  *valid=true; return blocks[out[0]];
}
static void inject(const uint8_t *frame,size_t n) {
  memcpy(link.rx,frame,n); link.count=n; link.pos=0; link.tx_count=0;
  logical_us += 29412U; // Logical vector spacing only: no throughput claim.
  rig_stage(1); iolink_device_process(&device); rig_stage(0);
}
static void init_protocol(iolink_baudrate_t speed) {
  memset(&link,0,sizeof(link)); memset(&config,0,sizeof(config));
  config.phy.user=&link; config.phy.recv_byte=receive; config.phy.send=send_reply;
  config.stack.m_seq_type=IOLINK_M_SEQ_TYPE_2_V;
  config.stack.pd_in_len=32; config.stack.pd_out_len=1; config.stack.od_len=1;
  config.pd_input_select=select_input;
  check(iolink_device_init(&device,&config)==0,"FAIL init");
  check(iolink_dll_set_baudrate(&device.dll,speed)==0,"FAIL baudrate select");
  // Explicitly scope these vectors to OPERATE. Bring-up is a separate lane.
  device.dll.state=IOLINK_DLL_STATE_OPERATE;
  check(device.dll.od_len==1,"FAIL OD width");
  check(((iolink_isdu_direct_param_page1_octet(&device.dll.isdu,3)>>1)&7)==4,
        "FAIL advertised M-sequence code");
}
static void process_data(iolink_baudrate_t speed,const char *name) {
  init_protocol(speed);
  unsigned start_fail=failures;
  for (unsigned mode=0;mode<2;mode++) {
    for (unsigned i=0;i<100;i++) {
      uint8_t selector=(uint8_t)((i*73U+mode*17U+7U)&255U);
      uint8_t frame[4]={(uint8_t)(mode ? 0 : 0x80),0x80,selector,0};
      size_t n=mode ? 4 : 3;
      frame[1]|=checksum(frame,n);
      unsigned calls=selected;
      inject(frame,n);
      size_t expected=mode ? 33 : 34;
      check(link.tx_count==expected,"FAIL reply length");
      check(selected==calls+1,"FAIL selector count");
      if(link.tx_count==expected) {
        check(memcmp(link.tx+(mode ? 0 : 1),blocks[selector],32)==0,
              "FAIL same-cycle 32-byte payload");
        uint8_t cks=link.tx[expected-1];
        link.tx[expected-1]&=0xc0;
        check((cks&0x3f)==checksum(link.tx,expected),"FAIL reply checksum");
        check((cks&0x40)==0,"FAIL PD invalid");
      }
    }
  }
  char s[120];
  snprintf(s,sizeof(s),"%s same-cycle: 100 read + 100 write; failures=%u",name,failures-start_fail);
  rig_emit(s);
  uint8_t bad[]={0x80,0x80,0x55}; bad[1]|=checksum(bad,sizeof(bad)); bad[1]^=1;
  unsigned calls=selected, before=replies;
  inject(bad,sizeof(bad));
  check(link.tx_count==0 && replies==before,"FAIL corrupt frame replied");
  check(selected==calls,"FAIL corrupt frame reached app");
  rig_emit("Negative control: bad CKT must produce no response and no selector call.");
}

static size_t vendor_size;
static unsigned vendor_calls;
static uint16_t vendor_index;
static uint8_t vendor(void *u,uint16_t index,uint8_t sub,bool write,
                      const uint8_t *input,size_t n,uint8_t *output,size_t *capacity) {
  (void)u; (void)sub; (void)input; (void)n;
  vendor_calls++; vendor_index=index;
  if (write) { *capacity=0; return 0; }
  size_t count=vendor_size < *capacity ? vendor_size : *capacity;
  size_t base=(size_t)(index-0x100U)*232U;
  for(size_t i=0;i<count;i++) output[i]=payload(base+i);
  *capacity=vendor_size; return 0;
}
static void request_read(iolink_isdu_ctx_t *s,uint16_t index) {
  uint8_t q[]={0xb5,(uint8_t)(index>>8),(uint8_t)index,0,0};
  q[4]=q[0]^q[1]^q[2]^q[3];
  for(unsigned i=0;i<sizeof(q);i++)
    iolink_isdu_od_write(s,i ? (uint8_t)i : IOLINK_FLOWCTRL_START,q+i,1);
}
static iolink_isdu_ctx_t service;
static uint8_t collected[256];
static void isdu_tests(void) {
  unsigned start_fail=failures;
  unsigned total=0;
  for(unsigned page=0;page<5;page++) {
    iolink_isdu_init(&service); service.vendor_service=vendor;
    vendor_size=page==4 ? 96 : 232;
    request_read(&service,(uint16_t)(0x100U+page));
    uint8_t busy=0;
    iolink_isdu_od_read(&service,IOLINK_FLOWCTRL_START,&busy,1);
    check(busy==1,"FAIL Busy octet before dispatch");
    iolink_isdu_process(&service);
    check(vendor_index==0x100U+page,"FAIL 16-bit index");
    check(service.response_len==vendor_size,"FAIL ISDU payload length");
    size_t length=service.resp_total;
    check(length<=238 && length<=sizeof(collected),"FAIL ISDU wire bound");
    if(length>sizeof(collected)) continue;
    for(size_t i=0;i<length;i++) {
      uint8_t flow=i ? (uint8_t)(i&15U) : IOLINK_FLOWCTRL_START;
      iolink_isdu_od_read(&service,flow,collected+i,1);
      if(i==1 || i==length-1) {
        uint8_t repeated=0;
        iolink_isdu_od_read(&service,flow,&repeated,1);
        check(repeated==collected[i],"FAIL duplicate response replay");
      }
    }
    size_t h=(collected[0]&15U)==1 ? 2 : 1;
    check(length==vendor_size+h+1,"FAIL ISDU encoded length");
    uint8_t x=0;for(size_t i=0;i<length;i++)x^=collected[i];
    check(x==0,"FAIL ISDU CHKPDU");
    for(size_t i=0;i<vendor_size && i+h<length;i++)
      check(collected[i+h]==payload(total+i),"FAIL ISDU reconstructed data");
    total+=(unsigned)vendor_size;
    uint8_t idle=0;iolink_isdu_od_read(&service,IOLINK_FLOWCTRL_IDLE,&idle,1);
  }
  check(total==1024,"FAIL reconstructed 1KiB size");
  iolink_isdu_init(&service);service.vendor_service=vendor;vendor_size=233;
  request_read(&service,0x1234);iolink_isdu_process(&service);
  check(service.response_len==2 && service.response_buf[0]==0x80,
        "FAIL 233-byte read rejection");
  char s[120];snprintf(s,sizeof(s),"ISDU: 1024 bytes in 5 services; 232-byte bound, index, busy, replay; failures=%u",failures-start_fail);
  rig_emit(s);
}
int rig_run(void) {
  checks=failures=selected=replies=0;logical_us=1000;
  for(unsigned s=0;s<256;s++)for(unsigned i=0;i<32;i++)blocks[s][i]=payload(s*32U+i);
  rig_emit("TYPE_2_V code4 / PDout1 / PDin32 / OD1");
  process_data(IOLINK_BAUDRATE_COM3,"COM3 configuration");
  process_data(IOLINK_BAUDRATE_COM2,"COM2 configuration");
  isdu_tests();
  char text[120];snprintf(text,sizeof(text),"RIG_RESULT checks=%u failures=%u replies=%u selectors=%u",checks,failures,replies,selected);
  rig_emit(text);
  rig_emit(failures==0 ? "PROTOCOL_REGRESSION_PASS" : "PROTOCOL_REGRESSION_FAIL");
  rig_emit("Timing/IAR/physical master: separate evidence required.");
  return (int)failures;
}
