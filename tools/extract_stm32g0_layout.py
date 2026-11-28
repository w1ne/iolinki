#!/usr/bin/env python3
"""Extract only the explicit firmware fixture counter layout from its ELF DWARF."""
import hashlib, json, re, subprocess, sys
from pathlib import Path
FIELDS=["enforce_timing","min_cycle_time_us","t_ren_limit_us","crc_errors","timeout_errors","framing_errors","timing_errors","t_ren_violations","t_cycle_violations","t_byte_violations"]
def extract(elf):
    text=subprocess.check_output(["readelf","--debug-dump=info",str(elf)],text=True)
    dies={}; order=[]; current=None
    for line in text.splitlines():
        m=re.match(r"\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: \d+(?: \(([^)]+)\))?",line)
        if m:
            current={"depth":int(m[1]),"id":int(m[2],16),"tag":m[3],"attrs":{}}
            dies[current["id"]]=current;order.append(current);continue
        if current is not None:
            m=re.search(r"DW_AT_(\w+)\s*:\s*(.*)",line)
            if m:current["attrs"][m[1]]=m[2]
    def name(d):return d["attrs"].get("name","").split(": ")[-1]
    def ref(d):
        m=re.search(r"<0x([0-9a-f]+)>",d["attrs"].get("type",""))
        if not m:raise ValueError("missing DWARF type")
        return dies[int(m[1],16)]
    def structure(tname):
        d=next(x for x in order if x["tag"]=="DW_TAG_typedef" and name(x)==tname)
        for _ in range(16):
            d=ref(d)
            if d["tag"]=="DW_TAG_structure_type":return d
        raise ValueError(tname)
    def members(d):
        out={}
        for x in order[order.index(d)+1:]:
            if x["depth"]<=d["depth"]:break
            if x["depth"]==d["depth"]+1 and x["tag"]=="DW_TAG_member":out[name(x)]=x
        return out
    def size(d):
        for _ in range(16):
            if "byte_size" in d["attrs"]:return int(d["attrs"]["byte_size"])
            d=ref(d)
        raise ValueError("unresolved size")
    device=structure("iolink_device_ctx_t");dll=structure("iolink_dll_ctx_t")
    base=int(members(device)["dll"]["attrs"]["data_member_location"])
    fields=members(dll)
    offsets={"sizeof_device":size(device),"sizeof_dll":size(dll),"offsetof_dll":base}
    widths={}
    for key in FIELDS:
        offsets["dll."+key]=base+int(fields[key]["attrs"]["data_member_location"])
        widths["dll."+key]=size(ref(fields[key]))
    dma=structure("board_dma_stats_t")
    dma_fields={key:{"offset":int(field["attrs"]["data_member_location"]),"width":size(ref(field))} for key,field in members(dma).items()}
    comment=subprocess.check_output(["readelf","-p",".comment",str(elf)],text=True).strip()
    return {"elf_sha256":hashlib.sha256(elf.read_bytes()).hexdigest(),"elf":str(elf.resolve()),"verification":"Offsets and widths extracted from this exact ELF's compiler-generated DWARF; the reader must verify elf_sha256 before using them","compiler_comment":comment,"board_dma_stats":{"size_bytes":size(dma),"fields":dma_fields},"board_uart_backend":{"width":4},"offsets":offsets,"field_width_bytes":widths}
if __name__=="__main__":
    for arg in sys.argv[1:]:
        elf=Path(arg);data=extract(elf);out=elf.parent/"arm-eabi-layout.json"
        out.write_text(json.dumps(data,indent=2)+"\n")
        print(str(out),data["elf_sha256"],data["offsets"]["dll.crc_errors"])
