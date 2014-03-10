/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <config.h>
#include <fnmatch.h>
#include <gdk/gdk.h>

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#include <ft2build.h>
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include <pango/pango.h>
#include <pango/pangofc-fontmap.h>
#include <fontconfig/fontconfig.h>

#include <cra-plugin.h>

/**
 * cra_plugin_get_name:
 */
const gchar *
cra_plugin_get_name (void)
{
	return "font";
}

/**
 * cra_plugin_add_globs:
 */
void
cra_plugin_add_globs (CraPlugin *plugin, GPtrArray *globs)
{
	cra_plugin_add_glob (globs, "/usr/share/fonts/*/*.otf");
	cra_plugin_add_glob (globs, "/usr/share/fonts/*/*.ttf");
}

/**
 * _cra_plugin_check_filename:
 */
static gboolean
_cra_plugin_check_filename (const gchar *filename)
{
	if (fnmatch ("/usr/share/fonts/*/*.otf", filename, 0) == 0)
		return TRUE;
	if (fnmatch ("/usr/share/fonts/*/*.ttf", filename, 0) == 0)
		return TRUE;
	return FALSE;
}

/**
 * cra_plugin_check_filename:
 */
gboolean
cra_plugin_check_filename (CraPlugin *plugin, const gchar *filename)
{
	return _cra_plugin_check_filename (filename);
}

/**
 * cra_font_fix_metadata:
 */
static void
cra_font_fix_metadata (CraApp *app)
{
	const gchar *tmp;
	const gchar *value;
	gchar *icon_tmp;
	GList *l;
	GList *langs = NULL;
	GString *str = NULL;
	guint j;
	PangoLanguage *plang;
	struct {
		const gchar	*lang;
		const gchar	*value;
	} text_icon[] =  {
		{ "en",		"Aa" },
		{ NULL, NULL } };
	struct {
		const gchar	*lang;
		const gchar	*value;
	} text_sample[] =  {
		{ "en",		"How quickly daft jumping zebras vex." },
		{ NULL, NULL } };

	/* ensure FontSampleText is defined */
	if (cra_app_get_metadata_item (app, "FontSampleText") == NULL) {
		for (j = 0; text_sample[j].lang != NULL; j++) {
			tmp = cra_app_get_language (app, text_sample[j].lang);
			if (tmp != NULL) {
				cra_app_add_metadata (app,
						      "FontSampleText",
						      text_sample[j].value);
				break;
			}
		}
	}

	/* ensure FontIconText is defined */
	if (cra_app_get_metadata_item (app, "FontIconText") == NULL) {
		for (j = 0; text_icon[j].lang != NULL; j++) {
			tmp = cra_app_get_language (app, text_icon[j].lang);
			if (tmp != NULL) {
				cra_app_add_metadata (app,
						      "FontIconText",
						      text_icon[j].value);
				break;
			}
		}
	}

	/* can we use a pango version */
	langs = cra_app_get_languages (app);
	if (cra_app_get_metadata_item (app, "FontIconText") == NULL) {
		for (l = langs; l != NULL; l = l->next) {
			tmp = l->data;
			plang = pango_language_from_string (tmp);
			value = pango_language_get_sample_string (plang);
			if (value == NULL)
				continue;
			cra_app_add_metadata (app,
					      "FontSampleText",
					      value);
			icon_tmp = g_utf8_substring (value, 0, 2);
			cra_app_add_metadata (app,
					      "FontIconText",
					      icon_tmp);
			g_free (icon_tmp);
		}
	}

	/* still not defined? */
	if (cra_app_get_metadata_item (app, "FontSampleText") == NULL) {
		str = g_string_sized_new (1024);
		for (l = langs; l != NULL; l = l->next) {
			tmp = l->data;
			g_string_append_printf (str, "%s, ", tmp);
		}
		if (str->len > 2)
			g_string_truncate (str, str->len - 2);
		cra_package_log (cra_app_get_package (app),
				 CRA_PACKAGE_LOG_LEVEL_WARNING,
				 "No FontSampleText for langs: %s",
				 str->str);
	}
	g_list_free (langs);
	if (str != NULL)
		g_string_free (str, TRUE);
}

