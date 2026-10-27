#ifndef USB_AUDIO_DESC_H
#define USB_AUDIO_DESC_H

#define UAC2_ENTITY_CLOCK 0x04
#define UAC2_ENTITY_FEATURE_UNIT 0x02

#define TUD_AUDIO_SPEAKER_STEREO_FB_DESC_LEN                         \
  (TUD_AUDIO_DESC_IAD_LEN + TUD_AUDIO_DESC_STD_AC_LEN +              \
   TUD_AUDIO_DESC_CS_AC_LEN + TUD_AUDIO_DESC_CLK_SRC_LEN +           \
   TUD_AUDIO_DESC_INPUT_TERM_LEN + TUD_AUDIO_DESC_OUTPUT_TERM_LEN +  \
   TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN +                     \
   TUD_AUDIO_DESC_STD_AS_INT_LEN + TUD_AUDIO_DESC_STD_AS_INT_LEN +   \
   TUD_AUDIO_DESC_CS_AS_INT_LEN + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN + \
   TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN +                                \
   TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN)

#define TUD_AUDIO_SPEAKER_STEREO_FB_DESCRIPTOR(                        \
    _itfnum, _stridx, _nBytesPerSample, _nBitsUsedPerSample, _epout,   \
    _epoutsize)                                                        \
  TUD_AUDIO_DESC_IAD(_itfnum, 0x02, 0x00),                                  \
      TUD_AUDIO_DESC_STD_AC(_itfnum, 0x00, _stridx),                         \
      TUD_AUDIO_DESC_CS_AC(                                                  \
          0x0200, AUDIO_FUNC_DESKTOP_SPEAKER,                                \
          TUD_AUDIO_DESC_CLK_SRC_LEN + TUD_AUDIO_DESC_INPUT_TERM_LEN +       \
              TUD_AUDIO_DESC_OUTPUT_TERM_LEN +                               \
              TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN,                   \
          AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS),                           \
      TUD_AUDIO_DESC_CLK_SRC(                                                \
          0x04, AUDIO_CLOCK_SOURCE_ATT_INT_PRO_CLK,                          \
          (AUDIO_CTRL_RW << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS), 0x01,      \
          0x00),                                                             \
      TUD_AUDIO_DESC_INPUT_TERM(0x01, AUDIO_TERM_TYPE_USB_STREAMING, 0x00,   \
                                0x04, 0x02,                                  \
                                AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, 0x00,   \
                                0, 0x00),                                    \
      TUD_AUDIO_DESC_OUTPUT_TERM(0x03, AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER,  \
                                 0x01, 0x02, 0x04, 0x0000, 0x00),            \
      TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL(                               \
          0x02, 0x01,                                                        \
          AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS |                \
              AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS,           \
          AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS |                \
              AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS,           \
          AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS |                \
              AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS,           \
          0x00),                                                             \
      TUD_AUDIO_DESC_STD_AS_INT((uint8_t)((_itfnum) + 1), 0x00, 0x00, 0x00), \
      TUD_AUDIO_DESC_STD_AS_INT((uint8_t)((_itfnum) + 1), 0x01, 0x01, 0x00), \
      TUD_AUDIO_DESC_CS_AS_INT(                                              \
          0x01, AUDIO_CTRL_NONE, AUDIO_FORMAT_TYPE_I,                        \
          AUDIO_DATA_FORMAT_TYPE_I_PCM, 0x02,                                \
          AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, 0x00),                        \
      TUD_AUDIO_DESC_TYPE_I_FORMAT(_nBytesPerSample, _nBitsUsedPerSample),   \
      TUD_AUDIO_DESC_STD_AS_ISO_EP(                                          \
          _epout,                                                            \
          (uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS |                         \
                    (uint8_t)TUSB_ISO_EP_ATT_SYNCHRONOUS |                   \
                    (uint8_t)TUSB_ISO_EP_ATT_DATA),                          \
          _epoutsize, 0x01),                                                 \
      TUD_AUDIO_DESC_CS_AS_ISO_EP(                                           \
          AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO_CTRL_NONE,   \
          AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC, 0x0001)

#endif
