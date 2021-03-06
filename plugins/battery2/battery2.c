#include "batt_sys.h"
#include "misc.h"
#include "../meter/meter.h"
#include "run.h"
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libnotify/notify.h>

//#define DEBUGPRN
#include "dbg.h"

static meter_class *k;

typedef struct {
    meter_priv meter;
    int timer;
    gboolean charging;
    int show_icon_in_ac;
    int low_limit;
    int low_limit_notify;
    int type_shutdown;
    gboolean notify_sended;
    battery *b;
} battery_priv;

static gchar *commands_shutdown[] = {
    "gksudo shutdown -h now",
    "pm-is-supported --suspend && gksudo pm-suspend",
    "pm-is-supported --hibernate && gksudo pm-hibernate",
    "pm-is-supported --suspend-hybrid && gksudo pm-suspend-hybrid",
};

static gchar *batt_working[] = {
    "battery_0",
    "battery_1",
    "battery_2",
    "battery_3",
    "battery_4",
    "battery_5",
    "battery_6",
    "battery_7",
    "battery_8",
    NULL
};

static gchar *batt_charging[] = {
    "battery_charging_0",
    "battery_charging_1",
    "battery_charging_2",
    "battery_charging_3",
    "battery_charging_4",
    "battery_charging_5",
    "battery_charging_6",
    "battery_charging_7",
    "battery_charging_8",
    NULL
};

static gchar *batt_na[] = {
    "battery_na",
    NULL
};

static void
battery_update_os(battery_priv *c)
{
	if (c->b == NULL)
		return;
	battery_update(c->b);
	c->charging = battery_is_charging(c->b);
}

static gboolean
send_notify(const gchar *message)
{
    NotifyNotification *n;  
    n = notify_notification_new ("Alert", message, NULL);
    notify_notification_set_timeout (n, 5000); // 5 seconds
    if (!notify_notification_show (n, NULL)) 
    {
        DBG("failed to send notification\n");
        RET(FALSE);
    }
    g_object_unref(G_OBJECT(n));
    RET(TRUE);
}

static gboolean
battery_callback(battery_priv *c)
{
    gchar buf[60];
    gchar **i;

    ENTER;
    battery_update_os(c);
    if (c->b) {
        if (c->charging) {
            i = batt_charging;
            if (c->b->percentage == 100)
				g_snprintf(buf, sizeof(buf), _("<b>Battery:</b> %d%%"), c->b->percentage);
            else
                g_snprintf(buf, sizeof(buf), _("<b>Battery:</b> %d%%\n%s %d min."), c->b->percentage, _("Be left to full\ncharge:"), c->b->seconds / 60);
        }
        else {
            i = batt_working;
            g_snprintf(buf, sizeof(buf), _("<b>Battery:</b> %d%%\n%s %d min."), c->b->percentage, _("Be left to full\ndischarge:"), ((int)c->b->seconds / 60));
        }
        gtk_widget_set_tooltip_markup(((plugin_instance *)c)->pwid, buf);
        k->set_icons(&c->meter, i);
        k->set_level(&c->meter, c->b->percentage);
        if ((c->b->percentage <= c->low_limit_notify) && !c->notify_sended && !c->charging) {
            send_notify(g_strdup_printf(_("Battery running low. The system will be \n terminated in %d minutes"), c->b->seconds / 60));
            c->notify_sended = TRUE;
        }
        if ((c->b->percentage <= c->low_limit) && !c->charging) {
            DBG("battery: %s\n", commands_shutdown[c->type_shutdown]);
            run_app(commands_shutdown[c->type_shutdown]);
        }
    } else {
        if (c->show_icon_in_ac) {
            i = batt_na;
            gtk_widget_set_tooltip_markup(((plugin_instance *)c)->pwid, _("Running on AC\nNo battery found"));
            k->set_icons(&c->meter, i);
            k->set_level(&c->meter, c->b->percentage);
        }
        else
            RET(FALSE);
    }

    RET(TRUE);
}


static int
battery_constructor(plugin_instance *p)
{
    battery_priv *c;

    ENTER;
    if (!(k = class_get("meter")))
        RET(0);
    if (!PLUGIN_CLASS(k)->constructor(p))
        RET(0);
    notify_init("battery");
    c = (battery_priv *) p;
    c->show_icon_in_ac = 0;
    c->low_limit = 2;
    c->low_limit_notify = 5;
    c->notify_sended = FALSE;
    c->type_shutdown = 0;
    c->b = battery_get();
    XCG(p->xc, "showiconinac", &c->show_icon_in_ac, enum, bool_enum);
    XCG(p->xc, "lowlimit", &c->low_limit, int);
    XCG(p->xc, "lowlimitnotify", &c->low_limit_notify, int);
    XCG(p->xc, "typeshutdown", &c->type_shutdown, int);
    if (c->low_limit_notify <= c->low_limit) {
        c->low_limit_notify = c->low_limit + 3;
    }
    DBG("ShowIconInAC=%s, LowLimit=%d, LowLimitNotify=%d, TypeShutdown=%s\n", (c->show_icon_in_ac) ? "True" : "False", c->low_limit, c->low_limit_notify, commands_shutdown[c->type_shutdown]);
    c->timer = g_timeout_add(2000, (GSourceFunc) battery_callback, c);
    battery_callback(c);
    RET(1);
}

static void
battery_destructor(plugin_instance *p)
{
    battery_priv *c = (battery_priv *) p;

    ENTER;
    if (c->timer)
        g_source_remove(c->timer);
    PLUGIN_CLASS(k)->destructor(p);
    class_put("meter");
    RET();
}

static plugin_class class = {
    .count       = 0,
    .type        = "battery2",
    .name        = "battery usage",
    .version     = "1.0",
    .description = "Display battery usage",
    .priv_size   = sizeof(battery_priv),
    .constructor = battery_constructor,
    .destructor  = battery_destructor,
};

static plugin_class *class_ptr = (plugin_class *) &class;