/**
 * cra_font_add_metadata:
 */
static void
cra_font_add_metadata (CraApp *app, FT_Face ft_face)
{
	FT_SfntName sfname;
	gchar *val;
	guint i;
	guint j;
	guint len;
	struct {
		FT_UShort	 idx;
		const gchar	*key;
	} tt_idx_to_md_name[] =  {
		{ TT_NAME_ID_FONT_FAMILY,		"FontFamily" },
		{ TT_NAME_ID_FONT_SUBFAMILY,		"FontSubFamily" },
		{ TT_NAME_ID_FULL_NAME,			"FontFullName" },
		{ TT_NAME_ID_PREFERRED_FAMILY,		"FontParent" },
		{ 0, NULL } };

	if (!FT_IS_SFNT (ft_face))
		return;

	/* look at the metadata table */
	len = FT_Get_Sfnt_Name_Count (ft_face);
	for (i = 0; i < len; i++) {
		FT_Get_Sfnt_Name (ft_face, i, &sfname);
		for (j = 0; tt_idx_to_md_name[j].key != NULL; j++) {
			if (sfname.name_id != tt_idx_to_md_name[j].idx)
				continue;
			val = g_locale_to_utf8 ((gchar *) sfname.string,
						sfname.string_len,
						NULL, NULL, NULL);
			if (val == NULL)
				continue;
			cra_app_add_metadata (app, tt_idx_to_md_name[j].key, val);
			g_free (val);
		}
	}
}

/**
 * cra_font_get_pixbuf:
 */
static GdkPixbuf *
cra_font_get_pixbuf (FT_Face ft_face,
		     guint width,
		     guint height,
		     const gchar *text)
{
	cairo_font_face_t *font_face;
	cairo_surface_t *surface;
	cairo_t *cr;
	cairo_text_extents_t te;
	GdkPixbuf *pixbuf;
	guint text_size = 64;
	guint border_width = 8;

	/* set up font */
	surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
					      width, height);
	cr = cairo_create (surface);
	font_face = cairo_ft_font_face_create_for_ft_face (ft_face, FT_LOAD_NO_AUTOHINT);
	cairo_set_font_face (cr, font_face);

	/* calculate best font size */
	while (text_size-- > 0) {
		cairo_set_font_size (cr, text_size);
		cairo_text_extents (cr, text, &te);
		if (te.width < width - (border_width * 2) &&
		    te.height < height - (border_width * 2))
			break;
	}

	/* center text and blit to a pixbuf */
	cairo_move_to (cr,
		       (width / 2) - te.width / 2 - te.x_bearing,
		       (height / 2) - te.height / 2 - te.y_bearing);
	cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
	cairo_show_text (cr, text);
	pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, width, height);

	cairo_destroy (cr);
	cairo_font_face_destroy (font_face);
	cairo_surface_destroy (surface);
	return pixbuf;
}

/**
 * cra_font_add_screenshot:
 */
static void
cra_font_add_screenshot (CraApp *app, FT_Face ft_face)
{
	const gchar *tmp;
	CraScreenshot *ss;
	gchar *caption;
	GdkPixbuf *pixbuf;

	tmp = cra_app_get_metadata_item (app, "FontSampleText");
	if (tmp == NULL)
		return;

	ss = cra_screenshot_new (cra_app_get_package (app),
				 cra_app_get_id (app));
	pixbuf = cra_font_get_pixbuf (ft_face, 640, 48, tmp);
	cra_screenshot_set_pixbuf (ss, pixbuf);
	caption = g_strdup_printf ("%s – %s",
				   cra_app_get_metadata_item (app, "FontFamily"),
				   cra_app_get_metadata_item (app, "FontSubFamily"));
	cra_screenshot_set_caption (ss, caption);
	cra_screenshot_set_only_source (ss, TRUE);
	cra_app_add_screenshot (app, ss);

	g_free (caption);
	g_object_unref (pixbuf);
	g_object_unref (ss);
}

