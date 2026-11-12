/* installer.inc - wcteos 安装程序（第一批：磁盘扫描 / 容量门槛 / 许可 / 磁盘列表 / FAT32 格式化）
 * 放在 oobe.inc 之后 include，复用其绘图与按键辅助函数。 */

typedef struct {
    UINT32 MediaId;
    UINT8  RemovableMedia;
    UINT8  MediaPresent;
    UINT8  LogicalPartition;
    UINT8  ReadOnly;
    UINT8  WriteCaching;
    UINT32 BlockSize;
    UINT32 IoAlign;
    UINT64 LastBlock;
} INS_MEDIA;

typedef struct {
    UINT64 Revision;
    INS_MEDIA *Media;
    EFI_STATUS (EFIAPI *Reset)(void *);
    EFI_STATUS (EFIAPI *ReadBlocks)(void *, UINT32, UINT64, UINTN, void *);
    EFI_STATUS (EFIAPI *WriteBlocks)(void *, UINT32, UINT64, UINTN, void *);
    EFI_STATUS (EFIAPI *FlushBlocks)(void *);
} INS_BIO;

static EFI_GUID ins_bio_guid = { 0x964e5b21, 0x6459, 0x11d2,
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };

#define INS_MAX_DISKS 8

typedef struct {
    EFI_HANDLE h;
    INS_BIO *bio;
    UINT64 bytes;
    int removable;
} INS_DISK;

static INS_DISK ins_disk;
static int ins_disk_count;
static int ins_debug;
static void ins_outnum(UINT64 v);

static UINT16 ins_rd16(const UINT8 *p) { return (UINT16)(p[0] | (p[1] << 8)); }
static UINT32 ins_rd32(const UINT8 *p)
{
    return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}
static void ins_wr16(UINT8 *p, UINT16 v) { p[0] = (UINT8)v; p[1] = (UINT8)(v >> 8); }
static void ins_wr32(UINT8 *p, UINT32 v)
{
    p[0] = (UINT8)v; p[1] = (UINT8)(v >> 8); p[2] = (UINT8)(v >> 16); p[3] = (UINT8)(v >> 24);
}

/* 枚举整盘（LogicalPartition=0），排除可移动介质（U 盘） */

/* 判断某块磁盘是不是当前启动介质：用 LocateDevicePath 反查启动卷所属磁盘句柄 */
typedef EFI_STATUS (EFIAPI *INS_LOCATE_DP)(EFI_GUID *, void **, EFI_HANDLE *);

static int ins_is_boot_disk(EFI_HANDLE h)
{
    if (!g_loaded || !g_loaded->DeviceHandle) return 0;
    void *dp = NULL;
    if (EFI_IS_ERROR(ST->BootServices->HandleProtocol(g_loaded->DeviceHandle, &device_path_guid, &dp)) || !dp)
        return 0;
    INS_LOCATE_DP locate = (INS_LOCATE_DP)ST->BootServices->LocateDevicePath;
    EFI_HANDLE diskh = NULL;
    void *rem = dp;
    if (EFI_IS_ERROR(locate(&device_path_guid, &rem, &diskh)) || !diskh) return 0;
    return diskh == h;
}

static int ins_scan(void)
{
    EFI_HANDLE *buf = NULL;
    UINTN n = 0;
    ins_disk_count = 0;
    int chosen = 0;
    if (EFI_IS_ERROR(ST->BootServices->LocateHandleBuffer(2, &ins_bio_guid, NULL, &n, &buf)) || !buf)
        return 0;
    for (UINTN i = 0; i < n && ins_disk_count < INS_MAX_DISKS; ++i) {
        INS_BIO *bio = NULL;
        if (EFI_IS_ERROR(ST->BootServices->HandleProtocol(buf[i], &ins_bio_guid, (void **)&bio)) || !bio)
            continue;
        if (!bio->Media || !bio->Media->MediaPresent) continue;
        if (bio->Media->LogicalPartition) continue;
        if (bio->Media->BlockSize == 0) continue;
        INS_DISK d;
        d.h = buf[i];
        d.bio = bio;
        d.removable = bio->Media->RemovableMedia ? 1 : 0;
        d.bytes = (bio->Media->LastBlock + 1) * (UINT64)bio->Media->BlockSize;
        if (!d.removable && d.bytes >= 400ULL * 1024 * 1024 && !ins_is_boot_disk(d.h) && !chosen) {
            ins_disk = d;
            chosen = 1;
        }
        ++ins_disk_count;
    }
    ST->BootServices->FreePool(buf);
    return ins_disk_count;
}

