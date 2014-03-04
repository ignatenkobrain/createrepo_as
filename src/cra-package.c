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

#include "config.h"

#include <limits.h>
#include <archive.h>
#include <archive_entry.h>

#include <rpm/rpmlib.h>
#include <rpm/rpmts.h>

#include "cra-package.h"
#include "cra-plugin.h"

typedef struct _CraPackagePrivate	CraPackagePrivate;
struct _CraPackagePrivate
{
	Header		 h;
	gchar		**filelist;
	gchar		*filename;
	gchar		*name;
	guint		 epoch;
	gchar		*version;
	gchar		*release;
	gchar		*arch;
	gchar		*url;
};

G_DEFINE_TYPE_WITH_PRIVATE (CraPackage, cra_package, G_TYPE_OBJECT)

#define GET_PRIVATE(o) (cra_package_get_instance_private (o))

/**
 * cra_package_finalize:
 **/
static void
cra_package_finalize (GObject *object)
{
	CraPackage *pkg = CRA_PACKAGE (object);
	CraPackagePrivate *priv = GET_PRIVATE (pkg);

	headerFree (priv->h);
	g_strfreev (priv->filelist);
	g_free (priv->filename);
	g_free (priv->name);
	g_free (priv->version);
	g_free (priv->release);
	g_free (priv->arch);
	g_free (priv->url);

	G_OBJECT_CLASS (cra_package_parent_class)->finalize (object);
}

/**
 * cra_package_init:
 **/
static void
cra_package_init (CraPackage *pkg)
{
	CraPackagePrivate *priv = GET_PRIVATE (pkg);
	priv->h = NULL;
}

/**
 * cra_package_get_filename:
 **/
const gchar *
cra_package_get_filename (CraPackage *pkg)
{
	CraPackagePrivate *priv = GET_PRIVATE (pkg);
	return priv->filename;
}

/**
 * cra_package_get_name:
 **/
const gchar *
cra_package_get_name (CraPackage *pkg)
{
	CraPackagePrivate *priv = GET_PRIVATE (pkg);
	return priv->name;
}

/**
 * cra_package_get_url:
 **/
const gchar *
cra_package_get_url (CraPackage *pkg)
{
	CraPackagePrivate *priv = GET_PRIVATE (pkg);
	return priv->url;
}

/**
 * cra_package_get_filelist:
 **/
gchar **
cra_package_get_filelist (CraPackage *pkg)
{
	CraPackagePrivate *priv = GET_PRIVATE (pkg);
	return priv->filelist;
}

/**
 * cra_package_class_init:
 **/
static void
cra_package_class_init (CraPackageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = cra_package_finalize;
}

/**
 * cra_package_ensure_filelist:
 **/
gboolean
cra_package_ensure_filelist (CraPackage *pkg, GError **error)
{
	CraPackagePrivate *priv = GET_PRIVATE (pkg);
	const gchar **dirnames = NULL;
	gboolean ret = TRUE;
	gint32 *dirindex = NULL;
	gint rc;
	guint i;
	rpmtd td[3];

	if (priv->filelist != NULL)
		goto out;

	/* read out the file list */
	for (i = 0; i < 3; i++)
		td[i] = rpmtdNew ();
	rc = headerGet (priv->h, RPMTAG_DIRNAMES, td[0], HEADERGET_MINMEM);
	if (rc)
		rc = headerGet (priv->h, RPMTAG_BASENAMES, td[1], HEADERGET_MINMEM);
	if (rc)
		rc = headerGet (priv->h, RPMTAG_DIRINDEXES, td[2], HEADERGET_MINMEM);
	if (!rc) {
		ret = FALSE;
		g_set_error (error,
			     CRA_PLUGIN_ERROR,
			     CRA_PLUGIN_ERROR_FAILED,
			     "Failed to read package file list %s",
			     priv->filename);
		goto out;
	}
	i = 0;
	dirnames = g_new0 (const gchar *, rpmtdCount (td[0]) + 1);
	while (rpmtdNext (td[0]) != -1)
		dirnames[i++] = rpmtdGetString (td[0]);
	i = 0;
	dirindex = g_new0 (gint32, rpmtdCount (td[2]) + 1);
	while (rpmtdNext (td[2]) != -1)
		dirindex[i++] = rpmtdGetNumber (td[2]);
	i = 0;
	priv->filelist = g_new0 (gchar *, rpmtdCount (td[1]) + 1);
	while (rpmtdNext (td[1]) != -1) {
		priv->filelist[i] = g_build_filename (dirnames[dirindex[i]],
						     rpmtdGetString (td[1]),
						     NULL);
		i++;
	}
out:
	g_free (dirindex);
	g_free (dirnames);
	return ret;
}

/**
 * cra_package_open:
 **/
