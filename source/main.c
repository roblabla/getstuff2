#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <switch.h>
#include <twili.h>



#define TITLE_ID 0x420000000000000E
#define HEAP_SIZE 0x000540000

// we aren't an applet
u32 __nx_applet_type = AppletType_None;

// setup a fake heap
char fake_heap[HEAP_SIZE];

// we override libnx internals to do a minimal init
void __libnx_initheap(void)
{
    extern char *fake_heap_start;
    extern char *fake_heap_end;

    // setup newlib fake heap
    fake_heap_start = fake_heap;
    fake_heap_end = fake_heap + HEAP_SIZE;
}

void __appInit(void)
{
    Result rc;
    svcSleepThread(10000000000L);
    rc = smInitialize();
    if (R_FAILED(rc))
        *(u32*)0 = 0xCAFEBABE;
    rc = setsysInitialize();
    if (R_SUCCEEDED(rc))
    {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }
}

void __appExit(void)
{
    smExit();
}


#define TRY(rc) do { \
    if (rc) { \
        printf("Error %x\n", rc); \
        return 1; \
    } \
} while (0)

Result pmdmntAtmosphereGetProcessInfo(Handle *out_process, u64 *out_title_id, FsStorageId *out_storage_id, u64 pid) {
    IpcCommand c;
    ipcInitialize(&c);

    struct {
        u64 magic;
        u64 cmd_id;
        u64 pid;
    } *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 65000;

    Service *pmdmnt = pmdmntGetServiceSession();
    Result rc = serviceIpcDispatch(pmdmnt);

    if (R_SUCCEEDED(rc)) {
        IpcParsedCommand r;
        ipcParse(&r);

        struct {
            u64 magic;
            u64 result;
            u64 title_id;
            FsStorageId storage_id;
        } *resp = r.Raw;

        rc = resp->result;

        if (R_SUCCEEDED(rc)) {
            if (out_title_id) *out_title_id = resp->title_id;
            if (out_storage_id) *out_storage_id = resp->storage_id;
            *out_process = r.Handles[0];
        }
    }

    return rc;
}

int main(int argc, char **argv)
{
    twiliInitialize();

    size_t addr = 0;
    MemoryInfo info;
    u32 pageinfo;
    u64 alias_region_addr;
    u64 alias_region_size;
    u64 heap_region_addr;
    u64 heap_region_size;
    u64 addr_space_addr;
    u64 addr_space_size;
    u64 stack_region_addr;
    u64 stack_region_size;
    u32 rc;

    pmdmntInitialize();

    // Get MK8D process
    u64 mk8d_pid;
    Handle mk8d_handle;

    while ((rc = pmdmntGetTitlePid(&mk8d_pid, 0x0100152000022000)) != 0) {
        svcSleepThread(1 * 1000 * 1000 * 1000);
    }
    TRY(pmdmntAtmosphereGetProcessInfo(&mk8d_handle, NULL, NULL, mk8d_pid));

    pmdmntExit();

    TRY(svcGetInfo(&alias_region_addr, 2, mk8d_handle, 0));
    TRY(svcGetInfo(&alias_region_size, 3, mk8d_handle, 0));
    TRY(svcGetInfo(&heap_region_addr, 4, mk8d_handle, 0));
    TRY(svcGetInfo(&heap_region_size, 5, mk8d_handle, 0));
    TRY(svcGetInfo(&addr_space_addr, 12, mk8d_handle, 0));
    TRY(svcGetInfo(&addr_space_size, 13, mk8d_handle, 0));
    TRY(svcGetInfo(&stack_region_addr, 14, mk8d_handle, 0));
    TRY(svcGetInfo(&stack_region_size, 15, mk8d_handle, 0));
    printf("INFOS:\n");
    printf("alias_region = %016" PRIx64 "-%016" PRIx64 "\n", alias_region_addr, alias_region_addr + alias_region_size);
    printf("heap_region = %016" PRIx64 "-%016" PRIx64 "\n", heap_region_addr, heap_region_addr + heap_region_size);
    printf("stack_region = %016" PRIx64 "-%016" PRIx64 "\n", stack_region_addr, stack_region_addr + stack_region_size);
    printf("addr_space = %016" PRIx64 "-%016" PRIx64 "\n", addr_space_addr, addr_space_addr + addr_space_size);
    printf("\nMEMORY LAYOUT\n");
    do {
        rc = svcQueryProcessMemory(&info, &pageinfo, mk8d_handle, addr);
        if (rc) {
            printf("Error: %" PRIx32 "\n", rc);
            return 1;
        }
        printf("%016" PRIx64 "-%016" PRIx64 ": %" PRIx32 " %" PRIx32 " %" PRIo32 " %" PRId32 " %" PRId32 "\n", info.addr, info.addr + info.size, info.type, info.attr, info.perm, info.device_refcount, info.ipc_refcount);
        addr = info.addr + info.size;
    } while(addr != 0);

    twiliExit();
    return 0;
}

