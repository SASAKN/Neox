// For using UEFI Library
#include  <Uefi.h>
#include  <Library/UefiLib.h>
#include  <Library/UefiBootServicesTableLib.h>
#include  <Library/UefiRuntimeServicesTableLib.h>
#include  <Library/PrintLib.h>
#include  <Library/MemoryAllocationLib.h>
#include  <Library/BaseMemoryLib.h>
#include  <Protocol/LoadedImage.h>
#include  <Protocol/SimpleFileSystem.h>
#include  <Protocol/DiskIo2.h>
#include  <Protocol/BlockIo.h>
#include  <Guid/FileInfo.h>

// For using kernel
#include "include/mem.h"
#include "include/elf.h"

// For Debug
#include "uefilib/inc/efi.h"
#include "uefilib/inc/efilib.h"

//EDK2から正しく読み込まれなかったら
#ifndef MAX_UINT64
#define MAX_UINT64 18446744073709551615
#endif

#ifndef MIN
#define MAX(a, b) (a < b ? b : a)
#define MIN(a, b) (a < b ? a : b)
#endif

// 文字を表示する関係

void PrintOK(void)
{
    gST->ConOut->SetAttribute(ST->ConOut, 0x02); /* 緑で、OKを表示 */
    gST->ConOut->OutputString(ST->ConOut, L"[ OK ]");
    gST->ConOut->SetAttribute(ST->ConOut, 0x0F); /* 白に戻す */
};

void PrintWarn(void)
{
    gST->ConOut->SetAttribute(ST->ConOut, 0x0E); /* 黄色で、Warnを表示 */
    gST->ConOut->OutputString(ST->ConOut, L"[ Warn ]");
    gST->ConOut->SetAttribute(ST->ConOut, 0x0F); /* 白に戻す */
};

void PrintError(void)
{
    gST->ConOut->SetAttribute(ST->ConOut, 0x04); /* あかで、Errorを表示 */
    gST->ConOut->OutputString(ST->ConOut, L"[ Error ! ]");
    gST->ConOut->SetAttribute(ST->ConOut, 0x0F); /* 白に戻す */
};

void PrintGoodBye(void)
{
    gST->ConOut->SetAttribute(ST->ConOut, EFI_BLUE); /* あかで、Errorを表示 */
    gST->ConOut->OutputString(ST->ConOut, L"[ GoodBye ]");
    gST->ConOut->SetAttribute(ST->ConOut, 0x0F); /* 白に戻す */
};

//ファイル関係

//ルートディレクトリーを開くための関数
EFI_STATUS open_root_dir(EFI_HANDLE IM, EFI_FILE_PROTOCOL** root) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *lip;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfsp;
    // Loaded Image Protocol
    status = gBS->OpenProtocol(IM, &gEfiLoadedImageProtocolGuid, (VOID **)&lip, IM, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(status)) {
        PrintError();
        Print(L"Open LIP Protocol\n");
        return status;
    };
    // Simple File System Protocol
    status = gBS->OpenProtocol(lip->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&sfsp, IM, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(status)) {
        PrintError();
        Print(L"Open SFSP Protocol\n");
        return status;
    };
    //記憶装置について取得
    status = sfsp->OpenVolume(sfsp, root);
    if (EFI_ERROR(status)) {
        PrintError();
        Print(L"Open Volume\n");
        return status;
    };
    return EFI_SUCCESS;
}

//ブロックデバイス関係

//ブロックデバイスについての情報を表示
EFI_STATUS open_block_io_protocol(EFI_HANDLE IM, EFI_BLOCK_IO_PROTOCOL **block_io) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *lip;
    // Loaded Image Protocol
    status = gBS->OpenProtocol(IM, &gEfiLoadedImageProtocolGuid, (VOID **)&lip, IM, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(status)) {
        PrintError();
        Print(L"Open LIP Protocol");
        return status;
    };
    // Block IO Protocol
    status = gBS->OpenProtocol(lip->DeviceHandle, &gEfiBlockIoProtocolGuid, (VOID **)block_io, IM, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if (EFI_ERROR(status)) {
        PrintError();
        Print(L"Open Block IO Protocol");
        return status;
    };
    return EFI_SUCCESS;
}

//ディスク情報を表示
EFI_STATUS open_disk(EFI_BLOCK_IO_PROTOCOL *block_io, UINT32 media_id, UINTN read_bytes, VOID** buffer) {
    EFI_STATUS status;
    //一時的なメモリーを確保
    status = gBS->AllocatePool(EfiLoaderData, read_bytes, buffer);
    if (EFI_ERROR(status)) {
        PrintError();
        Print(L"Allocate Pool\n");
        return status;
    }
    //ブロックデバイスを読む
    status = block_io->ReadBlocks(block_io, media_id, 0, read_bytes, *buffer);
    if (EFI_ERROR(status)) {
        PrintError();
        Print(L"Read Blocks\n");
        return status;
    }
    //デバッグ情報を表示
    Print(L"[ DEBUG ] LBA 0 Media ID %x, Read_Bytes %x, Buffer %x\n",media_id, read_bytes, buffer);
    //実は、OSには、ドライバーがしっかりあるためこれ以降は、BLOCK_IOを使わない
}


//メモリーマップ関係

