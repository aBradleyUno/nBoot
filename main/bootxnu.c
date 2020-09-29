//
// Created by Alexander Bradley on 2019-03-30.
//


#include <malloc.h>

#include <arm64/boot.h>
#include <mach-o/macho.h>
#include <asm/system.h>
#include <stdio.h>
#include <command.h>
#include <init.h>
#include <cpu_func.h>

static void call_kernel(uint64_t entry_point, uint64_t boot_args_ptr)
{
    dcache_disable();
    printf("Booting XNU at %#08llX\n\n", entry_point);
    armv8_switch_to_el1(boot_args_ptr, 0, 0, 0, (unsigned long)entry_point, ES_TO_AARCH64);
}


static void macho_setup_bootargs(boot_args* boot_args, uint64_t virt_base, uint64_t phys_base, uint64_t top_of_kernel_data, uint64_t dtb_address, uint32_t dtb_size) {
    boot_args->Revision = kBootArgsRevision;
    boot_args->Version = kBootArgsVersion2;
    boot_args->virtBase = virt_base;
    boot_args->physBase = phys_base;
    boot_args->memSize = 0x80000000;
    // top of kernel data (kernel, dtb, any ramdisk) + boot args size + padding to 16k
    boot_args->topOfKernelData = ((top_of_kernel_data + sizeof(boot_args)) + 0xffffull) & ~0xffffull;
    // todo: video, machine type, flags
    boot_args->deviceTreeP = dtb_address;
    boot_args->deviceTreeLength = dtb_size;
    strcpy(boot_args->CommandLine, env_get("bootargs"));
    memset(&boot_args->Video, 0, sizeof(Boot_Video));
    // this is badly named: it's actually
    // how much physical RAM is *not* available to the kernel
    // if left at 0, kernel estimates it by taking difference between
    // memSize and size rounded up to 512m
    // it's only used for memory usage debugging though.s
    boot_args->memSizeActual = 0;
    printf("Boot Args:\n"
           "\tRevision:\t\t%u\n"
           "\tVersion:\t\t%u\n"
           "\tVirtual Base:\t\t%#llX\n"
           "\tPhysical Base:\t\t%#llX\n"
           "\tMemory Size:\t\t%#X\n"
           "\tTop of Kernel Data:\t%#X\n"
           "\tDevice Tree:\t\t%#X\n"
           "\tDevice Tree Size:\t%u\n"
           "\tCommand Line:\t\t%s\n"
           "\tVideo:\t\t\tNot Set\n"
           "\tMemory Size Actual:\t%#X\n\n",
           boot_args->Revision, boot_args->Version, boot_args->virtBase, boot_args->physBase, boot_args->memSize, boot_args->topOfKernelData, boot_args->deviceTreeP, boot_args->deviceTreeLength, boot_args->CommandLine, boot_args->memSizeActual);
}

#define xnu_kPropNameLength 32
struct xnu_DeviceTreeNodeProperty {
    char name[xnu_kPropNameLength];
    uint32_t length;
    char value[];
};

static void macho_highest_lowest(struct mach_header_64* mh, uint64_t *lowaddr, uint64_t *highaddr) {
    struct load_command* cmd = (struct load_command*)((uint8_t*)mh + sizeof(struct mach_header_64));
    // iterate through all the segments once to find highest and lowest addresses
    uint64_t low_addr_temp = ~0;
    uint64_t high_addr_temp = 0;
    for (unsigned int index = 0; index < mh->ncmds; index++) {
        switch (cmd->cmd) {
            case LC_SEGMENT_64: {
                struct segment_command_64* segCmd = (struct segment_command_64*)cmd;
                if (segCmd->vmaddr < low_addr_temp) {
                    low_addr_temp = segCmd->vmaddr;
                }
                if (segCmd->vmaddr + segCmd->vmsize > high_addr_temp) {
                    high_addr_temp = segCmd->vmaddr + segCmd->vmsize;
                }
                break;
            }
        }
        cmd = (struct load_command*)((char*)cmd + cmd->cmdsize);
    }
    *lowaddr = low_addr_temp;
    *highaddr = high_addr_temp;
}

static int macho_set_devicetree(uint8_t* dtb_data, size_t dtb_size, uint64_t ramdisk_addr, uint64_t ramdisk_size) {
    struct xnu_DeviceTreeNodeProperty* dtNode = NULL;
    for (size_t i = 0; i < dtb_size; i++) {
        if (strncmp((const char*)dtb_data + i, "MemoryMapReserved-0", xnu_kPropNameLength) == 0) {
            dtNode = (struct xnu_DeviceTreeNodeProperty*)(dtb_data + i);
            strncpy(dtNode->name, "RAMDisk", xnu_kPropNameLength);
            uint64_t* valuePtr = (uint64_t*)&dtNode->value;
            valuePtr[0] = ramdisk_addr;
            valuePtr[1] = ramdisk_size;
        } else if (strncmp((const char*)dtb_data + i, "firmware-version", xnu_kPropNameLength) == 0) {
            dtNode = (struct xnu_DeviceTreeNodeProperty*)(dtb_data + i);
            strcpy(dtNode->value, "nBoot-0.1.0-beta");
        }
    }
//     if (!dtNode) {
// //        fprintf(stderr, "Can't write device tree node for ramdisk!\n");
//         return 1;
//     }
}

#define VAtoPA(addr) ((addr - 0xfffffff004004000) + rom_buf)

