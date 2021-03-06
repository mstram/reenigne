org 0
cpu 8086

  ; Disable interrupts and turn off refresh
  cli
  mov al,0x60  ; Timer 1, write LSB, mode 0, binary
  out 0x43,al
  mov al,0x01  ; Count = 0x0001 so we'll stop almost immediately
  out 0x41,al

  ; Set "stosb" destination to be CGA memory
  mov ax,0xb800
  mov es,ax
  xor di,di

  ; Set argument for MUL
  mov cl,1

  ; Ensure "stosb" won't take us out of video memory
  cld

  ; Go into CGA lockstep. The delays were determined by trial and error.
  jmp $+2      ; Clear prefetch queue
  stosb        ; From 16 down to 3 possible CGA/CPU relative phases.
  mov al,0x01
  mul cl
  stosb        ; Down to 2 possible CGA/CPU relative phases.
  mov al,0x7f
  mul cl
  stosb        ; Down to 1 possible CGA/CPU relative phase: lockstep achieved.

  ; Fill video memory with 0s
  mov cx,0x2000
  mov ax,0x0000
  xor di,di
  rep stosw

  ; Set mode register to 2bpp graphics mode so that the palette register
  ; affects the entire screen.
  mov dx,0x03d8
  mov al,0x0a
  out dx,al

  ; Set up CRTC for 1 character by 2 scanline "frame". This gives us 2 lchars
  ; per frame.
  mov dl,0xd4
  ;   0xff Horizontal Total
  mov ax,0x0000
  out dx,ax
  ;   0xff Horizontal Displayed                         28
  mov ax,0x0101
  out dx,ax
  ;   0xff Horizontal Sync Position                     2d
  mov ax,0x2d02
  out dx,ax
  ;   0x0f Horizontal Sync Width                        0a
  mov ax,0x0a03
  out dx,ax
  ;   0x7f Vertical Total                               7f
  mov ax,0x0104
  out dx,ax
  ;   0x1f Vertical Total Adjust                        06
  mov ax,0x0005
  out dx,ax
  ;   0x7f Vertical Displayed                           64
  mov ax,0x0106
  out dx,ax
  ;   0x7f Vertical Sync Position                       70
  mov ax,0x0007
  out dx,ax
  ;   0x03 Interlace Mode                               02
  mov ax,0x0208
  out dx,ax
  ;   0x1f Max Scan Line Address                        01
  mov ax,0x0009
  out dx,ax

  times 512 nop
  nop

  ; To get the CRTC into lockstep with the CGA and CPU, we need to figure out
  ; which of the two possible CRTC states we're in and switch states if we're
  ; in the wrong one by waiting for an odd number of lchars more in one code
  ; path than in the other. To keep CGA and CPU in lockstep, we also need both
  ; code paths to take the same time mod 3 lchars, so we wait 3 lchars more on
  ; one code path than on the other.
  mov dl,0xda
  in al,dx
  jmp $+2
  test al,1
  jz shortPath
  times 2 nop
  jmp $+2
shortPath:


  ; Mode                                                0a
  ;      1 +HRES                                         0
  ;      2 +GRPH                                         2
  ;      4 +BW                                           0
  ;      8 +VIDEO ENABLE                                 8
  ;   0x10 +1BPP                                         0
  ;   0x20 +ENABLE BLINK                                 0
  mov dl,0xd8
  mov al,0x0a
  out dx,al

  ; Palette                                             00
  ;      1 +OVERSCAN B                                   0
  ;      2 +OVERSCAN G                                   0
  ;      4 +OVERSCAN R                                   0
  ;      8 +OVERSCAN I                                   0
  ;   0x10 +BACKGROUND I                                 0
  ;   0x20 +COLOR SEL                                    0
  inc dx
  mov al,0
  out dx,al

  mov dl,0xd4

  ;   0xff Horizontal Total                             38
  mov ax,0x3800
  out dx,ax

  ;   0xff Horizontal Displayed                         28
  mov ax,0x2801
  out dx,ax

  ;   0xff Horizontal Sync Position                     2d
  mov ax,0x2d02
  out dx,ax

  ;   0x0f Horizontal Sync Width                        0a
  mov ax,0x0a03
  out dx,ax

  ;   0x7f Vertical Total                               7f
  mov ax,0x7f04
  out dx,ax

  ;   0x1f Vertical Total Adjust                        06
  mov ax,0x0605
  out dx,ax

  ;   0x7f Vertical Displayed                           64
  mov ax,0x6406
  out dx,ax

  ;   0x7f Vertical Sync Position                       70
  mov ax,0x7007
  out dx,ax

  ;   0x03 Interlace Mode                               02
  mov ax,0x0208
  out dx,ax

  ;   0x1f Max Scan Line Address                        01
  mov ax,0x0109
  out dx,ax

  ; Cursor Start                                        1f
  ;   0x1f Cursor Start                                 1f
  ;   0x60 Cursor Mode                                   0
  mov ax,0x1f0a
  out dx,ax

  ;   0x1f Cursor End                                   1f
  inc ax
  out dx,ax

  ;   0x3f Start Address (H)                            00
  mov ax,0x000c
  out dx,ax

  ;   0xff Start Address (L)                            00
  inc ax
  out dx,ax

  ;   0x3f Cursor (H)                                   00
  inc ax
  out dx,ax

  ;   0xff Cursor (L)                                   00
  inc ax
  out dx,ax

  ; Set DX to palette register port address
  mov dl,0xd9

  ; Fill prefetch queue
  mov al,0
  mov cl,0
  mul cl

  ; Do pattern

;%macro scanLineSegment 0  ; 15 (measured)
;    inc ax
;    out dx,al
;%endmacro
;
;%macro scanLine 0           ;         304
;  %rep 20
;    scanLineSegment         ; 20*15 = 300
;  %endrep
;    sahf                    ;           4
;%endmacro
;
;pattern:                    ;           79648
;%rep 261
;  scanLine                  ; 261*304 = 79344
;%endrep
;%rep 12
;  scanLineSegment           ; 12*15 =     180
;%endrep
;  times 4 sahf
;  mov al,3
;  mov cl,0
;  mul cl
;  jmp pattern               ;              24

  mov cl,1

pattern:
  %rep 261
    inc ax
    out dx,al
    mov bl,al
    mov al,0x01
    mul cl
    mov al,bl
    times 50 nop
  %endrep
  mov al,0x03
  mul cl
  times 52 nop
  jmp pattern
