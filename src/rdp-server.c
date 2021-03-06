/*
 * Copyright © 2012 Canonical Ltd.
 * Copyright © 2015 The Arctica Project
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Ted Gould <ted@canonical.com>
 *          Mike Gabriel <mike.gabriel@das-netzwerkteam.de>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/mman.h>

#include <glib/gi18n.h>

#include <string.h>

#include "rdp-server.h"
#include "defines.h"

static void rdp_server_class_init (RdpServerClass *klass);
static void rdp_server_init       (RdpServer *self);
static void rdp_server_dispose    (GObject *object);
static void rdp_server_finalize   (GObject *object);
static GVariant *  get_properties        (Server * server);

G_DEFINE_TYPE (RdpServer, rdp_server, SERVER_TYPE);

static void
rdp_server_class_init (RdpServerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rdp_server_dispose;
	object_class->finalize = rdp_server_finalize;

	ServerClass * server_class = SERVER_CLASS(klass);

	server_class->get_properties = get_properties;

	return;
}

static void
rdp_server_init (RdpServer *self)
{
	self->username = NULL;
	self->password = NULL;
	self->domain = NULL;
	self->domain_required = FALSE;

	return;
}

static void
rdp_server_dispose (GObject *object)
{

	G_OBJECT_CLASS (rdp_server_parent_class)->dispose (object);
	return;
}

/* Unlocks the memory before freeing */
static void
password_clear (gpointer data)
{
	char * pass = (char *)data;
	munlock(pass, strlen(pass));
	g_free(pass);
	return;
}

static void
rdp_server_finalize (GObject *object)
{
	RdpServer * server = RDP_SERVER(object);

	g_clear_pointer(&server->username, g_free);
	g_clear_pointer(&server->password, password_clear);
	g_clear_pointer(&server->domain, g_free);

	G_OBJECT_CLASS (rdp_server_parent_class)->finalize (object);
	return;
}

static GVariant *
get_properties (Server * server)
{
	RdpServer * rserver = RDP_SERVER(server);

	GVariantBuilder propbuilder;
	g_variant_builder_init(&propbuilder, G_VARIANT_TYPE_ARRAY);

	GVariantBuilder namebuilder;
	g_variant_builder_init(&namebuilder, G_VARIANT_TYPE_TUPLE);
	g_variant_builder_add_value(&namebuilder, g_variant_new_string("username"));
	g_variant_builder_add_value(&namebuilder, g_variant_new_boolean(TRUE));
	if (rserver->username == NULL) {
		g_variant_builder_add_value(&namebuilder, g_variant_new_variant(g_variant_new_string("")));
	} else {
		g_variant_builder_add_value(&namebuilder, g_variant_new_variant(g_variant_new_string(rserver->username)));
	}
	g_variant_builder_add_value(&namebuilder, g_variant_parse(G_VARIANT_TYPE_VARDICT, "{}", NULL, NULL, NULL));
	g_variant_builder_add_value(&propbuilder, g_variant_builder_end(&namebuilder));

	GVariantBuilder passbuilder;
	g_variant_builder_init(&passbuilder, G_VARIANT_TYPE_TUPLE);
	g_variant_builder_add_value(&passbuilder, g_variant_new_string("password"));
	g_variant_builder_add_value(&passbuilder, g_variant_new_boolean(TRUE));
	if (rserver->password == NULL) {
		g_variant_builder_add_value(&passbuilder, g_variant_new_variant(g_variant_new_string("")));
	} else {
		g_variant_builder_add_value(&passbuilder, g_variant_new_variant(g_variant_new_string(rserver->password)));
	}
	g_variant_builder_add_value(&passbuilder, g_variant_parse(G_VARIANT_TYPE_VARDICT, "{}", NULL, NULL, NULL));
	g_variant_builder_add_value(&propbuilder, g_variant_builder_end(&passbuilder));

	GVariantBuilder domainbuilder;
	g_variant_builder_init(&domainbuilder, G_VARIANT_TYPE_TUPLE);
	g_variant_builder_add_value(&domainbuilder, g_variant_new_string("domain"));
	g_variant_builder_add_value(&domainbuilder, g_variant_new_boolean(rserver->domain_required));
	if (rserver->domain == NULL) {
		g_variant_builder_add_value(&domainbuilder, g_variant_new_variant(g_variant_new_string("")));
	} else {
		g_variant_builder_add_value(&domainbuilder, g_variant_new_variant(g_variant_new_string(rserver->domain)));
	}
	g_variant_builder_add_value(&domainbuilder, g_variant_parse(G_VARIANT_TYPE_VARDICT, "{}", NULL, NULL, NULL));
	g_variant_builder_add_value(&propbuilder, g_variant_builder_end(&domainbuilder));

	return g_variant_builder_end(&propbuilder);
}