gboolean
cra_package_open (CraPackage *pkg, const gchar *filename, GError **error)
{
	CraPackagePrivate *priv = GET_PRIVATE (pkg);
	FD_t fd;
	gboolean ret = TRUE;
	gint rc;
	rpmtd td;
	rpmts ts;

	/* open the file */
	ts = rpmtsCreate ();
	fd = Fopen (filename, "r");
	if (fd <= 0) {
		ret = FALSE;
		g_set_error (error,
			     CRA_PLUGIN_ERROR,
			     CRA_PLUGIN_ERROR_FAILED,
			     "Failed to open package %s", filename);
		goto out;
	}

	/* create package */
	priv->filename = g_strdup (filename);
	rc = rpmReadPackageFile (ts, fd, filename, &priv->h);
	if (rc != RPMRC_OK) {
		ret = FALSE;
		g_set_error (error,
			     CRA_PLUGIN_ERROR,
			     CRA_PLUGIN_ERROR_FAILED,
			     "Failed to read package %s", filename);
		goto out;
	}


	/* get the simple stuff */
	td = rpmtdNew ();
	headerGet (priv->h, RPMTAG_NAME, td, HEADERGET_MINMEM);
	priv->name = g_strdup (rpmtdGetString (td));
	headerGet (priv->h, RPMTAG_VERSION, td, HEADERGET_MINMEM);
	priv->version = g_strdup (rpmtdGetString (td));
	headerGet (priv->h, RPMTAG_RELEASE, td, HEADERGET_MINMEM);
	priv->release = g_strdup (rpmtdGetString (td));
	headerGet (priv->h, RPMTAG_ARCH, td, HEADERGET_MINMEM);
	priv->arch = g_strdup (rpmtdGetString (td));
	headerGet (priv->h, RPMTAG_EPOCH, td, HEADERGET_MINMEM);
	priv->epoch = rpmtdGetNumber (td);
	headerGet (priv->h, RPMTAG_URL, td, HEADERGET_MINMEM);
	priv->url = g_strdup (rpmtdGetString (td));
out:
	rpmtsFree (ts);
	Fclose (fd);
	return ret;
}

/**
 * cra_package_explode:
 **/
gboolean
cra_package_explode (CraPackage *pkg, const gchar *dir, GError **error)
{
	CraPackagePrivate *priv = GET_PRIVATE (pkg);
	const gchar *tmp;
	gboolean ret = TRUE;
	gchar buf[PATH_MAX];
	gchar *data = NULL;
	gsize len;
	int r;
	struct archive *arch = NULL;
	struct archive_entry *entry;

	/* load file at once to avoid seeking */
	ret = g_file_get_contents (priv->filename, &data, &len, error);
	if (!ret)
		goto out;

	/* read anything */
	arch = archive_read_new ();
	archive_read_support_format_all (arch);
	archive_read_support_filter_all (arch);
	r = archive_read_open_memory (arch, data, len);
	if (r) {
		ret = FALSE;
		g_set_error (error,
			     CRA_PLUGIN_ERROR,
			     CRA_PLUGIN_ERROR_FAILED,
			     "Cannot open: %s",
			     archive_error_string (arch));
		goto out;
	}

	/* decompress each file */
	for (;;) {
		r = archive_read_next_header (arch, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			ret = FALSE;
			g_set_error (error,
				     CRA_PLUGIN_ERROR,
				     CRA_PLUGIN_ERROR_FAILED,
				     "Cannot read header: %s",
				     archive_error_string (arch));
			goto out;
		}

		/* update output path */
		g_snprintf (buf, PATH_MAX, "%s/%s",
			    dir, archive_entry_pathname (entry));
		archive_entry_update_pathname_utf8 (entry, buf);

		/* update hardlinks */
		tmp = archive_entry_hardlink (entry);
		if (tmp != NULL) {
			g_snprintf (buf, PATH_MAX, "%s/%s", dir, tmp);
			archive_entry_update_hardlink_utf8 (entry, buf);
		}

		/* update symlinks */
		tmp = archive_entry_symlink (entry);
		if (tmp != NULL) {
			g_snprintf (buf, PATH_MAX, "%s/%s", dir, tmp);
			archive_entry_update_symlink_utf8 (entry, buf);
		}

		/* no output file */
		if (archive_entry_pathname (entry) == NULL)
			continue;

		r = archive_read_extract (arch, entry, 0);
		if (r != ARCHIVE_OK) {
			ret = FALSE;
			g_set_error (error,
				     CRA_PLUGIN_ERROR,
				     CRA_PLUGIN_ERROR_FAILED,
				     "Cannot extract: %s",
				     archive_error_string (arch));
			goto out;
		}
	}
out:
	g_free (data);
	if (arch != NULL) {
		archive_read_close (arch);
		archive_read_free (arch);
	}
	return ret;
}

/**
 * cra_package_new:
 **/
CraPackage *
cra_package_new (void)
{
	CraPackage *pkg;
	pkg = g_object_new (CRA_TYPE_PACKAGE, NULL);
	return CRA_PACKAGE (pkg);
}