/* 选中的安装目标：第一块非移动介质磁盘 */
static INS_DISK *ins_target(void)
{
    if (ins_disk_count <= 0) return NULL;
    if (ins_disk.removable) return NULL;
    return &ins_disk;
}

static int ins_sector_write(INS_BIO *bio, UINT64 lba, void *buf)
{
    return !EFI_IS_ERROR(bio->WriteBlocks(bio, bio->Media->MediaId, lba, bio->Media->BlockSize, buf));
}
static int ins_sector_read(INS_BIO *bio, UINT64 lba, void *buf)
{
    return !EFI_IS_ERROR(bio->ReadBlocks(bio, bio->Media->MediaId, lba, bio->Media->BlockSize, buf));
}

/* 计算 FAT32 参数并写盘；返回 0 成功，负数为失败点 */
static int ins_format_fat32(INS_BIO *bio, UINT64 start_lba, UINT64 part_sectors, char *msg, int msglen)
{
    UINT8 sec[512];
    UINT32 spc = 8, fatsz = 0, clusters = 0;
    while (spc > 1 && part_sectors / spc < 70000) spc >>= 1;
    fatsz = (UINT32)(((part_sectors / spc + 2) * 4 + 511) / 512 + 2);
    for (int it = 0; it < 8; ++it) {
        clusters = (UINT32)((part_sectors - 32 - (UINT64)2 * fatsz) / spc);
        UINT32 need = (UINT32)(((UINT64)(clusters + 2) * 4 + 511) / 512 + 2);
        if (need <= fatsz) break;
        fatsz = need;
    }
    if (clusters < 65525) {
        msg[0] = 's'; msg[1] = 'm'; msg[2] = 'a'; msg[3] = 'l'; msg[4] = 'l'; msg[5] = 0;
        return -1;
    }

    /* 清 MBR，写一个 0x0C FAT32 LBA 分区 */
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    sec[446 + 0] = 0x00;                   /* 非活动 */
    sec[446 + 1] = 0x00; sec[446 + 2] = 0x02; sec[446 + 3] = 0x00;   /* CHS 起始占位 */
    sec[446 + 4] = 0x0C;                   /* FAT32 LBA */
    sec[446 + 5] = 0xFE; sec[446 + 6] = 0xFF; sec[446 + 7] = 0xFF;
    ins_wr32(sec + 446 + 8, (UINT32)start_lba);
    ins_wr32(sec + 446 + 12, (UINT32)part_sectors);
    sec[510] = 0x55; sec[511] = 0xAA;
    if (!ins_sector_write(bio, 0, sec)) { msg[0]='m';msg[1]='b';msg[2]='r';msg[3]=0; return -2; }

    /* 引导扇区 */
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    sec[0] = 0xEB; sec[1] = 0x58; sec[2] = 0x90;
    const char *oem = "MSWIN4.1";
    for (int i = 0; i < 8; ++i) sec[3 + i] = (UINT8)oem[i];
    ins_wr16(sec + 11, 512);
    sec[13] = (UINT8)spc;
    ins_wr16(sec + 14, 32);
    sec[16] = 2;
    ins_wr16(sec + 17, 0);
    ins_wr16(sec + 19, 0);
    sec[21] = 0xF8;
    ins_wr16(sec + 22, 0);
    ins_wr16(sec + 24, 63);
    ins_wr16(sec + 26, 255);
    ins_wr32(sec + 28, (UINT32)start_lba);
    ins_wr32(sec + 32, (UINT32)part_sectors);
    ins_wr32(sec + 36, fatsz);
    ins_wr16(sec + 40, 0);
    ins_wr16(sec + 42, 0);
    ins_wr32(sec + 44, 2);
    ins_wr16(sec + 46, 1);
    ins_wr16(sec + 48, 6);
    sec[64] = 0x80;
    sec[66] = 0x29;
    ins_wr32(sec + 67, 0x57435430);
    const char *label = "WCTEOS     ";
    for (int i = 0; i < 11; ++i) sec[71 + i] = (UINT8)label[i];
    const char *fs = "FAT32   ";
    for (int i = 0; i < 8; ++i) sec[82 + i] = (UINT8)fs[i];
    sec[510] = 0x55; sec[511] = 0xAA;
    if (!ins_sector_write(bio, start_lba, sec)) { msg[0]='b';msg[1]='p';msg[2]='b';msg[3]=0; return -3; }

    /* FSInfo */
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    ins_wr32(sec + 0, 0x41615252);
    ins_wr32(sec + 484, 0x61417272);
    ins_wr32(sec + 488, clusters - 1);
    ins_wr32(sec + 492, 3);
    ins_wr16(sec + 510, 0xAA55);
    if (!ins_sector_write(bio, start_lba + 1, sec)) { msg[0]='f';msg[1]='s';msg[2]='i';msg[3]=0; return -4; }

    /* 备份引导扇区：先写 6 号，再复制 1 号 FSInfo 的简化版 */
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    ins_wr32(sec + 0, 0x41615252);
    ins_wr32(sec + 484, 0x61417272);
    ins_wr32(sec + 488, clusters - 1);
    ins_wr32(sec + 492, 3);
    ins_wr16(sec + 510, 0xAA55);
    ins_sector_write(bio, start_lba + 7, sec);
    /* 备份引导扇区内容与主引导扇区相同，重写一遍 */
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    sec[0] = 0xEB; sec[1] = 0x58; sec[2] = 0x90;
    for (int i = 0; i < 8; ++i) sec[3 + i] = (UINT8)oem[i];
    ins_wr16(sec + 11, 512);
    sec[13] = (UINT8)spc;
    ins_wr16(sec + 14, 32);
    sec[16] = 2;
    ins_wr16(sec + 17, 0);
    ins_wr16(sec + 19, 0);
    sec[21] = 0xF8;
    ins_wr16(sec + 22, 0);
    ins_wr16(sec + 24, 63);
    ins_wr16(sec + 26, 255);
    ins_wr32(sec + 28, (UINT32)start_lba);
    ins_wr32(sec + 32, (UINT32)part_sectors);
    ins_wr32(sec + 36, fatsz);
    ins_wr16(sec + 40, 0);
    ins_wr16(sec + 42, 0);
    ins_wr32(sec + 44, 2);
    ins_wr16(sec + 46, 1);
    ins_wr16(sec + 48, 6);
    sec[64] = 0x80;
    sec[66] = 0x29;
    ins_wr32(sec + 67, 0x57435430);
    for (int i = 0; i < 11; ++i) sec[71 + i] = (UINT8)label[i];
    for (int i = 0; i < 8; ++i) sec[82 + i] = (UINT8)fs[i];
    sec[510] = 0x55; sec[511] = 0xAA;
    ins_sector_write(bio, start_lba + 6, sec);

    /* FAT 表：两个副本，首扇区写保留项，其余清零 */
    for (int f = 0; f < 2; ++f) {
        UINT64 fat = start_lba + 32 + (UINT64)f * fatsz;
        for (UINT32 s = 0; s < fatsz; ++s) {
            for (int i = 0; i < 512; ++i) sec[i] = 0;
            if (s == 0) {
                ins_wr32(sec + 0, 0x0FFFFFF8);
                ins_wr32(sec + 4, 0x0FFFFFFF);
                ins_wr32(sec + 8, 0x0FFFFFFF);   /* 根目录簇 2 结束 */
            }
            if (!ins_sector_write(bio, fat + s, sec)) { msg[0]='f';msg[1]='a';msg[2]='t';msg[3]=0; return -5; }
        }
    }

    /* 根目录簇清零 */
    UINT64 root = start_lba + 32 + (UINT64)2 * fatsz;
    for (UINT32 s = 0; s < spc; ++s) {
        for (int i = 0; i < 512; ++i) sec[i] = 0;
        if (!ins_sector_write(bio, root + s, sec)) { msg[0]='r';msg[1]='o';msg[2]='o';msg[3]=0; return -6; }
    }
    bio->FlushBlocks(bio);

    /* 回读校验 */
    UINT8 chk[512];
    if (!ins_sector_read(bio, start_lba, chk) || ins_rd16(chk + 11) != 512 || ins_rd32(chk + 36) != fatsz) {
        msg[0]='v';msg[1]='e';msg[2]='r';msg[3]=0; return -7;
    }
    msg[0] = 'o'; msg[1] = 'k'; msg[2] = 0;
    return 0;
}


