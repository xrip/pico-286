; Pico-286 Filesystem Redirector TSR for NASM
ORG 100h

section .text

main:
    jmp    installation_code

; --- Resident Part ---
; This is the new INT 2Fh handler. It must come first.
new_int2f_handler:
    cmp    ah, 11h      ; Is it a redirector call (AH=11h)?
    jne    chain        ; If not, pass control to the original handler.

    ; It is a redirector call. Let the emulator handle it.
    int    88h          ; Custom interrupt for our emulator's redirector.
    iret                ; Return from our interrupt handler.

chain:
    ; This is not for us, so chain to the original INT 2Fh handler.
    jmp    dword [old_int2f_handler]

; Data for the resident part
old_int2f_handler   dd 0  ; To store the address of the old handler

; --- Installation Part (will not be resident) ---
installation_code:
    ; 1. Install the TSR
    ; Get the original INT 2Fh vector
    mov    ax, 352Fh    ; DOS - Get Vector (AH=35h, AL=2Fh)
    int    21h          ; Old vector is returned in ES:BX
    mov    [old_int2f_handler], bx
    mov    [old_int2f_handler+2], es

    ; Set our new INT 2Fh handler
    mov    ax, 252Fh    ; DOS - Set Vector (AH=25h, AL=2Fh)
    mov    dx, new_int2f_handler
    int    21h

    ; 2. Print installation message
    mov    dx, install_msg
    mov    ah, 09h
    int    21h

    ; 3. Terminate and Stay Resident
    ; The resident part ends right before 'installation_code'.
    mov    dx, installation_code
    int    27h          ; DOS - TSR (size in DX)

; Data for the installation part
install_msg db 'Pico-286 Filesystem Redirector Installed.', 0Dh, 0Ah, '$'
