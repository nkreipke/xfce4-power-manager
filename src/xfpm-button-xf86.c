/*
 * * Copyright (C) 2008-2009 Ali <aliov@xfce.org>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


/*
 * Based on code from gpm-button (gnome power manager)
 * Copyright (C) 2006-2007 Richard Hughes <richard@hughsie.com>
 * 
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/XF86keysym.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <glib.h>

#include <libxfce4util/libxfce4util.h>

#include "xfpm-button-xf86.h"
#include "xfpm-enum.h"
#include "xfpm-enum-types.h"

/* Init */
static void xfpm_button_xf86_class_init (XfpmButtonXf86Class *klass);
static void xfpm_button_xf86_init       (XfpmButtonXf86 *button);
static void xfpm_button_xf86_finalize   (GObject *object);

static gpointer xfpm_button_xf86_object = NULL;

#define XFPM_BUTTON_XF86_GET_PRIVATE(o) \
(G_TYPE_INSTANCE_GET_PRIVATE((o), XFPM_TYPE_BUTTON_XF86, XfpmButtonXf86Private))

struct XfpmButtonXf86Private
{
    GdkScreen	*screen;
    GdkWindow   *window;
    GHashTable  *hash;
    
    guint8       mapped_buttons;
};

enum
{
    XF86_BUTTON_PRESSED,
    LAST_SIGNAL
};

#define DUPLICATE_SHUTDOWN_TIMEOUT 4.0f

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(XfpmButtonXf86, xfpm_button_xf86, G_TYPE_OBJECT)

static GdkFilterReturn
xfpm_button_xf86_filter_x_events (GdkXEvent *xevent, GdkEvent *ev, gpointer data)
{
    XEvent *xev = (XEvent *) xevent;
    
    if ( xev->type != KeyPress )
    	return GDK_FILTER_CONTINUE;
    
    XfpmButtonXf86 *button = (XfpmButtonXf86 *) data;
    
    gpointer key_hash = g_hash_table_lookup (button->priv->hash, GINT_TO_POINTER(xev->xkey.keycode));
    
    if ( !key_hash )
    {
	TRACE("Key %d not found in hash\n", xev->xkey.keycode);
	return GDK_FILTER_CONTINUE;
    }
    
    XfpmButtonKey type = GPOINTER_TO_INT (key_hash);
    
    TRACE("Found key in hash %d", type);
    
    g_signal_emit (G_OBJECT(button), signals[XF86_BUTTON_PRESSED], 0, type);

    return GDK_FILTER_REMOVE;
}

static gboolean
xfpm_button_xf86_grab_keystring (XfpmButtonXf86 *button, guint keycode)
{
    Display *display;
    guint ret;
    guint modmask = 0;
    
    display = GDK_DISPLAY ();
    
    gdk_error_trap_push ();

    ret = XGrabKey (display, keycode, modmask,
		    GDK_WINDOW_XID (button->priv->window), True,
		    GrabModeAsync, GrabModeAsync);
		    
    if ( ret == BadAccess )
    {
	g_warning ("Failed to grab modmask=%u, keycode=%li",
		    modmask, (long int) keycode);
	return FALSE;
    }
	
    ret = XGrabKey (display, keycode, LockMask | modmask,
		    GDK_WINDOW_XID (button->priv->window), True,
		    GrabModeAsync, GrabModeAsync);
			
    if (ret == BadAccess)
    {
	g_warning ("Failed to grab modmask=%u, keycode=%li",
		   LockMask | modmask, (long int) keycode);
	return FALSE;
    }

    gdk_flush ();
    gdk_error_trap_pop ();
    return TRUE;
}


static gboolean
xfpm_button_xf86_xevent_key (XfpmButtonXf86 *button, guint keysym , XfpmButtonKey type)
{
    guint keycode = XKeysymToKeycode (GDK_DISPLAY(), keysym);

    if ( keycode == 0 )
    {
	g_critical ("could not map keysym %x to keycode\n", keysym);
	return FALSE;
    }
    
    if ( !xfpm_button_xf86_grab_keystring(button, keycode)) 
    {
    	g_critical ("Failed to grab %i\n", keycode);
	return FALSE;
    }
    
#ifdef DEBUG
    gchar *content;
    GValue value = { 0, };
    g_value_init (&value, XFPM_TYPE_BUTTON_KEY);
    g_value_set_enum (&value, type);
    content = g_strdup_value_contents (&value);
    TRACE("Grabbed key=%s, keycode=%li", content, (long int) keycode);
    g_free (content);
#endif /*DEBUG */

    g_hash_table_insert (button->priv->hash, GINT_TO_POINTER(keycode), GINT_TO_POINTER(type));
    
    return TRUE;
}

static void
xfpm_button_xf86_setup (XfpmButtonXf86 *button)
{
    button->priv->screen = gdk_screen_get_default ();
    button->priv->window = gdk_screen_get_root_window (button->priv->screen);
    
    if ( xfpm_button_xf86_xevent_key (button, XF86XK_PowerOff, BUTTON_POWER_OFF) )
	button->priv->mapped_buttons |= POWER_KEY;
    
    if ( xfpm_button_xf86_xevent_key (button, XF86XK_Sleep, BUTTON_SLEEP) )
	button->priv->mapped_buttons |= SLEEP_KEY;
	
    if ( xfpm_button_xf86_xevent_key (button, XF86XK_MonBrightnessUp, BUTTON_MON_BRIGHTNESS_UP) &&
	 xfpm_button_xf86_xevent_key (button, XF86XK_MonBrightnessDown, BUTTON_MON_BRIGHTNESS_DOWN) )
	button->priv->mapped_buttons |= BRIGHTNESS_KEY;

    gdk_window_add_filter (button->priv->window, 
			   xfpm_button_xf86_filter_x_events, button);
}

static void
xfpm_button_xf86_class_init(XfpmButtonXf86Class *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    signals[XF86_BUTTON_PRESSED] = 
        g_signal_new("xf86-button-pressed",
                      XFPM_TYPE_BUTTON_XF86,
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(XfpmButtonXf86Class, xf86_button_pressed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__ENUM,
                      G_TYPE_NONE, 1, XFPM_TYPE_BUTTON_KEY);

    object_class->finalize = xfpm_button_xf86_finalize;

    g_type_class_add_private(klass,sizeof(XfpmButtonXf86Private));
}

static void
xfpm_button_xf86_init(XfpmButtonXf86 *button)
{
    button->priv = XFPM_BUTTON_XF86_GET_PRIVATE(button);
    
    button->priv->mapped_buttons = 0;
    button->priv->screen = NULL;
    button->priv->window = NULL;
    
    button->priv->hash = g_hash_table_new (NULL, NULL);
    
    xfpm_button_xf86_setup (button);
}

static void
xfpm_button_xf86_finalize(GObject *object)
{
    XfpmButtonXf86 *button;

    button = XFPM_BUTTON_XF86 (object);
    
    g_hash_table_destroy (button->priv->hash);

    G_OBJECT_CLASS(xfpm_button_xf86_parent_class)->finalize(object);
}

XfpmButtonXf86 *
xfpm_button_xf86_new(void)
{
    if ( xfpm_button_xf86_object != NULL )
    {
	g_object_ref (xfpm_button_xf86_object);
    }
    else
    {
	xfpm_button_xf86_object = g_object_new (XFPM_TYPE_BUTTON_XF86, NULL);
	g_object_add_weak_pointer (xfpm_button_xf86_object, &xfpm_button_xf86_object);
    }
    return XFPM_BUTTON_XF86 (xfpm_button_xf86_object);
}

guint8 xfpm_button_xf86_get_mapped_buttons (XfpmButtonXf86 *button)
{
    g_return_val_if_fail (XFPM_IS_BUTTON_XF86 (button), 0);
    
    return button->priv->mapped_buttons;
}
