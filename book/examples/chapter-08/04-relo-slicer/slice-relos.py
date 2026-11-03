#!/usr/bin/env python3
# slice-relos.py —— 第 8 章 04-relo-slicer:.BTF.ext 重定位记录切片器(python 五站)
#
# 【用途与输入】
#   用法:python3 slice-relos.py <path/to/.bpf.o>
#   把 clang 编出的 BPF 目标文件(object)里的 .BTF.ext 段逐字节切开。CO-RE
#   (Compile Once - Run Everywhere,一次编译、处处运行)在编译期给每处字段访问
#   登记「重定位记录」(relocation record:记下哪条指令的立即数要按目标内核改写、
#   记的是哪个类型哪个字段、记什么量),这些记录就藏在 .BTF.ext 的 core_relo 小节;
#   加载期 libbpf/内核 CO-RE 引擎按记录逐条改写。本脚本让学生不猜就能看到
#   「源码一行 BPF_CORE_READ,对应 .BTF.ext 一条记录」的机械对应链。
#   输入:一个 ELF 目标文件路径。本章验证输入是 ../03-core-read/core.bpf.o
#   (本机实测:4 条记录,3 条字节偏移 + 1 条字段存在性)。
#
# 【输出结构】
#   五站,每站一行「## 站 n:中文站名」标题;站内 OK/PASS/WARN/FAIL 前缀分档
#   (与 ../00-env/check-env.sh 同款口径):
#     站 1  ELF 段布局:.BTF/.BTF.ext/.rel.BTF.ext 尺寸(llvm-objdump -h 与脚本自解析互验)
#     站 2  btf_ext 头:magic 0xEB9F/长度/各段偏移(func_info/line_info/core_relo)
#     站 3  core_relo 记录解析:insn_off/type_id/access_str_off/kind 四字段逐条切片
#     站 4  kind 对照 uapi 枚举:0–11 名称表 + 逐条判定 + 分布
#     站 5  access string 解码:字符串表偏移 → 0:1:2 访问链 → BTF 成员名 → 源码字段对照
#   末尾「## 汇总」给一行机器可读的 RELOS=<记录数>(grep '^RELOS=' 可取)。
#
# 【依赖与降级行为】
#   - 仅 python3 标准库(struct/subprocess/sys/os/re):全部解析都在本地字节上完成;
#   - llvm-objdump:站 1 的段表展示与交叉验证用(subprocess 直调,不经 shell);
#     缺失或调用失败只降级——站 1 改用脚本自解析段表并打 WARN,不改变退出码;
#   - 与输入对象同目录的 core.bpf.c(源码对照素材):不在场只降级,站 5 跳过源码
#     对照并提示,不改变退出码;
#   - 本脚本只读输入文件与源码,不写任何文件、不加载任何内核对象、无网络。
#
# 【失败模式(退出码)】
#   0  五站全过,末尾 RELOS=<n>(n>0);
#   1  切片失败(打印 FAIL 行后退出),包括:
#        输入文件不存在/不可读;不是 64 位小端 ELF;
#        段表里没有 .BTF.ext(要用 clang -g -target bpf 编译,见 ../03-core-read);
#        有 .BTF.ext 却没有主 .BTF(access_str_off/sec_name_off 都相对主 .BTF
#        字符串区,缺主 .BTF 则记录不可解码);
#        .BTF/.BTF.ext 头非法(magic/版本/截断);.BTF.ext 头无 core_relo 字段;
#        core_relo 小节连 rec_size 都放不下或记录数=0;rec_size≠16;num_info=0;
#        kind 越界(本章断言口径 [0,11]);sec_name_off/access_str_off 越出字符串区;
#   2  用法错误(参数个数不对,打印用法后退出)。
#
# 【安全声明】
#   只读解析、无副作用;subprocess 仅调用 llvm-objdump -h,参数固定且
#   shell=False,输入路径只作为参数传递、不会被解释执行;所有越界/畸形输入
#   都以显式 FAIL 收场并给出人话解释,不静默吞错。

import os
import re
import struct
import subprocess
import sys

# ------------------------------- 常量表 -------------------------------

# BTF 家族统一魔数与版本(uapi btf.h :8-9;.BTF 与 .BTF.ext 同一个门牌)
BTF_MAGIC = 0xEB9F
BTF_VERSION = 1

