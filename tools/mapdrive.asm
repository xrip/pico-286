; MapDrive - A utility to map a host drive in the Pico-286 emulator.
;
; This program interfaces with the emulator's built-in network redirector
; (INT 2Fh, Function 11h) to make a host directory available as a DOS drive.
; By default, it maps drive H: to the host's shared directory.
;
; This allows for seamless file access between the DOS environment and the
; host system, simplifying file transfers and development workflows.
;
; To assemble this file, use the Flat Assembler (FASM):
;   fasm mapdrive.asm mapdrive.com
;

    org 100h
    use16

    ; Constants
    DRIVE_LETTER         equ 'H'
    DRIVE_NUMBER         equ 7         ; H = 7
    CDS_ENTRY_SIZE       equ 058h      ; DOS 4+ CDS entry size
    CDS_OFF_FLAGS        equ 043h      ; offset of flags within CDS entry
    ; Flags
    CDSFLAG_PHY          equ 04000h
    CDSFLAG_NET          equ 08000h
    CDSFLAG_NET_PHY      equ (CDSFLAG_PHY or CDSFLAG_NET)

    start:
        ; Get List of Lists: INT 21h, AH=52h -> ES:BX
        mov ah, 52h
        int 21h

        ; Get LASTDRIVE at ES:[BX+21h] (DOS 3.1+)
        mov si, 021h
        mov dl, byte [es:bx+si]    ; DL = lastdrive

        ; Get CDS base pointer (far) at ES:[BX+16h] (offset:segment)
        mov si, 016h
        les bx, [es:bx+si]         ; ES:BX = CDS base

        ; Some OS set CDS to FFFF:FFFF (invalid)
        cmp bx, 0FFFFh
        jne .cds_ok
        mov ax, es          ; move ES to a GP register first
        cmp ax, 0FFFFh
        je .error_cds

    .cds_ok:
        ; Check drive <= lastdrive
        mov al, DRIVE_NUMBER
        cmp al, dl
        jg .error_lastdrive

    .drive_ok:
        ; DI = CDS entry = BX + drive * 58h (use MUL instead of many SHLs)
        mov di, bx                 ; DI = CDS base offset
        xor ax, ax
        mov al, DRIVE_NUMBER       ; AL = drive (0..25)
        mov bl, CDS_ENTRY_SIZE     ; BL = 88 (0x58)
        mul bl                     ; AX = AL * BL
        add di, ax                 ; DI = CDS base + drive * 58h (entry address)

    .drive_free:
        ; Set flags = NET|PHY (C000h)
        mov word [es:di+CDS_OFF_FLAGS], CDSFLAG_NET_PHY

        ; Set current_path = "H:\"
        mov byte [es:di+0], DRIVE_LETTER
        mov byte [es:di+1], ':'
        mov byte [es:di+2], '\'
        mov byte [es:di+3], 0

        ; Get SDA pointer: INT 21h, AX=5D06h -> DS:SI
        mov ax, 5D06h
        push ds
        push si
        int 21h
        ; DS:SI now points to SDA; save into BX:DX for INT 2Fh
        mov bx, ds
        mov dx, si
        pop si
        pop ds

        ; INT 2Fh, AX=1100h, BX:DX = SDA
        mov ax, 1100h
        int 2Fh

        ; Success message
        mov dx, msg_ok
        mov ah, 09h
        int 21h

        jmp exit

    .error_cds:
        mov dx, err_cds_fail
        mov ah, 09h
        int 21h
        jmp exit

    .error_lastdrive:
        mov dx, err_lastdrive
        mov ah, 09h
        int 21h

    exit:
        mov ax, 4C00h
        int 21h

    ; Data
    err_cds_fail  db 'Error: Could not get CDS for drive H:.',13,10,'$'
    err_lastdrive db 'Your LASTDRIVE setting in CONFIG.SYS might be set to H',13,10,'$'
    msg_ok        db 'Drive H: successfully mapped as a host drive.',13,10,'$'