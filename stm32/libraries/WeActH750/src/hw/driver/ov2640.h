/*
 * ov2640.h
 *
 * The OV2640 that ships on this board's camera module.
 *
 * Same shape as ov7725.h next door: cameraInit() probes the SCCB bus, and
 * whichever sensor answers gets its Open() called to fill in the camera_t
 * function table. Which one is compiled in is hw_def.h's choice.
 */

#ifndef SRC_HW_DRIVER_OV2640_H_
#define SRC_HW_DRIVER_OV2640_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#include "hw/driver/camera.h"

#ifdef _USE_HW_OV2640

bool ov2640Init(void);
bool ov2640Open(camera_t *sensor);

#endif

#ifdef __cplusplus
}
#endif

#endif /* SRC_HW_DRIVER_OV2640_H_ */