# 站 4 的 kind 名称表(权威来源:走读树 include/uapi/linux/bpf.h :7262 起
# enum bpf_core_relo_kind;中文含义取自同一文件逐行注释)。
# 注:走读树同一文件还定义了 12 = BPF_CORE_TYPE_MATCHES(:7275,较新追加);
#     本章切片器断言口径为 [0,11],记录里若出现 12 会按越界 FAIL 并点名(见站 4)。
RELO_KINDS = [
    (0,  'BPF_CORE_FIELD_BYTE_OFFSET', '字段字节偏移(field byte offset)'),
    (1,  'BPF_CORE_FIELD_BYTE_SIZE',   '字段字节大小(field size in bytes)'),
    (2,  'BPF_CORE_FIELD_EXISTS',      '字段在目标内核是否存在(field existence)'),
    (3,  'BPF_CORE_FIELD_SIGNED',      '字段符号性(0 无符号/1 有符号)'),
    (4,  'BPF_CORE_FIELD_LSHIFT_U64',  '位域左移量(bitfield-specific left shift)'),
    (5,  'BPF_CORE_FIELD_RSHIFT_U64',  '位域右移量(bitfield-specific right shift)'),
    (6,  'BPF_CORE_TYPE_ID_LOCAL',     '本机 BTF 类型 ID(type id in local BPF object)'),
    (7,  'BPF_CORE_TYPE_ID_TARGET',    '目标内核 BTF 类型 ID(type id in target kernel)'),
    (8,  'BPF_CORE_TYPE_EXISTS',       '类型在目标内核是否存在(type existence)'),
    (9,  'BPF_CORE_TYPE_SIZE',         '类型大小(type size in bytes)'),
    (10, 'BPF_CORE_ENUMVAL_EXISTS',    '枚举值在目标内核是否存在(enum value existence)'),
    (11, 'BPF_CORE_ENUMVAL_VALUE',     '枚举值整数值(enum value integer value)'),
]
KIND_NAME = {k: n for k, n, _ in RELO_KINDS}

# BTF kind 名(权威:走读树 include/uapi/linux/btf.h :60 起的枚举)。
# 易记错点:6.6 树里 FUNC=12、FUNC_PROTO=13(顺序与一些旧资料相反),以 uapi 为准。
BTF_KINDS = ['UNKN', 'INT', 'PTR', 'ARRAY', 'STRUCT', 'UNION', 'ENUM', 'FWD',
             'TYPEDEF', 'VOLATILE', 'CONST', 'RESTRICT', 'FUNC', 'FUNC_PROTO',
             'VAR', 'DATASEC', 'FLOAT', 'DECL_TAG', 'TYPE_TAG', 'ENUM64']

# ELF 段类型(SHT_)里本脚本需要区分的值
SHT_NOBITS = 8


class Fail(Exception):
    """切片硬失败:带站号与中文原因,main 捕获后统一打 FAIL 行并 exit 1。"""

    def __init__(self, station, msg):
        super().__init__(msg)
        self.station = station
        self.msg = msg


def fail(station, msg):
    raise Fail(station, msg)


# ------------------------------- ELF 段表 -------------------------------

def cstr_in(data, base, size, off):
    """从 data[base+off] 起取 NUL 结尾字符串;越界/无 NUL 返回 None(调用方定夺)。"""
    if off >= size:
        return None
    start = base + off
    end = data.find(b'\0', start, base + size)
    if end < 0:
        return None
    return data[start:end].decode('utf-8', 'replace')


def parse_elf_sections(data):
    """解析 64 位小端 ELF 的段头表,返回 [(dict(name,offset,size,type,flags))](BPF 对象
    与 x86-64 普通目标文件同布局;只读段头,不碰程序头)。"""
    if data[:4] != b'\x7fELF':
        fail(1, '不是 ELF 文件(开头缺 \\x7fELF 魔数)')
    ei_class, ei_data = data[4], data[5]
    if ei_class != 2 or ei_data != 1:
        fail(1, f'仅支持 64 位小端 ELF(EI_CLASS={ei_class} EI_DATA={ei_data});'
                'BPF 对象与 x86-64 .o 都满足,其他架构请先交叉确认')
    e_machine = struct.unpack_from('<H', data, 18)[0]
    e_shoff = struct.unpack_from('<Q', data, 0x28)[0]
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', data, 0x3a)
    if e_shoff == 0 or e_shnum == 0:
        fail(1, 'ELF 没有段头表(不像可重定位目标文件)')
    if e_shstrndx >= e_shnum:
        fail(1, 'ELF 段名字表索引越界(文件截断?)')
    shs = []
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        if base + 64 > len(data):
            fail(1, f'ELF 段头 #{i} 越界(文件截断?)')
        (sh_name, sh_type, sh_flags, _addr, sh_offset, sh_size,
         _link, _info, _align, _entsize) = struct.unpack_from('<IIQQQQIIQQ', data, base)
        shs.append({'name': None, 'offset': sh_offset, 'size': sh_size,
                    'type': sh_type, 'flags': sh_flags, 'name_idx': sh_name})
    strtab = shs[e_shstrndx]
    for s in shs:
        s['name'] = cstr_in(data, strtab['offset'], strtab['size'], s['name_idx']) or ''
    return shs, e_machine