typedef EFI_STATUS (EFIAPI *INS_CONNECT)(EFI_HANDLE, EFI_HANDLE *, void *, void *, int);

/* 格式化后让固件重新挂载目标分区，并把启动文件复制过去 */
static EFI_FILE_PROTOCOL *ins_open_target_root(INS_DISK *d)
{
    typedef EFI_STATUS (EFIAPI *INS_DISCONNECT)(EFI_HANDLE, EFI_HANDLE, EFI_HANDLE);
    INS_DISCONNECT dc = (INS_DISCONNECT)ST->BootServices->DisconnectController;
    dc(d->h, NULL, NULL);                 /* 清掉格式化前的旧子句柄 */
    INS_CONNECT cc = (INS_CONNECT)ST->BootServices->ConnectController;
    EFI_STATUS cst = cc(d->h, NULL, NULL, NULL, 1);
    out(L"[inst] connect st="); ins_outnum((UINT64)(unsigned)cst); outln(L"");

    /* 分区是磁盘的子句柄：枚举所有 SimpleFS，取容量最大且 >=400MB 的卷 */
    EFI_HANDLE *buf = NULL; UINTN n = 0;
    if (EFI_IS_ERROR(ST->BootServices->LocateHandleBuffer(2, &simple_fs_guid, NULL, &n, &buf)) || !buf)
        return NULL;
    EFI_FILE_PROTOCOL *best = NULL;
    UINT64 bestsize = 0;
    for (UINTN i = 0; i < n; ++i) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
        if (EFI_IS_ERROR(ST->BootServices->HandleProtocol(buf[i], &simple_fs_guid, (void **)&fs)) || !fs) continue;
        EFI_FILE_PROTOCOL *root = NULL;
        if (EFI_IS_ERROR(fs->OpenVolume(fs, &root)) || !root) continue;
        static UINT8 ib[1024]; UINTN isz = sizeof(ib);
        EFI_FILE_SYSTEM_INFO *info = (EFI_FILE_SYSTEM_INFO *)ib;
        if (EFI_IS_ERROR(root->GetInfo(root, &fs_info_guid, &isz, ib))) { root->Close(root); continue; }
        if (info->VolumeSize >= 400ULL * 1024 * 1024 && info->VolumeSize > bestsize) {
            if (best) best->Close(best);
            best = root;
            bestsize = info->VolumeSize;
        } else {
            root->Close(root);
        }
    }
    ST->BootServices->FreePool(buf);
    out(L"[inst] target volume bytes="); ins_outnum(bestsize); outln(L"");
    return best;
}