Server *
rdp_server_new_from_keyfile (GKeyFile * keyfile, const gchar * groupname)
{
	g_return_val_if_fail(keyfile != NULL, NULL); /* NOTE: No way to check if that's really a keyfile :-( */
	g_return_val_if_fail(groupname != NULL, NULL);

	if (!g_key_file_has_group(keyfile, groupname)) {
		g_warning("Server specified but group '%s' was not found", groupname);
		return NULL;
	}

	RdpServer * server = g_object_new(RDP_SERVER_TYPE, NULL);

	if (g_key_file_has_key(keyfile, groupname, CONFIG_SERVER_NAME, NULL)) {
		gchar * keyname = g_key_file_get_string(keyfile, groupname, CONFIG_SERVER_NAME, NULL);
		server->parent.name = g_strdup(_(keyname));
		g_free(keyname);
	}

	if (g_key_file_has_key(keyfile, groupname, CONFIG_SERVER_URI, NULL)) {
		server->parent.uri = g_key_file_get_string(keyfile, groupname, CONFIG_SERVER_URI, NULL);
	}

	return SERVER(server);
}

/* Build the RDP server from information in the JSON object */
Server *
rdp_server_new_from_json (JsonObject * object)
{
	RdpServer * server = g_object_new(RDP_SERVER_TYPE, NULL);

	if (json_object_has_member(object, JSON_SERVER_NAME)) {
		JsonNode * node = json_object_get_member(object, JSON_SERVER_NAME);
		if (JSON_NODE_TYPE(node) == JSON_NODE_VALUE && json_node_get_value_type(node) == G_TYPE_STRING) {
			const gchar * name = json_node_get_string(node);
			server->parent.name = g_strdup(name);
		}
	}

	if (json_object_has_member(object, JSON_URI)) {
		JsonNode * node = json_object_get_member(object, JSON_URI);
		if (JSON_NODE_TYPE(node) == JSON_NODE_VALUE && json_node_get_value_type(node) == G_TYPE_STRING) {
			const gchar * uri = json_node_get_string(node);
			server->parent.uri = g_strdup(uri);
		}
	}

	if (json_object_has_member(object, JSON_USERNAME)) {
		JsonNode * node = json_object_get_member(object, JSON_USERNAME);
		if (JSON_NODE_TYPE(node) == JSON_NODE_VALUE && json_node_get_value_type(node) == G_TYPE_STRING) {
			const gchar * username = json_node_get_string(node);
			server->username = g_strdup(username);
		}
	}

	if (json_object_has_member(object, JSON_PASSWORD)) {
		JsonNode * node = json_object_get_member(object, JSON_PASSWORD);
		if (JSON_NODE_TYPE(node) == JSON_NODE_VALUE && json_node_get_value_type(node) == G_TYPE_STRING) {
			const gchar * password = json_node_get_string(node);
			server->password = g_strdup(password);
			mlock(server->password, strlen(server->password));
		}
	}

	if (json_object_has_member(object, JSON_DOMAIN)) {
		JsonNode * node = json_object_get_member(object, JSON_DOMAIN);
		if (JSON_NODE_TYPE(node) == JSON_NODE_VALUE && json_node_get_value_type(node) == G_TYPE_STRING) {
			const gchar * domain = json_node_get_string(node);
			server->domain = g_strdup(domain);
		}
	}

	if (json_object_has_member(object, JSON_DOMAIN_REQ)) {
		JsonNode * node = json_object_get_member(object, JSON_DOMAIN_REQ);
		if (JSON_NODE_TYPE(node) == JSON_NODE_VALUE && json_node_get_value_type(node) == G_TYPE_BOOLEAN) {
			server->domain_required = json_node_get_boolean(node);
		}
	}

	return SERVER(server);
}