__weak void board_preboot_os(void)
{
    /* please define board specific board_preboot_os() */
}

static uint64_t arm_load_macho(bool hasRD, uint64_t macho_data, uint32_t macho_len, uint64_t rd_data, uint32_t rd_len, uint64_t afdt_data, uint32_t afdt_len)
{
    printf("\n\n"
           "===========================================================================\n"
           "::\n"
           ":: nBoot for BCM2711, Copyright 2020 Alexander Bradley (@abradleyuno).\n"
           "::\n"
           "::\tVERSION: nBoot-0.1.0-beta\n"
           "::\n"
           "::\tNOTES:  Thanks to Zhuowei Zhang (@zhuowei), matteyeux (@matteyeux),\n"
           "::\t\t@winocm and Kristina Brooks (github:@christinaa).\n"
           "::\n"
           "===========================================================================\n\n\n"
            );

//    printf("macho_data: %#X\nmacho_len: %u\nrd_data: %#X\nrd_len: %u\nafdt_data: %#X\nafdt_len: %u\n\n", macho_data, macho_len, rd_data, rd_len, afdt_data, afdt_len);
    uint64_t rom_buf = 0x4004000; // This is expected by XNU, just make sure to adjust the base address if you change it.
    struct mach_header_64* mh = (struct mach_header_64*)macho_data;
    struct load_command* cmd = (struct load_command*)(macho_data + sizeof(struct mach_header_64));

    // iterate through all the segments once to find highest and lowest addresses
    uint64_t pc = 0;
    uint64_t low_addr_temp;
    uint64_t high_addr_temp;
    macho_highest_lowest(mh, &low_addr_temp, &high_addr_temp);
//    printf("Relocating kernel\n");
    for (unsigned int index = 0; index < mh->ncmds; index++) {
        switch (cmd->cmd) {
            case LC_SEGMENT_64: {
                struct segment_command_64* segCmd = (struct segment_command_64*)cmd;
//                printf("\tLC_SEGMENT_64 relocated to %#X\n", (rom_buf + segCmd->vmaddr - low_addr_temp));
                memcpy(rom_buf + segCmd->vmaddr - low_addr_temp,  macho_data + segCmd->fileoff, segCmd->filesize);
                break;
            }
            case LC_UNIXTHREAD: {
                // grab just the entry point PC
                uint64_t* ptrPc = (uint64_t*)((char*)cmd + 0x110); // for arm64 only.
                pc = VAtoPA(*ptrPc);
                break;
            }
        }
        cmd = (struct load_command*)((char*)cmd + cmd->cmdsize);
    }

    uint64_t load_extra_offset = high_addr_temp;

    uint64_t ramdisk_address = load_extra_offset;
    uint32_t ramdisk_size = 0;

    // load ramdisk if exists
//    printf("\nSetting up Ramdisk\n");
    if (hasRD) {
        memcpy(VAtoPA(ramdisk_address), rd_data, rd_len);
        load_extra_offset = (load_extra_offset + ramdisk_size + 0xffffull) & ~0xffffull;
    }

    uint64_t dtb_address = load_extra_offset;
    // load device tree
//    printf("Setting up Device Tree\n");
    if (true) {
        if (hasRD) {
            macho_set_devicetree(afdt_data, afdt_len, VAtoPA(ramdisk_address), ramdisk_size);
        }
        memset(VAtoPA(dtb_address), 0, afdt_len);
        memcpy(VAtoPA(dtb_address), afdt_data, afdt_len);
        load_extra_offset = (load_extra_offset + afdt_len + 0xffffull) & ~0xffffull;
    }

//    printf("Finished Loading DT and RD\n\n");

    // fixup boot args
    // note: device tree and args must follow kernel and be included in the kernel data size.
    // macho_setup_bootargs takes care of adding the size for the args
    // osfmk/arm64/arm_vm_init.c:arm_vm_prot_init
    uint64_t bootargs_addr = VAtoPA(load_extra_offset);
    uint64_t phys_base = 0x0; // = rom_buf - 0x4004000: Also, this is added to all addreses in the EmbeddedDeviceTrees blob. For a Pi3, Apple sets it to 0x0... So I do the same.
    uint64_t virt_base = 0xfffffff000000000;

//    printf("Setting up bootargs\n\n");
    macho_setup_bootargs(bootargs_addr, virt_base, phys_base, VAtoPA(load_extra_offset), dtb_address, afdt_len);

    call_kernel(pc, bootargs_addr);
}




static int do_bootxnu(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[]){

    if (argc != 7) {
        printf("Error: wrong number of arguments (got %d)\n", argc);
        return 1;
    }

    arm_load_macho(true, simple_strtoull(argv[1], NULL, 16),simple_strtoul(argv[2], NULL, 16),simple_strtoull(argv[3], NULL, 16),simple_strtoul(argv[4], NULL, 16), simple_strtoull(argv[5], NULL, 16), simple_strtoul(argv[6], NULL, 16));
    return 0;
}

static char bootxnu_help_text[] =
        "\t  bootxnu - Relocates xnu, device tree and ramdisk in memory and boots.\n"
        "\t\t\tUSAGE: bootxnu XNU_ADDR XNU_LEN RAMDISK_ADDR RAMDISK_LEN AFDT_ADDR AFDT_LEN \n";

U_BOOT_CMD(
        bootxnu,	15,	1,	do_bootxnu,
"relocate and boot xnu", bootxnu_help_text
);