//メモリーマップを取得
EFI_STATUS get_memmap(struct MemoryMap *map) {
    EFI_STATUS status;
    status = gBS->GetMemoryMap(&map->map_size, NULL, &map->map_key, &map->descriptor_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || !map->map_size) {
        PrintError();
        Print(L"Get Memory Map\n");
    }
    map->map_size += 4 * map->descriptor_size;
    char memmap_buf[map->map_size];
    map->buffer = memmap_buf;
    if (!map->map_size) {
        PrintError();
        Print(L"Allocate Memory\n");
        return 1;
    }
    //2回目の実行で確実に取得できる
    status = gBS->GetMemoryMap(&map->map_size, (EFI_MEMORY_DESCRIPTOR *)map->buffer, &map->map_key, &map->descriptor_size, NULL);
    if (EFI_ERROR(status)) {
        PrintError();
        Print(L"Get Memory Map\n");
    }
    //これを計算することによって、Printなどで呼び出すときに呼び出しやすくなる
    map->memmap_desc_entry = map->map_size / map->descriptor_size;
    PrintOK();
    Print(L"Get Memory Map\n");
    return EFI_SUCCESS;
};

//メモリーマップを画面に表示
EFI_STATUS print_memmap(struct MemoryMap *map) {
    EFI_STATUS status;
    Print(L"\n [ INFO ] MemoryMap \n");
    CHAR8 *header = "Index, Buffer, Type, Type(name), PhysicalStart, VirtualStart, NumberOfPages, Size, Attribute";
    Print("%s", )
    UINT32 i;
    EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)map->buffer;
    for (i = 0; i < map->memmap_desc_entry; i++) {
        Print(L"%02d, %016x, %02x, %s, %016x, %016x, %016x, %d, %016x \n", i, desc, desc->Type, get_memtype_name(desc->Type), desc->PhysicalStart, desc->VirtualStart, desc->NumberOfPages, desc->NumberOfPages, desc->Attribute);
        desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)desc + map->descriptor_size);
    }
    Print(L"\n");
    PrintOK();
    Print(L"Print Memory Map\n");
    return EFI_SUCCESS;
};

//メモリーマップをファイルに保存する関数
EFI_STATUS save_memmap(struct MemoryMap *map, EFI_FILE_PROTOCOL *f) {
    EFI_STATUS status;
    CHAR8 memmap_buffer[1024];
    UINTN size;
    char *header = "Index, Buffer, Type, Type(name), PhysicalStart, VirtualStart, NumberOfPages, Size, Attribute";
    size = AsciiStrlen(header);
    //ヘッダーの書き込み
    status = f->Write(f, &size, header);
    if(EFI_ERROR(status)) {
        PrintError();
        Print(L"Open File\n");
        return status;
    }
    Print(L"[ DEBUG ] map->buffer = %08lx, map->map_size = %08lx\n", map->buffer, map->map_size);
    EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)map->buffer;
    //メモリーマップの書き込み
    for (UINT32 i = 0; i < map->memmap_desc_entry; i++) {
        size = AsciiSPrint(memmap_buffer, sizeof(memmap_buffer), "%02d, %016x, %02x, %s, %016x, %016x, %016x, %d, %016x \n", i, desc, desc->Type, get_memtype_name(desc->Type), desc->PhysicalStart, desc->VirtualStart, desc->NumberOfPages, desc->NumberOfPages, desc->Attribute);
        status = f->Write(f, &size, memmap_buffer);
        if(EFI_ERROR(status)) {
            PrintError();
            Print(L"Save Memory Map\n");
            return status;
        }
    }
}

//ELF解析系

//メモリアドレスの計算
void calc_address_range(elf64_ehdr *ehdr, UINT64 *start, UINT64 *end) {
    //初期値の設定
    start = 0;
    end = 0;
    Elf64_Half i = 0;
    //ループでアドレスの計算
    for (i < ehdr->e_phnum; i++) {
        elf64_phdr* phdr = (elf64_phdr *)((UINT64)ehdr + ehdr->e_phoff + ehdr->e_phentsize * i);
        if (phdr[i].p_type != PT_LOAD) continue;
        if (!start) {
            start = phdr->p_paddr;
            end = phdr->p_paddr +phdr->p_memsz;
        }
        start = MIN(start, phdr->p_paddr);
        end = MAX(end, phdr->p_paddr + phdr->p_memsz);
    };
};

//セグメントのコピーとロード
void copy_load_segments(elf64_ehdr *ehdr) {
    for (Elf64_Half i = 0; i < ehdr->e_phnum; i++) {
        elf64_phdr* phdr = (elf64_phdr *)((UINT64)ehdr + ehdr->e_phoff + ehdr->e_phentsize * i);
        if (phdr->p_type != PT_LOAD) continue;
        CopyMem((VOID *)phdr->p_paddr, (char *)ehdr + phdr->p_offset, phdr->p_filesz);
    }
}


//カーネルパラメーター用意系

//ラッパー関数系

//ExitBootServices
EFI_STATUS exit_bs(EFI_HANDLE IM, struct MemoryMap *map) {
    EFI_STATUS status;
    PrintGoodBye();
    status = gBS->ExitBootServices(IM, map->map_key);
    if (EFI_ERROR(status)) {
        PrintError();
        Print(L"Exit Boot Services\n");
        //メモリーマップを取得して再度試す
        Print(L"trying again ...\n");
        get_memmap(map);
        status = gBS->ExitBootServices(IM, map->map_key);
        if (EFI_ERROR(status)) {
            //どうしても無理なら終了
            PrintError();
            Print(L"Fatal Error :Exit Boot Services\n");
            gBS->Exit(IM, EFI_SUCCESS, 0, NULL);
        }
    }
}