/**
 * cra_plugin_process_filename:
 */
static void
cra_font_add_languages (CraApp *app, const FcPattern *pattern)
{
	const gchar *tmp;
	FcResult fc_rc = FcResultMatch;
	FcStrList *list;
	FcStrSet *langs;
	FcValue fc_value;
	guint i;

	for (i = 0; fc_rc == FcResultMatch; i++) {
		fc_rc = FcPatternGet (pattern, FC_LANG, i, &fc_value);
		if (fc_rc == FcResultMatch) {
			langs = FcLangSetGetLangs (fc_value.u.l);
			list = FcStrListCreate (langs);
			FcStrListFirst (list);
			while ((tmp = (const gchar*) FcStrListNext (list)) != NULL)
				cra_app_add_language (app, tmp, "");
			FcStrListDone (list);
			FcStrSetDestroy (langs);
		}
	}
}

/**
 * cra_plugin_process_filename:
 */
static gboolean
cra_plugin_process_filename (CraPlugin *plugin,
			     CraPackage *pkg,
			     const gchar *filename,
			     GList **apps,
			     const gchar *tmpdir,
			     GError **error)
{
	CraApp *app = NULL;
	FcFontSet *fonts;
	FT_Error rc;
	FT_Face ft_face = NULL;
	FT_Library library;
	const gchar *tmp;
	gboolean ret = TRUE;
	gchar *app_id = NULL;
	gchar *comment = NULL;
	gchar *filename_full;
	gchar *icon_filename = NULL;
	GdkPixbuf *pixbuf = NULL;
	const FcPattern *pattern;

	/* load font */
	filename_full = g_build_filename (tmpdir, filename, NULL);
	ret = FcConfigAppFontAddFile (FcConfigGetCurrent(), (FcChar8 *) filename_full);
	if (!ret) {
		g_set_error (error,
			     CRA_PLUGIN_ERROR,
			     CRA_PLUGIN_ERROR_FAILED,
			     "Failed to AddFile %s", filename);
		goto out;
	}
	fonts = FcConfigGetFonts (FcConfigGetCurrent(), FcSetApplication);
	pattern = fonts->fonts[0];
	FT_Init_FreeType (&library);
	rc = FT_New_Face (library, filename_full, 0, &ft_face);
	if (rc != 0) {
		ret = FALSE;
		g_set_error (error,
			     CRA_PLUGIN_ERROR,
			     CRA_PLUGIN_ERROR_FAILED,
			     "FT_Open_Face failed to open %s: %i",
			     filename, rc);
		goto out;
	}

	/* create app that might get merged later */
	app_id = g_path_get_basename (filename);
	app = cra_app_new (pkg, app_id);
	cra_app_set_type_id (app, "font");
	cra_app_add_category (app, "Addons");
	cra_app_add_category (app, "Fonts");
	cra_app_set_requires_appdata (app, TRUE);
	cra_app_set_name (app, "C", ft_face->family_name);
	comment = g_strdup_printf ("A %s font from %s",
				   ft_face->style_name,
				   ft_face->family_name);
	cra_app_set_comment (app, "C", comment);
	cra_font_add_languages (app, pattern);
	cra_font_add_metadata (app, ft_face);
	cra_font_fix_metadata (app);
	cra_font_add_screenshot (app, ft_face);

	/* generate icon */
	tmp = cra_app_get_metadata_item (app, "FontIconText");
	if (tmp != NULL) {
		icon_filename = g_strdup_printf ("%s.png", cra_app_get_id (app));
		cra_app_set_icon (app, icon_filename);
		pixbuf = cra_font_get_pixbuf (ft_face, 64, 64, tmp);
		cra_app_set_icon_type (app, CRA_APP_ICON_TYPE_CACHED);
		cra_app_set_pixbuf (app, pixbuf);
	}

	/* add */
	cra_plugin_add_app (apps, app);
out:
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	if (app != NULL)
		g_object_unref (app);
	FT_Done_Face (ft_face);
	g_free (app_id);
	g_free (comment);
	g_free (filename_full);
	g_free (icon_filename);
	return ret;
}

