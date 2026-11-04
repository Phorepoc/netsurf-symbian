/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * ftmodules.h -- the FreeType modules the NetSurf engine needs (TrueType and CFF/OpenType outlines, the hinters, the
 * rasterizers); selected with -DFT_CONFIG_MODULES_H=<ftmodules.h>. FreeType 2.13's form: FT_USE_MODULE( type, class ),
 * as in its include/freetype/config/ftmodule.h. */
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )
