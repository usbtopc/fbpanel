/*
 * 
 * 
 */

#include "misc.h"
#include "../meter/meter.h"
#include "run.h"
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <alsa/asoundlib.h>


//#define DEBUGPRN
#include "dbg.h"

static gchar *names[] = {
    "stock_volume-min",
    "stock_volume-med",
    "stock_volume-max",
    NULL
};

static gchar *s_names[] = {
    "stock_volume-mute",
    NULL
};
  
typedef struct {
    meter_priv meter;
    snd_mixer_t* handle;
    snd_mixer_elem_t* elem;
    snd_mixer_selem_id_t* sid;
    gchar *mixer;
    guchar vol, muted_vol;
    int update_id, leave_id;
    int has_pointer;
    gboolean muted;
    GtkWidget *slider_window;
    GtkWidget *slider;
} volume_priv;

static meter_class *k;

static void slider_changed(GtkRange *range, volume_priv *c);
static gboolean crossed(GtkWidget *widget, GdkEventCrossing *event,
    volume_priv *c);

static gboolean asound_find_element(volume_priv * vol, const char * ename)
{
    for (
      vol->elem = snd_mixer_first_elem(vol->handle);
      vol->elem != NULL;
      vol->elem = snd_mixer_elem_next(vol->elem))
    {
        snd_mixer_selem_get_id(vol->elem, vol->sid);
        if ((snd_mixer_selem_is_active(vol->elem))
        && (strcmp(ename, snd_mixer_selem_id_get_name(vol->sid)) == 0))
            return TRUE;
    }
    return FALSE;
}

/* Initialize the ALSA interface. */
static gboolean asound_initialize(volume_priv * vol)
{
    /* Access the "default" device. */
    snd_mixer_selem_id_alloca(&vol->sid);
    snd_mixer_open(&vol->handle, 0);
    snd_mixer_attach(vol->handle, "default");
    snd_mixer_selem_register(vol->handle, NULL, NULL);
    snd_mixer_load(vol->handle);

    /* Find Master element, or Front element, or PCM element, or LineOut element.
     * If one of these succeeds, master_element is valid. */
    if ( ! asound_find_element(vol, "Master"))
        if ( ! asound_find_element(vol, "Front"))
            if ( ! asound_find_element(vol, "PCM"))
                if ( ! asound_find_element(vol, "LineOut"))
                    return FALSE;

    /* Set the playback volume range as we wish it. */
    snd_mixer_selem_set_playback_volume_range(vol->elem, 0, 100);
    return TRUE;
}

static int
get_volume(volume_priv *c)
{
    long volume;
    ENTER;
    if (snd_mixer_selem_get_playback_volume(c->elem, 0, &volume) < 0) {
        DBG("volume2: can't get volume\n");
        RET(0);
    }
    DBG("volume=%d\n", (int) volume);
    RET(volume);
}

static void
set_volume(volume_priv *c, long volume)
{
    ENTER;
    DBG("volume=%d\n", (int) volume);
    if (snd_mixer_selem_set_playback_volume_all(c->elem, volume) < 0) {
        DBG("volume2: don't set volume\n");
    }
}

static gboolean
volume_update_gui(volume_priv *c)
{
    int volume;
    gchar buf[20];

    ENTER;
    volume = get_volume(c);
    if ((volume != 0) != (c->vol != 0)) {
        if (volume)
            k->set_icons(&c->meter, names);
        else
            k->set_icons(&c->meter, s_names);
        DBG("seting %s icons\n", volume ? "normal" : "muted");
    }
    c->vol = volume;
    k->set_level(&c->meter, volume);
    g_snprintf(buf, sizeof(buf), "<b>Volume:</b> %d%%", volume);
    if (!c->slider_window)
        gtk_widget_set_tooltip_markup(((plugin_instance *)c)->pwid, buf);
    else {
        g_signal_handlers_block_by_func(G_OBJECT(c->slider),
            G_CALLBACK(slider_changed), c);
        gtk_range_set_value(GTK_RANGE(c->slider), volume);
        g_signal_handlers_unblock_by_func(G_OBJECT(c->slider),
            G_CALLBACK(slider_changed), c);
    }
    RET(TRUE);
}

static void
slider_changed(GtkRange *range, volume_priv *c)
{
    int volume = (int) gtk_range_get_value(range);
    ENTER;
    DBG("value=%d\n", volume);
    set_volume(c, volume);
    volume_update_gui(c);
    RET();
}

static GtkWidget *
volume_create_slider(volume_priv *c)
{
    GtkWidget *slider, *win;
    GtkWidget *frame;
    
    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(win), 180, 180);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(win), 1);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_MOUSE);
    gtk_window_stick(GTK_WINDOW(win));

    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(win), frame);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 1);
    
    slider = gtk_vscale_new_with_range(0.0, 100.0, 1.0);
    gtk_widget_set_size_request(slider, 25, 82);
    gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(slider), GTK_POS_BOTTOM);
    gtk_scale_set_digits(GTK_SCALE(slider), 0);
    gtk_range_set_inverted(GTK_RANGE(slider), TRUE);
    gtk_range_set_value(GTK_RANGE(slider), ((meter_priv *) c)->level);
    DBG("meter->level %f\n", ((meter_priv *) c)->level);
    g_signal_connect(G_OBJECT(slider), "value_changed",
        G_CALLBACK(slider_changed), c);
    g_signal_connect(G_OBJECT(slider), "enter-notify-event",
        G_CALLBACK(crossed), (gpointer)c);
    g_signal_connect(G_OBJECT(slider), "leave-notify-event",
        G_CALLBACK(crossed), (gpointer)c);
    gtk_container_add(GTK_CONTAINER(frame), slider);
    
    c->slider = slider;
    return win;
}  

static gboolean
icon_clicked(GtkWidget *widget, GdkEventButton *event, volume_priv *c)
{
    int volume;

    ENTER;
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        if (c->slider_window == NULL) {
            c->slider_window = volume_create_slider(c);
            gtk_widget_show_all(c->slider_window);
            gtk_widget_set_tooltip_markup(((plugin_instance *)c)->pwid, NULL);
        } else {
            gtk_widget_destroy(c->slider_window);
            c->slider_window = NULL;
            if (c->leave_id) {
                g_source_remove(c->leave_id);
                c->leave_id = 0;
            }
        }
        RET(FALSE);
    }
    if (!(event->type == GDK_BUTTON_PRESS && event->button == 2)) {
        run_app(c->mixer);
        RET(FALSE);
    }
    if (c->muted) {
        volume = c->muted_vol;
    } else {
        c->muted_vol = c->vol;
        volume = 0;
    }
    c->muted = !c->muted;
    set_volume(c, volume);
    volume_update_gui(c);
    RET(FALSE);
}

static gboolean
icon_scrolled(GtkWidget *widget, GdkEventScroll *event, volume_priv *c)
{
    int volume;
    
    ENTER;
    volume = (c->muted) ? c->muted_vol : ((meter_priv *) c)->level;
    volume += 2 * ((event->direction == GDK_SCROLL_UP
            || event->direction == GDK_SCROLL_LEFT) ? 1 : -1);
    if (volume > 100)
        volume = 100;
    if (volume < 0)
        volume = 0;
    
    if (c->muted) 
        c->muted_vol = volume;
    else {
        set_volume(c, volume);
        volume_update_gui(c);
    }
    RET(TRUE);
}

static gboolean
leave_cb(volume_priv *c)
{
    ENTER;
    c->leave_id = 0;
    c->has_pointer = 0;
    gtk_widget_destroy(c->slider_window);
    c->slider_window = NULL;
    RET(FALSE);
}

static gboolean
crossed(GtkWidget *widget, GdkEventCrossing *event, volume_priv *c)
{
    ENTER;
    if (event->type == GDK_ENTER_NOTIFY)
        c->has_pointer++;
    else
        c->has_pointer--;
    if (c->has_pointer > 0) {
        if (c->leave_id) {
            g_source_remove(c->leave_id);
            c->leave_id = 0;
        }
    } else {
        if (!c->leave_id && c->slider_window) {
            c->leave_id = g_timeout_add(1200, (GSourceFunc) leave_cb, c);
        }
    }
    DBG("has_pointer=%d\n", c->has_pointer);
    RET(FALSE);
}

static int
volume_constructor(plugin_instance *p)
{
    volume_priv *c;
    
    if (!(k = class_get("meter")))
        RET(0);
    if (!PLUGIN_CLASS(k)->constructor(p))
        RET(0);
    c = (volume_priv *) p;
    XCG(p->xc, "mixer", &c->mixer, str);
    DBG("mixer=%s\n", c->mixer);
    if (! asound_initialize(c)) {
        ERR("volume2:Not initialize sound card\n");
        RET(0);
    }
    k->set_icons(&c->meter, names);
    c->update_id = g_timeout_add(1000, (GSourceFunc) volume_update_gui, c);
    c->vol = 200;
    volume_update_gui(c);
    g_signal_connect(G_OBJECT(p->pwid), "scroll-event",
        G_CALLBACK(icon_scrolled), (gpointer) c);
    g_signal_connect(G_OBJECT(p->pwid), "button_press_event",
        G_CALLBACK(icon_clicked), (gpointer)c);
    g_signal_connect(G_OBJECT(p->pwid), "enter-notify-event",
        G_CALLBACK(crossed), (gpointer)c);
    g_signal_connect(G_OBJECT(p->pwid), "leave-notify-event",
        G_CALLBACK(crossed), (gpointer)c);

    RET(1);
}

static void
volume_destructor(plugin_instance *p)
{
    volume_priv *c = (volume_priv *) p;
    
    ENTER;
    g_source_remove(c->update_id);
    if (c->slider_window)
        gtk_widget_destroy(c->slider_window);
    PLUGIN_CLASS(k)->destructor(p);
    class_put("meter");
    snd_mixer_close(c->handle);
    RET();
}



static plugin_class class = {
    .count       = 0,
    .type        = "volume2",
    .name        = "Volume2",
    .version     = "2.0",
    .description = "ALSA volume control",
    .priv_size   = sizeof(volume_priv),
    .constructor = volume_constructor,
    .destructor  = volume_destructor,
};

static plugin_class *class_ptr = (plugin_class *) &class;
