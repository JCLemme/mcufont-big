CEXAMPLE_MOD_DIR := $(USERMOD_DIR)

# Add all C files to SRC_USERMOD.
SRC_USERMOD += $(CEXAMPLE_MOD_DIR)/modmcufont.c
SRC_USERMOD += $(CEXAMPLE_MOD_DIR)/mcufont/decoder/mf_bwfont.c
SRC_USERMOD += $(CEXAMPLE_MOD_DIR)/mcufont/decoder/mf_encoding.c
SRC_USERMOD += $(CEXAMPLE_MOD_DIR)/mcufont/decoder/mf_font.c
SRC_USERMOD += $(CEXAMPLE_MOD_DIR)/mcufont/decoder/mf_justify.c
SRC_USERMOD += $(CEXAMPLE_MOD_DIR)/mcufont/decoder/mf_kerning.c
SRC_USERMOD += $(CEXAMPLE_MOD_DIR)/mcufont/decoder/mf_rlefont.c
SRC_USERMOD += $(CEXAMPLE_MOD_DIR)/mcufont/decoder/mf_scaledfont.c
SRC_USERMOD += $(CEXAMPLE_MOD_DIR)/mcufont/decoder/mf_wordwrap.c

# We can add our module folder to include paths if needed
# This is not actually needed in this example.
CFLAGS_USERMOD += -I$(CEXAMPLE_MOD_DIR)