static int ins_mkdir(EFI_FILE_PROTOCOL *root, const CHAR16 *name)
{
    EFI_FILE_PROTOCOL *dir = NULL;
    EFI_STATUS st = root->Open(root, &dir, (CHAR16 *)name, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0x10);
    if (EFI_IS_ERROR(st) || !dir) return 0;
    dir->Close(dir);
    return 1;
}

static int ins_copy_one(EFI_FILE_PROTOCOL *src, EFI_FILE_PROTOCOL *dst, const CHAR16 *spath, const CHAR16 *dpath)
{
    EFI_FILE_PROTOCOL *f = NULL, *g = NULL;
    if (!open_path(spath, &f, EFI_FILE_MODE_READ, 0)) return 0;
    EFI_STATUS st = dst->Open(dst, &g, (CHAR16 *)dpath, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_IS_ERROR(st) || !g) { f->Close(f); return 0; }
    static UINT8 buf[64 * 1024];
    int ok = 1;
    for (;;) {
        UINTN n = sizeof(buf);
        if (EFI_IS_ERROR(f->Read(f, &n, buf)) || n == 0) break;
        UINTN w = n;
        if (EFI_IS_ERROR(g->Write(g, &w, buf)) || w != n) { ok = 0; break; }
    }
    g->Close(g);
    f->Close(f);
    return ok;
}