def objdump_h(path):
    """调 llvm-objdump -h 拿段表文本;任何失败(缺工具/打不开/超时)返回 None 并由调用方降级。"""
    try:
        p = subprocess.run(['llvm-objdump', '-h', path], capture_output=True,
                           encoding='utf-8', errors='replace', timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    if p.returncode != 0:
        return None
    return p.stdout


OBJDUMP_ROW = re.compile(r'^\s*(\d+)\s+(\S+)\s+([0-9a-fA-F]+)\s')


def objdump_sizes(text, wanted):
    """从 llvm-objdump -h 文本里摘出 wanted 各段的 (idx, name, size) 行;认不出的段略过。"""
    rows = {}
    for line in text.splitlines():
        m = OBJDUMP_ROW.match(line)
        if m and m.group(2) in wanted:
            rows[m.group(2)] = (m.group(1), int(m.group(3), 16), line.rstrip())
    return rows


# ------------------------------ 主 .BTF 视图 ------------------------------

def btf_kind_extra(kind, vlen):
    """每个 BTF kind 在 12 字节 btf_type 头之外附带的字节数(uapi btf.h「extra data」;
    6.6 树 FUNC=12、FUNC_PROTO=13,勿凭旧记忆写反)。"""
    if kind in (4, 5):
        return 12 * vlen    # STRUCT/UNION:btf_member(name,type,offset)×vlen
    if kind == 6:
        return 8 * vlen     # ENUM:btf_enum(name,val)×vlen
    if kind == 13:
        return 8 * vlen     # FUNC_PROTO:btf_param(name,type)×vlen
    if kind == 15:
        return 12 * vlen    # DATASEC:btf_var_secinfo(type,offset,size)×vlen
    if kind == 19:
        return 12 * vlen    # ENUM64:btf_enum64(name,val_lo,val_hi)×vlen
    return {1: 4, 3: 12, 14: 4, 17: 4}.get(kind, 0)  # INT/ARRAY/VAR/DECL_TAG 固定尾料,其余 0


class BtfView:
    """主 .BTF 段的只读视图:字符串区随机访问 + 类型区按 ID 线性走查
    (站 3 用字符串区解段名,站 5 用它解 access string 与成员名)。"""

    def __init__(self, blob):
        self.blob = blob
        if len(blob) < 24:
            fail(2, f'主 .BTF 段仅 {len(blob)} 字节,连头都放不下')
        magic, ver, _flags, hdr_len = struct.unpack_from('<HBBI', blob, 0)
        if magic != BTF_MAGIC:
            fail(2, f'主 .BTF 头 magic=0x{magic:04x} ≠ 0xEB9F(不是合法 BTF)')
        if ver != BTF_VERSION:
            fail(2, f'主 .BTF 头 version={ver} ≠ 1(本切片器只认 BTF_VERSION=1)')
        type_off, type_len, str_off, str_len = struct.unpack_from('<IIII', blob, 8)
        self.str_base = hdr_len + str_off
        self.str_len = str_len
        self.types_base = hdr_len + type_off
        if self.str_base + str_len > len(blob):
            fail(2, '主 .BTF 字符串区越界(段截断?)')
        if self.types_base + type_len > len(blob):
            fail(2, '主 .BTF 类型区越界(段截断?)')

    def cstr(self, off):
        """字符串区第 off 字节起的 NUL 结尾串;越界返回 None(调用方定夺是否 FAIL)。"""
        if off >= self.str_len:
            return None
        return cstr_in(self.blob, self.str_base, self.str_len, off)

    def type_at(self, tid):
        """按类型 ID 取 btf_type,返回 (off, name_off, kind, vlen, kflag, size_or_type);
        越界/走查失败返回 None。类型 ID 从 1 起连续编址,只能线性走到目标。"""
        if tid < 1:
            return None
        off = self.types_base
        for cur in range(1, tid + 1):
            if off + 12 > len(self.blob):
                return None
            name_off, info, u = struct.unpack_from('<III', self.blob, off)
            kind = (info >> 24) & 0x1f
            vlen = info & 0xffff
            if cur == tid:
                return (off, name_off, kind, vlen, info >> 31, u)
            off += 12 + btf_kind_extra(kind, vlen)
        return None

    def skip_mods(self, tid):
        """穿过 typedef/const/volatile/restrict 等修饰(同 libbpf skip_mods_and_typedefs 的简化版)。"""
        for _ in range(64):
            t = self.type_at(tid)
            if t is None:
                return tid
            _, _, kind, _, _, u = t
            if kind in (8, 9, 10, 11):   # TYPEDEF/VOLATILE/CONST/RESTRICT
                tid = u
            else:
                return tid
        return tid

    def resolve_size(self, tid, depth=0):
        """尽力求类型字节大小(结构体/整型取头内 size,指针 8,数组乘元素,修饰穿透);
        求不出返回 None——只影响展示,不作为断言。"""
        if depth > 16:
            return None
        t = self.type_at(tid)
        if t is None:
            return None
        off, _, kind, _vlen, _kflag, u = t
        if kind in (1, 4, 5, 6, 16, 19):  # INT/STRUCT/UNION/ENUM/FLOAT/ENUM64:size 在头里
            return u
        if kind == 2:                    # PTR
            return 8
        if kind == 3:                    # ARRAY:nelems × 元素大小(元素类型在尾料首字段)
            elem, _idx, nelems = struct.unpack_from('<III', self.blob, off + 12)
            esz = self.resolve_size(elem, depth + 1)
            return None if esz is None else nelems * esz
        if kind in (8, 9, 10, 11, 18):   # TYPEDEF/VOLATILE/CONST/RESTRICT/TYPE_TAG 穿透
            return self.resolve_size(u, depth + 1)
        return None


# ------------------------------ 站 1:ELF 段布局 ------------------------------

TRIO = ('.BTF', '.BTF.ext', '.rel.BTF.ext')


def station1(obj_path, ctx):
    print('## 站 1:ELF 段布局(.BTF/.BTF.ext/.rel.BTF.ext 尺寸)')
    if not os.path.isfile(obj_path):
        fail(1, f'输入文件不存在:{obj_path}')
    try:
        data = open(obj_path, 'rb').read()
    except OSError as e:
        fail(1, f'输入文件不可读:{obj_path}({e})')
    ctx['data'] = data
    shs, e_machine = parse_elf_sections(data)
    mach = {247: 'BPF', 62: 'x86-64'}.get(e_machine, f'机器号 {e_machine}')
    print(f'OK    {obj_path}:ELF64 小端,{mach},共 {len(shs)} 个段头,'
          f'文件 {len(data)} 字节')

    # llvm-objdump -h 是学生手里现成的段表工具:摘出三段原文做展示,
    # 并与脚本自解析(struct 逐字节)的尺寸互验——两个来源必须一致。
    dump = objdump_h(obj_path)
    dump_rows = None
    if dump is not None:
        dump_rows = objdump_sizes(dump, set(TRIO))
        if dump_rows:
            print('llvm-objdump -h 摘录(尺寸列为 16 进制,原文如下):')
            for name in TRIO:
                if name in dump_rows:
                    print('  ' + dump_rows[name][2])
        else:
            print('WARN  llvm-objdump -h 可用但摘不出本切片器关注的三段'
                  '(可能对象里本就没有,或输出格式未被识别),互验降级')
    else:
        print('WARN  llvm-objdump 不可用(未装或调用失败):站 1 降级为仅脚本自解析段表,不影响后续各站')

    sec_by_name = {s['name']: s for s in shs}
    ctx['sec_by_name'] = sec_by_name
    compared = 0
    print('脚本自解析段表(struct 逐字节):')
    for name in TRIO:
        s = sec_by_name.get(name)
        if s is None:
            continue
        extra = ''
        if name == '.BTF':
            extra = '——主 BTF(类型表+字符串表),站 3/5 要来这里取字符串'
        elif name == '.BTF.ext':
            extra = '——本次切片的主角:CO-RE 重定位元数据'
        elif name == '.rel.BTF.ext':
            extra = '——作用于 .BTF.ext 自身的 ELF 重定位,本切片器只量尺寸不解析'
        print(f'OK    {name:<13s} {s["size"]:7d} 字节(0x{s["size"]:x}){extra}')
        if dump_rows and name in dump_rows:
            if dump_rows[name][1] != s['size']:
                fail(1, f'段表两源不一致:{name} llvm-objdump 报 {dump_rows[name][1]} 字节,'
                        f'自解析得 {s["size"]} 字节')
            compared += 1
    if compared:
        print(f'PASS  交叉校验:llvm-objdump 与自解析的尺寸一致({compared} 个段两源都在场)')

    ext = sec_by_name.get('.BTF.ext')
    if ext is None:
        fail(1, '段表里没有 .BTF.ext——这不是带 BTF 的 BPF 对象。'
                '用 clang -g -target bpf 编译 .bpf.c 生成(完整链路见 ../03-core-read)')
    if ext['flags'] & 0x800:  # SHF_COMPRESSED:本切片器不解压缩
        fail(1, '.BTF.ext 段带 SHF_COMPRESSED 压缩标志,本切片器不解析压缩段')
    btf = sec_by_name.get('.BTF')
    if btf is None:
        fail(1, '有 .BTF.ext 却没有主 .BTF:记录里的 sec_name_off/access_str_off '
                '都以主 .BTF 字符串区为坐标系,缺主 .BTF 记录不可解码')
    ctx['ext_bytes'] = data[ext['offset']:ext['offset'] + ext['size']]
    ctx['btf_bytes'] = data[btf['offset']:btf['offset'] + btf['size']]
    print()


# --------------------------- 站 2:btf_ext 头 ---------------------------

def station2(ctx):
    print('## 站 2:btf_ext 头(magic 0xEB9F/长度/各段偏移)')
    ext = ctx['ext_bytes']
    if len(ext) < 8:
        fail(2, f'.BTF.ext 段仅 {len(ext)} 字节,连头都放不下')
    magic, version, flags, hdr_len = struct.unpack_from('<HBBI', ext, 0)
    if magic != BTF_MAGIC:
        fail(2, f'.BTF.ext 头 magic=0x{magic:04x} ≠ 0xEB9F'
                '(若看着像 0x9FEB,多半是按大端读了——BTF 家族按小端落盘)')
    if version != BTF_VERSION:
        fail(2, f'.BTF.ext 头 version={version} ≠ 1(本切片器只认 BTF_VERSION=1)')
    if flags != 0:
        fail(2, f'.BTF.ext 头 flags={flags} ≠ 0(libbpf 同样拒绝非零 flags,见走读树 btf.c :2808)')
    if hdr_len < 24:
        fail(2, f'.BTF.ext 头 hdr_len={hdr_len} < 24:连 func_info/line_info 偏移字段都不齐')
    print(f'OK    magic   = 0x{magic:04x}(BTF_MAGIC,BTF 家族统一门牌)')
    print(f'OK    version = {version}(BTF_VERSION=1)')
    print(f'OK    flags   = {flags}(libbpf 要求为 0)')
    print(f'OK    hdr_len = {hdr_len}(头的字节数;'
          '其后各段偏移一律相对头【末尾】——读 .BTF.ext 的关键)')

    fi_off, fi_len, li_off, li_len = struct.unpack_from('<IIII', ext, 8)
    print(f'OK    func_info_off={fi_off:4d} len={fi_len:4d}(函数信息;定位展示,本切片器不解析)')
    print(f'OK    line_info_off={li_off:4d} len={li_len:4d}(行号信息;同上)')
    if hdr_len < 32:
        fail(2, f'.BTF.ext 头 hdr_len={hdr_len} < 32:没有 core_relo 偏移字段——'
                '本对象没有 CO-RE 重定位小节(记录数=0);'
                '本章素材需经 BPF_CORE_READ/bpf_core_field_exists 等宏产生字段访问')
    cr_off, cr_len = struct.unpack_from('<II', ext, 24)
    print(f'OK    core_relo_off={cr_off:4d} len={cr_len:4d}(CO-RE 重定位小节——站 3 的原料)')

    # 自洽校验:头 + 三个信息段必须都落在 .BTF.ext 段内(btf.rst 4.2 节的布局)。
    for tag, off, ln in (('func_info', fi_off, fi_len),
                         ('line_info', li_off, li_len),
                         ('core_relo', cr_off, cr_len)):
        if hdr_len + off + ln > len(ext):
            fail(2, f'{tag} 小节(hdr_len+{off}+{ln}={hdr_len + off + ln})'
                    f'越出 .BTF.ext 段尺寸 {len(ext)}——段被截断?')
    max_end = max(hdr_len + fi_off + fi_len, hdr_len + li_off + li_len,
                  hdr_len + cr_off + cr_len)
    tail = len(ext) - max_end
    if tail == 0:
        print(f'PASS  自洽:头 + 三段首尾相接共 {max_end} 字节 = .BTF.ext 段尺寸(尾部余量 0;'
              '.BTF.ext 规范里没有独立字符串区,字符串在主 .BTF——见站 5 的辨析)')
    else:
        print(f'WARN  自洽:三段共到 {max_end} 字节,段尾余 {tail} 字节未识别'
              '(规范里 .BTF.ext 没有字符串区,余量请人工确认)')
    ctx['ext_hdr'] = {'hdr_len': hdr_len, 'cr_off': cr_off, 'cr_len': cr_len}

    # 顺带验主 .BTF 的头(站 3 取段名、站 5 解码字符串都踩它的坐标系)
    btf = BtfView(ctx['btf_bytes'])
    ctx['btf'] = btf
    bhdr_len = struct.unpack_from('<I', ctx['btf_bytes'], 4)[0]
    _t_off, _t_len, str_off, str_len = struct.unpack_from('<IIII', ctx['btf_bytes'], 8)
    print(f'OK    主 .BTF 头亦合法(其字符串区起点=hdr_len {bhdr_len} + str_off {str_off},'
          f'长度 {str_len} 字节;access_str_off 都相对这个起点)')
    print()


# --------------------- 站 3:core_relo 记录解析 ---------------------

def station3(ctx):
    print('## 站 3:core_relo 记录解析(insn_off/type_id/access_str_off/kind)')
    ext = ctx['ext_bytes']
    hdr = ctx['ext_hdr']
    cr_abs = hdr['hdr_len'] + hdr['cr_off']      # 小节绝对偏移 = 头长 + 头内相对偏移
    cr_len = hdr['cr_len']
    if cr_len < 4:
        fail(3, f'core_relo 小节仅 {cr_len} 字节:连 core_relo_rec_size 都放不下'
                '(本对象没有任何 CO-RE 重定位,记录数=0)')
    rec_size = struct.unpack_from('<I', ext, cr_abs)[0]
    if rec_size != 16:
        fail(3, f'core_relo_rec_size={rec_size} ≠ 16:uapi struct bpf_core_relo'
                '(bpf.h :7326)恰为四个 __u32;非 16 的记录布局本切片器不认识')
    print(f'OK    core_relo_rec_size = 16(uapi struct bpf_core_relo = '
          'insn_off/type_id/access_str_off/kind 四个 __u32)')

    btf = ctx['btf']
    records = []
    p = cr_abs + 4
    end = cr_abs + cr_len
    # 小节按 btf_ext_info_sec 分段(btf.rst 4.2:段名偏移 + 记录数 + 记录数组)
    while p < end:
        if p + 8 > end:
            fail(3, 'btf_ext_info_sec 头被截断')
        sec_name_off, num_info = struct.unpack_from('<II', ext, p)
        name = btf.cstr(sec_name_off)
        if name is None:
            fail(3, f'sec_name_off={sec_name_off} 越出主 .BTF 字符串区(段名不可解)')
        if num_info == 0:
            fail(3, f'小节 "{name}" num_info=0(btf.rst 规定 num_info 必须 >0)')
        need = 8 + num_info * rec_size
        if p + need > end:
            fail(3, f'小节 "{name}" 的记录数组越出 core_relo 小节(截断?)')
        print(f'OK    小节 "{name}"(sec_name_off={sec_name_off}):{num_info} 条记录')
        for r in range(num_info):
            insn_off, type_id, acc_off, kind = struct.unpack_from('<IIII', ext, p + 8 + r * rec_size)
            records.append({'sec_name': name, 'insn_off': insn_off, 'type_id': type_id,
                            'acc_off': acc_off, 'kind': kind})
        p += need
    if p != end:
        print(f'WARN  core_relo 小节尾剩 {end - p} 字节未对齐任何 btf_ext_info_sec(请人工确认)')

    total = len(records)
    if total == 0:
        fail(3, '记录数=0:core_relo 小节里没有任何重定位记录')
    print('逐条记录(uapi struct bpf_core_relo 四字段;insn_off 是 ELF API 的段内【字节】偏移,'
          '内核 API 才用指令条数为单位——BPF 指令一条 8 字节):')
    for i, rec in enumerate(records):
        kname = KIND_NAME.get(rec['kind'], f'kind {rec["kind"]}')
        print(f'      #{i} insn_off={rec["insn_off"]:4d}(0x{rec["insn_off"]:04x}) '
              f'type_id={rec["type_id"]:3d} access_str_off={rec["acc_off"]:5d} '
              f'kind={rec["kind"]}({kname})')
    print(f'PASS  断言:记录总数 = {total} > 0')
    ctx['records'] = records
    print()


# ------------------- 站 4:kind 对照 uapi 枚举 -------------------

def station4(ctx):
    print('## 站 4:kind 对照 uapi 枚举(0–11 名称表)')
    print('名称表(权威来源:走读树 include/uapi/linux/bpf.h :7262 起 '
          'enum bpf_core_relo_kind,中文含义取自同行注释):')
    for k, name, cn in RELO_KINDS:
        print(f'  {k:2d}  {name:<28s} {cn}')
    print(f'  (注:同一 uapi 文件在 11 之后还定义了 12 = BPF_CORE_TYPE_MATCHES——'
          '较新内核追加的类型匹配判定;本章切片器断言口径为 [0,11],'
          '记录里出现 12 会按越界 FAIL 并点名。)')

    hist = {}
    for i, rec in enumerate(ctx['records']):
        kind = rec['kind']
        if kind == 12:
            fail(4, f'记录 #{i} kind=12(BPF_CORE_TYPE_MATCHES):uapi 较新追加的枚举值,'
                    '超出本章切片器断言范围 [0,11]')
        if not 0 <= kind <= 11:
            fail(4, f'记录 #{i} kind={kind} 不在 uapi 枚举 0–11 内(记录损坏?)')
        hist[kind] = hist.get(kind, 0) + 1
        print(f'OK    记录 #{i} kind={kind:2d} = {KIND_NAME[kind]}')
    dist = '、'.join(f'{KIND_NAME[k]}×{c}' for k, c in sorted(hist.items()))
    print(f'PASS  断言:全部 kind ∈ [0,11];分布:{dist}')
    print()


# ------------- 站 5:access string 解码与源码字段对照 -------------

CO_RE_CALL = re.compile(r'(BPF_CORE_[A-Z_0-9]+|bpf_core_[a-z_]+)\s*\(')


def field_chain(btf, root_tid, raw):
    """按 libbpf bpf_core_parse_spec(relo_core.c :263)的语义走查字段访问链,返回
    (steps 描述, bit_offset 或 None, 是否含位域, 最深成员名或 None)。
    raw[0] 是根下标(根实体取 [idx],指针语义即解引用),raw[1:] 逐层是
    结构体成员序/数组下标。任何走不下去的层都降级返回(展示用途,不作断言)。"""
    steps = []
    bit_offset = 0
    has_bitfield = False
    last_member = None
    tid = root_tid

    idx0 = raw[0]
    size0 = btf.resolve_size(tid)
    if idx0 == 0:
        steps.append('根下标 0(根实体取 [0]:指针语义即解引用一次;位偏移 +0)')
    elif size0 is not None:
        bit = idx0 * size0 * 8
        bit_offset += bit
        steps.append(f'根下标 {idx0}(根实体取 [{idx0}];位偏移 +{bit})')
    else:
        bit_offset = None
        steps.append(f'根下标 {idx0}(根实体大小未知,偏移降级)')

    for depth, idx in enumerate(raw[1:], start=1):
        tid = btf.skip_mods(tid)
        t = btf.type_at(tid)
        if t is None:
            steps.append(f'第 {depth} 层:类型 #{tid} 走查失败(BTF 读取降级,仅显示索引 {idx})')
            return steps, None, has_bitfield, last_member
        off, name_off, kind, vlen, kflag, u = t
        if kind in (4, 5):            # STRUCT/UNION:成员序编址
            if idx >= vlen:
                steps.append(f'第 {depth} 层:成员下标 {idx} ≥ vlen {vlen}(越界,降级)')
                return steps, None, has_bitfield, last_member
            mn, mt, mo = struct.unpack_from('<III', btf.blob, off + 12 + 12 * idx)
            bitpos = mo & 0xffffff                      # BTF_MEMBER_BIT_OFFSET
            bfsize = (mo >> 24) if kflag else 0         # BTF_MEMBER_BITFIELD_SIZE
            if bfsize:
                has_bitfield = True
            if bit_offset is not None:
                bit_offset += bitpos
            tname = btf.cstr(name_off) or '(匿名)'
            mname = btf.cstr(mn) or '(匿名成员)'
            extra = f'、位域宽 {bfsize}' if bfsize else ''
            steps.append(f'第 {depth} 层:{BTF_KINDS[kind]} {tname} 成员 #{idx} = '
                         f'{mname}(位偏移 +{bitpos}{extra})')
            last_member = mname
            tid = mt
        elif kind == 3:               # ARRAY:下标编址
            elem, _idx_t, nelems = struct.unpack_from('<III', btf.blob, off + 12)
            esz = btf.resolve_size(elem)
            if esz is not None and idx >= nelems:
                steps.append(f'第 {depth} 层:数组下标 {idx} ≥ nelems {nelems}'
                             '(越界,或柔性数组——降级)')
                return steps, None, has_bitfield, last_member
            if esz is not None and bit_offset is not None:
                bit_offset += idx * esz * 8
            steps.append(f'第 {depth} 层:数组元素 [{idx}](元素类型 #{elem})')
            tid = elem
        else:
            steps.append(f'第 {depth} 层:种类 {BTF_KINDS[kind]} 不是结构体/数组,走查停止(降级)')
            return steps, None, has_bitfield, last_member
    return steps, bit_offset, has_bitfield, last_member


def enumval_name(btf, root_tid, idx):
    """枚举值类 kind:access string 是枚举器下标,返回 (名字, 值) 或 None。"""
    tid = btf.skip_mods(root_tid)
    t = btf.type_at(tid)
    if t is None:
        return None
    off, _name, kind, vlen, _kflag, _u = t
    if kind == 6 and idx < vlen:      # ENUM:btf_enum(name,val)
        n, val = struct.unpack_from('<Ii', btf.blob, off + 12 + 8 * idx)
        return btf.cstr(n), val
    if kind == 19 and idx < vlen:     # ENUM64:btf_enum64(name,val_lo,val_hi)
        n, lo, hi = struct.unpack_from('<III', btf.blob, off + 12 + 12 * idx)
        return btf.cstr(n), (hi << 32) | lo
    return None


def insn_imm(ctx, sec_name, insn_off):
    """取段内 insn_off 处那条 BPF 指令(8 字节)的立即数 imm(指令第 4-8 字节,有符号);
    取不到返回 None。"""
    s = ctx['sec_by_name'].get(sec_name)
    if s is None or s['type'] == SHT_NOBITS:
        return None
    if insn_off + 8 > s['size']:
        return None
    pos = s['offset'] + insn_off + 4
    if pos + 4 > len(ctx['data']):
        return None
    return struct.unpack_from('<i', ctx['data'], pos)[0]


def source_hits(src_lines, member):
    """在源码行里找「同时含成员名、又像 CO-RE 调用」的行(启发式):
    先按 '/*' 粗暴剥掉行尾块注释再匹配,避免结构体字段旁的解说注释干扰;
    整行注释(以 /* * // 开头)同样跳过。剥法不处理字符串字面量里的 '/*',
    对本章源码够用;漏配/多配都不是失败,只影响对照展示。"""
    hits = []
    for no, text in src_lines:
        s = text.strip()
        if s.startswith(('/*', '*', '//')):
            continue
        code = text.split('/*')[0]
        if member in code and CO_RE_CALL.search(code):
            hits.append((no, s))
    return hits


def station5(ctx):
    print('## 站 5:access string 解码与源码字段对照')
    btf = ctx['btf']
    print('重要辨析:access_str_off 相对【主 .BTF 的字符串区】起点——'
          '.BTF.ext 自身没有字符串区(btf.rst 4.2 节未定义任何字符串段,段尾即最后一条'
          '记录;libbpf relo_core.c :1308 亦用 btf__name_by_offset(local_btf, ...) '
          '取串)。本机实况:字符串区起点 = hdr_len 24 + str_off 18092 = 18116,'
          '长度 12486 字节,其他构建数值不同。')

    src_name = 'core.bpf.c'
    src_path = os.path.join(os.path.dirname(os.path.abspath(ctx['obj_path'])), src_name)
    src_lines = None
    if os.path.isfile(src_path):
        with open(src_path, 'r', encoding='utf-8', errors='replace') as f:
            src_lines = [(i + 1, line) for i, line in enumerate(f)]
        print(f'OK    源码对照素材:{src_path}({len(src_lines)} 行)')
    else:
        print(f'WARN  源码对照素材不在场({src_path}):本站跳过源码对照(降级,不影响退出码)')

    print('逐条解码(访问链语义同 libbpf:首段=根下标,其余=成员序/数组下标):')
    for i, rec in enumerate(ctx['records']):
        acc = btf.cstr(rec['acc_off'])
        if acc is None:
            fail(5, f'记录 #{i} access_str_off={rec["acc_off"]} 越出主 .BTF 字符串区'
                    '(或串未以 NUL 结尾)——记录损坏?')
        print(f'  #{i} access_str_off={rec["acc_off"]} → "{acc}"')
        try:
            raw = [int(x) for x in acc.split(':')]
        except ValueError:
            print('      WARN  访问串不是纯数字下标链(降级,仅展示原文)')
            continue
        if not raw or raw[0] < 0:
            print('      WARN  访问串为空或首段为负(异常,降级)')
            continue
        kind = rec['kind']
        member = None
        if 6 <= kind <= 9:            # 类型类:访问串应为 "0"
            if raw != [0]:
                print(f'      WARN  类型类 kind 的访问串应为 "0",实测 "{acc}"(异常)')
            t = btf.type_at(btf.skip_mods(rec['type_id']))
            tname = btf.cstr(t[1]) if t else None
            root_desc = tname if tname else f"#{rec['type_id']}"
            print(f'      解码:根类型 {root_desc}(类型类重定位不带字段链)')
        elif kind in (10, 11):        # 枚举值类:下标进枚举器
            ev = enumval_name(btf, rec['type_id'], raw[0])
            if ev:
                print(f'      解码:根下标 {raw[0]} = 枚举器 "{ev[0]}"(值 {ev[1]})')
                member = ev[0]
            else:
                print(f'      解码:枚举器下标 {raw[0]}(根类型走查失败,降级)')
        else:                         # 字段类(0-5):根下标 + 成员/数组链
            steps, bit_offset, has_bf, member = field_chain(btf, rec['type_id'], raw)
            for st in steps:
                print(f'      {st}')
            if bit_offset is not None:
                if bit_offset % 8 == 0:
                    print(f'      汇:本机位偏移 {bit_offset} = 字节 {bit_offset // 8}'
                          '(本机实况,其他内核数值不同——正是加载期 CO-RE 引擎要按目标内核改写的量)')
                else:
                    print(f'      汇:本机位偏移 {bit_offset}(非整字节——位域字段)')
            # 立即数对照:kind=0 时 LLVM 把本机字节偏移直接编进指令立即数
            if kind == 0 and bit_offset is not None and bit_offset % 8 == 0 and not has_bf:
                imm = insn_imm(ctx, rec['sec_name'], rec['insn_off'])
                if imm is None:
                    print('      WARN  立即数取不到(程序段缺失或 insn_off 越界),对照跳过')
                elif imm == bit_offset // 8:
                    print(f'      指令立即数:insn_off={rec["insn_off"]} 处 imm={imm}'
                          f'(0x{imm & 0xffffffff:x})= 本机字节偏移,PASS 一致')
                else:
                    print(f'      WARN  指令立即数 imm={imm} ≠ 本机字节偏移 {bit_offset // 8},请人工判读')
            elif kind == 2:
                imm = insn_imm(ctx, rec['sec_name'], rec['insn_off'])
                if imm is not None:
                    print(f'      指令立即数:insn_off={rec["insn_off"]} 处 imm={imm}'
                          '(存在性判定的占位立即数,加载期由 CO-RE 引擎按目标内核改写成 1/0)')
        # 源码字段对照:拿最深成员名回源码里找 CO-RE 调用行
        if src_lines is None:
            continue
        if member is None:
            print('      WARN  成员名未解析,源码对照跳过')
            continue
        hits = source_hits(src_lines, member)
        if hits:
            for no, text in hits:
                show = text if len(text) <= 80 else text[:77] + '...'
                print(f'      源码对照:{src_name}:{no}  {show}')
        else:
            print(f'      WARN  源码 {src_name} 里未见含 "{member}" 的 CO-RE 调用行'
                  '(可能是宏展开或别名访问,请人工判读)')
    print()


# -------------------------------- 汇总 --------------------------------

def summary(ctx):
    total = len(ctx['records'])
    hist = {}
    for rec in ctx['records']:
        hist[rec['kind']] = hist.get(rec['kind'], 0) + 1
    dist = '、'.join(f'{KIND_NAME[k]}×{c}' for k, c in sorted(hist.items()))
    print('## 汇总')
    print(f'RELOS={total}')
    print(f'kind 分布:{dist}')
    print(f'断言:记录数 > 0(实测 {total})PASS;全部 kind ∈ [0,11] PASS')
    print('结论:切片成功——.BTF.ext 五站走完;逐条记录的「源码一行 CO-RE 访问 ↔ '
          '.BTF.ext 一条记录」对照见站 5(数值均为本机实况,其他内核不同)。')


USAGE = ('用法:python3 slice-relos.py <path/to/.bpf.o>\n'
         '示例:python3 slice-relos.py ../03-core-read/core.bpf.o')


def main(argv):
    if len(argv) != 2:
        print(USAGE)
        return 2
    ctx = {'obj_path': argv[1]}
    try:
        station1(ctx['obj_path'], ctx)
        station2(ctx)
        station3(ctx)
        station4(ctx)
        station5(ctx)
        summary(ctx)
    except Fail as f:
        print(f'FAIL  站 {f.station}:{f.msg}')
        print('结论:切片失败(exit 1)——按上方 FAIL 行修复输入或环境后重试。')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
