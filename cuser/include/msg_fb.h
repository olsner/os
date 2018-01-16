#ifndef __MSG_FB_H
#define __MSG_FB_H

enum msg_fb
{
	/**
	 * Set video mode (size and bpp).
	 *
	 * arg1: width << 32 | height
	 * arg2: bits per pixel
	 *
	 * The user should map the handle, as much memory as needed for the given
	 * resolution. (Or more, to support use of MSG_PRESENT.)
	 */
	MSG_SET_VIDMODE = MSG_USER,
	/**
	 * For 4- or 8-bit modes, update a palette entry.
	 *
	 * arg1: index << 24 | r << 16 | g << 8 | b
	 */
	MSG_SET_PALETTE,
	/**
	 * Placeholder for future (window manager driven) api - present a frame.
	 *
	 * arg1: origin of frame to present, relative mapped memory area.
	 * (an id? some way for the client to know when this has been presented and
	 * the previous frame will not be used by the window manager again)
	 */
	MSG_PRESENT,
};

#endif /* __MSG_FB_H */