/**
 * cra_plugin_process:
 */
GList *
cra_plugin_process (CraPlugin *plugin,
		    CraPackage *pkg,
		    const gchar *tmpdir,
		    GError **error)
{
	gboolean ret;
	GList *apps = NULL;
	guint i;
	gchar **filelist;

	filelist = cra_package_get_filelist (pkg);
	for (i = 0; filelist[i] != NULL; i++) {
		if (!_cra_plugin_check_filename (filelist[i]))
			continue;
		ret = cra_plugin_process_filename (plugin,
						   pkg,
						   filelist[i],
						   &apps,
						   tmpdir,
						   error);
		if (!ret) {
			/* FIXME: free apps? */
			apps = NULL;
			goto out;
		}
	}

	/* no fonts files we care about */
	if (apps == NULL) {
		g_set_error (error,
			     CRA_PLUGIN_ERROR,
			     CRA_PLUGIN_ERROR_FAILED,
			     "nothing interesting in %s",
			     cra_package_get_filename (pkg));
		goto out;
	}
out:
	return apps;
}

/**
 * cra_font_get_app_sortable_idx:
 */
static guint
cra_font_get_app_sortable_idx (CraApp *app)
{
	const gchar *font_str = cra_app_get_id (app);
	guint idx = 0;

	if (g_strstr_len (font_str, -1, "It") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Bold") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Semibold") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "ExtraLight") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Lig") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Medium") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Bla") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Hai") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Keyboard") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Kufi") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Tamil") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Hebrew") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Arabic") != NULL)
		idx += 1;
	if (g_strstr_len (font_str, -1, "Fallback") != NULL)
		idx += 1;
	return idx;
}

/**
 * cra_font_merge_family:
 */
static void
cra_font_merge_family (GList **list, const gchar *md_key)
{
	CraApp *app;
	CraApp *found;
	GHashTable *hash;
	GList *hash_values = NULL;
	GList *l;
	GList *list_new = NULL;
	const gchar *tmp;

	hash = g_hash_table_new_full (g_str_hash, g_str_equal,
				      g_free, (GDestroyNotify) g_object_unref);
	for (l = *list; l != NULL; l = l->next) {
		app = CRA_APP (l->data);

		/* no family, or not a font */
		tmp = cra_app_get_metadata_item (app, md_key);
		if (tmp == NULL) {
			cra_plugin_add_app (&list_new, app);
			continue;
		}

		/* find the font family */
		found = g_hash_table_lookup (hash, tmp);
		if (found == NULL) {
			g_hash_table_insert (hash,
					     g_strdup (tmp),
					     g_object_ref (app));
		} else {
			/* app is better than found */
			if (cra_font_get_app_sortable_idx (app) <
			    cra_font_get_app_sortable_idx (found)) {
				g_hash_table_insert (hash,
						     g_strdup (tmp),
						     g_object_ref (app));
				cra_app_subsume (app, found);
			} else {
				cra_app_subsume (found, app);
			}
		}
	}

	/* add all the best fonts to the list */
	hash_values = g_hash_table_get_values (hash);
	for (l = hash_values; l != NULL; l = l->next) {
		app = CRA_APP (l->data);
		cra_plugin_add_app (&list_new, app);
	}
	g_list_free (hash_values);

	/* success */
	g_list_free_full (*list, (GDestroyNotify) g_object_unref);
	*list = list_new;


	g_hash_table_unref (hash);
}

/**
 * cra_plugin_merge:
 */
void
cra_plugin_merge (CraPlugin *plugin, GList **list)
{
	cra_font_merge_family (list, "FontFamily");
	cra_font_merge_family (list, "FontParent");
}