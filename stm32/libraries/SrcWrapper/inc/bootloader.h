#ifndef _BOOTLOADER_H_
#define _BOOTLOADER_H_

/* Ensure DTR_TOGGLING_SEQ enabled */
#if defined(BL_LEGACY_LEAF) || defined(BL_HID)
  #ifndef DTR_TOGGLING_SEQ
    #define DTR_TOGGLING_SEQ
  #endif /* DTR_TOGGLING_SEQ || BL_HID */
#endif /* BL_LEGACY_LEAF */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Called from the CDC control handler when the host does a 1200 bps touch -
 * opens the port at 1200 baud and closes it again. That is how the Arduino
 * tooling asks a board to hand itself over to its bootloader, and it is what
 * upload.use_1200bps_touch in boards.txt makes arduino-cli do before running
 * the upload tool.
 *
 * Weakly defined as a no-op, so only variants that want it provide a body.
 */
void usb_1200bps_touch_hook(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* _BOOTLOADER_H_ */