/* 返回复制的文件数，0 表示失败 */
static int ins_copy_system(INS_DISK *d)
{
    EFI_FILE_PROTOCOL *tr = ins_open_target_root(d);
    if (!tr) return 0;
    int n = 0;
    if (!ins_mkdir(tr, L"\\EFI")) { tr->Close(tr); return 0; }
    ins_mkdir(tr, L"\\EFI\\BOOT");
    ins_mkdir(tr, L"\\wct");
    if (ins_copy_one(ROOT, tr, L"\\EFI\\BOOT\\BOOTX64.EFI", L"\\EFI\\BOOT\\BOOTX64.EFI")) ++n;
    tr->Close(tr);
    return n;
}

/* 调试命令：格式化并复制启动文件（只用于验证，不接 UI） */
static void ins_outnum(UINT64 v)
{
    CHAR16 b[24];
    int n = 0;
    if (!v) { out(L"0"); return; }
    while (v && n < 23) { b[n++] = (CHAR16)('0' + (int)(v % 10)); v /= 10; }
    CHAR16 t[24];
    for (int i = 0; i < n; ++i) t[i] = b[n - 1 - i];
    t[n] = 0;
    out(t);
}

static void cmd_instfmt(void)
{
    ins_debug = 1;
    int n = ins_scan();
    outln(L"installer: scan disks");
    for (int i = 0; i < n && i < INS_MAX_DISKS; ++i) {
        out(L"  disk ");
        ins_outnum(i);
        out(L"  bytes=");
        ins_outnum((ins_disk.bio->Media->LastBlock + 1) * (UINT64)ins_disk.bio->Media->BlockSize);
        out(L"  removable=");
        ins_outnum((UINT64)ins_disk.removable);
        outln(L"");
    }
    INS_DISK *t = ins_target();
    if (!t) { outln(L"no installable disk"); return; }
    UINT64 total = t->bio->Media->LastBlock + 1;
    if (total * t->bio->Media->BlockSize < 400ULL * 1024 * 1024) { outln(L"disk too small"); return; }
    char msg[8];
    int rc = ins_format_fat32(t->bio, 2048, total - 4096, msg, sizeof(msg));
    out(L"format rc="); ins_outnum((UINT64)(unsigned)rc); out(L" msg=");
    outln_cstr(msg);
    if (rc == 0) {
        int files = ins_copy_system(t);
        out(L"copy files="); ins_outnum((UINT64)(unsigned)files); outln(L"");
    }
}
