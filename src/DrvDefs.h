/******************************************************************************
 *
 * DrvDefs.h -- the read-only queries of the Emu68 HDMI AHI driver.
 *
 * The driver answers them through its AHIsub_HardwareControl() vector (the
 * same channel HDMIDiag uses). An attribute a driver does not know returns 0,
 * so an original 4.16 driver simply reports fewer things: HDMI_QUERY_FEATURES
 * is 0 there and 3 in the 4.17 driver (bit 0: the interrupt queries
 * exist, bit 1: master volume). Values come from the status.h of the driver sources (ahi/status.h).
 *
 ******************************************************************************/

#ifndef MIXER_DRVDEFS_H
#define MIXER_DRVDEFS_H

#define HDMI_QUERY_FAULT          0x48444d49UL
#define HDMI_QUERY_GAP            0x48444d4aUL
#define HDMI_QUERY_FILLS          0x48444d4bUL
#define HDMI_QUERY_STAGE          0x48444d4cUL
#define HDMI_QUERY_STATE          0x48444d4dUL
#define HDMI_QUERY_RESULT         0x48444d50UL
#define HDMI_QUERY_RECOVERIES     0x48444d51UL
#define HDMI_QUERY_RECOVERY_FAULT 0x48444d52UL
#define HDMI_QUERY_MIX_BITS       0x48444d57UL
#define HDMI_QUERY_IRQ_ID         0x48444d58UL
#define HDMI_QUERY_IRQS           0x48444d59UL
#define HDMI_QUERY_WAKES          0x48444d5aUL
#define HDMI_QUERY_MAX_BURST      0x48444d5eUL
#define HDMI_QUERY_PRE_CALLS      0x48444d5fUL
#define HDMI_QUERY_PRE_SKIPS      0x48444dc0UL
#define HDMI_QUERY_RESYNC_AT      0x48444dc1UL
#define HDMI_QUERY_FEATURES       0x48444dc2UL
/* The master volume is the standard one of AHI (devices/ahi.h): AHIC_OutputVolume / AHIC_OutputVolume_Query = TAG_USER + 87 / 88,
** a Fixed value (0x10000 = unity). AHI Prefs shows it as the Output volume of a unit. */
#define HDMI_AHIC_OUTPUTVOLUME        (0x80000000UL + 87UL)
#define HDMI_AHIC_OUTPUTVOLUME_QUERY  (0x80000000UL + 88UL)

/* HDMI_QUERY_FEATURES bits */
#define HDMI_FEATURE_IRQ          0x01UL         /* interrupt driver: the interrupt queries exist */
#define HDMI_FEATURE_GAIN         0x02UL         /* master volume */

/* HDMI_QUERY_STATE bits */
#define HDMI_STATE_POISONED  0x01UL
#define HDMI_STATE_OWNED     0x02UL
#define HDMI_STATE_WORKER    0x04UL
#define HDMI_STATE_RUNNING   0x08UL
#define HDMI_STATE_STOPPING  0x10UL

#endif /* MIXER_DRVDEFS_H */
