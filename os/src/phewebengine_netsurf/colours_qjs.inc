/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only
 */

			nsoption_set_colour(sys_colour_AccentColor, 0x00666666);
			nsoption_set_colour(sys_colour_AccentColorText, 0x00ffffff);
			nsoption_set_colour(sys_colour_ActiveText, 0x000000ee);
			nsoption_set_colour(sys_colour_ButtonBorder, 0x00aaaaaa);
			nsoption_set_colour(sys_colour_ButtonFace, 0x00dddddd);
			nsoption_set_colour(sys_colour_ButtonText, 0x00000000);
			nsoption_set_colour(sys_colour_Canvas, 0x00aaaaaa);
			nsoption_set_colour(sys_colour_CanvasText, 0x00000000);
			nsoption_set_colour(sys_colour_Field, 0x00f1f1f1);
			nsoption_set_colour(sys_colour_FieldText, 0x00000000);
			nsoption_set_colour(sys_colour_GrayText, 0x00777777);
			nsoption_set_colour(sys_colour_Highlight, 0x00ee0000);
			nsoption_set_colour(sys_colour_HighlightText, 0x00000000);
			nsoption_set_colour(sys_colour_LinkText, 0x00ee0000);
			nsoption_set_colour(sys_colour_Mark, 0x0000ffff);
			nsoption_set_colour(sys_colour_MarkText, 0x00000000);
			nsoption_set_colour(sys_colour_SelectedItem, 0x00e48435);
			nsoption_set_colour(sys_colour_SelectedItemText, 0x00ffffff);
			nsoption_set_colour(sys_colour_VisitedText, 0x008b1a55);
	/* JavaScript (QuickJS) is on by default in the NetSurf QuickJS tree; cut runaway scripts off (ARMv6 is slow) */
	nsoption_set_int(script_timeout, 10);
