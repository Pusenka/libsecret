/* libsecret - GLib wrapper for Secret Service
 *
 * Copyright 2011 Red Hat Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Stef Walter <stefw@gnome.org>
 */


#include "config.h"

#include "secret-item.h"
#include "secret-service.h"
#include "secret-private.h"
#include "secret-prompt.h"

#include "mock-service.h"

#include "egg/egg-testing.h"

#include <glib.h>

#include <errno.h>
#include <stdlib.h>

typedef struct {
	SecretService *service;
} Test;

static void
setup (Test *test,
       gconstpointer data)
{
	GError *error = NULL;
	const gchar *mock_script = data;

	mock_service_start (mock_script, &error);
	g_assert_no_error (error);

	test->service = secret_service_get_sync (SECRET_SERVICE_NONE, NULL, &error);
	g_assert_no_error (error);
}

static void
teardown (Test *test,
          gconstpointer unused)
{
	g_object_unref (test->service);
	egg_assert_not_object (test->service);

	mock_service_stop ();
}

static void
on_async_result (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	GAsyncResult **ret = user_data;
	g_assert (ret != NULL);
	g_assert (*ret == NULL);
	*ret = g_object_ref (result);
	egg_test_wait_stop ();
}

static gboolean
on_idle_increment (gpointer user_data)
{
	guint *value = user_data;
	++(*value);
	return TRUE;
}

static void
test_perform_sync (Test *test,
                   gconstpointer unused)
{
	SecretPrompt *prompt;
	GError *error = NULL;
	gboolean ret;
	guint value = 0;
	guint increment_id;

	/* Verify that main loop does not run during this call */
	increment_id = g_idle_add (on_idle_increment, &value);

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/simple");

	ret = secret_prompt_perform_sync (prompt, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert_cmpuint (value, ==, 0);
	g_source_remove (increment_id);

	g_object_unref (prompt);
	egg_assert_not_object (prompt);
}

static void
test_perform_run (Test *test,
                  gconstpointer unused)
{
	SecretPrompt *prompt;
	GError *error = NULL;
	gboolean ret;
	guint value = 0;
	guint increment_id;

	/* Verify that main loop does run during this call */
	increment_id = g_idle_add (on_idle_increment, &value);

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/simple");

	ret = secret_prompt_run (prompt, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpuint (value, >, 0);
	g_source_remove (increment_id);

	/* Make sure everything completes */
	egg_test_wait_idle ();

	g_object_unref (prompt);
	egg_assert_not_object (prompt);
}

static void
test_perform_async (Test *test,
                    gconstpointer unused)
{
	SecretPrompt *prompt;
	GError *error = NULL;
	GAsyncResult *result = NULL;
	gboolean ret;

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/simple");

	secret_prompt_perform (prompt, 0, NULL, on_async_result, &result);
	g_assert (result == NULL);

	egg_test_wait ();

	ret = secret_prompt_perform_finish (prompt, result, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_object_unref (result);

	/* Make sure everything completes */
	egg_test_wait_idle ();

	g_object_unref (prompt);
	egg_assert_not_object (prompt);
}

static void
test_perform_cancel (Test *test,
                     gconstpointer unused)
{
	SecretPrompt *prompt;
	GError *error = NULL;
	GAsyncResult *result = NULL;
	GCancellable *cancellable;
	gboolean ret;

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/delay");

	cancellable = g_cancellable_new ();
	secret_prompt_perform (prompt, 0, cancellable, on_async_result, &result);
	g_assert (result == NULL);

	g_cancellable_cancel (cancellable);
	g_object_unref (cancellable);

	egg_test_wait ();

	ret = secret_prompt_perform_finish (prompt, result, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_object_unref (result);
	g_object_unref (prompt);

	/* Due to GDBus threading races */
	egg_test_wait_until (100);

	egg_assert_not_object (prompt);
}

static void
test_perform_fail (Test *test,
                   gconstpointer unused)
{
	SecretPrompt *prompt;
	GError *error = NULL;
	gboolean ret;

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/error");

	ret = secret_prompt_perform_sync (prompt, 0, NULL, &error);
	g_assert_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED);
	g_assert (ret == FALSE);

	g_object_unref (prompt);
	egg_assert_not_object (prompt);
}

static void
test_perform_vanish (Test *test,
                     gconstpointer unused)
{
	SecretPrompt *prompt;
	GError *error = NULL;
	gboolean ret;

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/vanish");

	ret = secret_prompt_perform_sync (prompt, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == FALSE);

	g_object_unref (prompt);
	egg_assert_not_object (prompt);
}

static void
test_prompt_result (Test *test,
                    gconstpointer unused)
{
	SecretPrompt *prompt;
	GError *error = NULL;
	gboolean ret;
	GVariant *result;

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/result");

	result = secret_prompt_get_result_value (prompt, NULL);
	g_assert (result == NULL);

	ret = secret_prompt_perform_sync (prompt, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	result = secret_prompt_get_result_value (prompt, G_VARIANT_TYPE_STRING);
	g_assert (result != NULL);
	g_assert_cmpstr (g_variant_get_string (result, NULL), ==, "Special Result");
	g_variant_unref (result);

	g_object_unref (prompt);
	egg_assert_not_object (prompt);
}

static void
test_prompt_window_id (Test *test,
                       gconstpointer unused)
{
	SecretPrompt *prompt;
	GError *error = NULL;
	gboolean ret;
	GVariant *result;

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/window");

	ret = secret_prompt_perform_sync (prompt, 555, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	result = secret_prompt_get_result_value (prompt, G_VARIANT_TYPE_STRING);
	g_assert (result != NULL);
	g_assert_cmpstr (g_variant_get_string (result, NULL), ==, "555");
	g_variant_unref (result);

	g_object_unref (prompt);
	egg_assert_not_object (prompt);
}

static void
test_service_sync (Test *test,
                   gconstpointer unused)
{
	SecretPrompt *prompt;
	GError *error = NULL;
	gboolean ret;

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/simple");

	ret = secret_service_prompt_sync (test->service, prompt, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_object_unref (prompt);
	egg_assert_not_object (prompt);
}

static void
test_service_async (Test *test,
                    gconstpointer unused)
{
	SecretPrompt *prompt;
	GError *error = NULL;
	GAsyncResult *result = NULL;
	gboolean ret;

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/simple");

	secret_service_prompt (test->service, prompt, NULL, on_async_result, &result);
	g_assert (result == NULL);

	egg_test_wait ();

	ret = secret_service_prompt_finish (test->service, result, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_object_unref (result);

	/* Make sure everything completes */
	egg_test_wait_idle ();

	g_object_unref (prompt);
	egg_assert_not_object (prompt);
}

static void
test_service_fail (Test *test,
                    gconstpointer unused)
{
	SecretPrompt *prompt;
	GError *error = NULL;
	GAsyncResult *result = NULL;
	gboolean ret;

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/error");

	secret_service_prompt (test->service, prompt, NULL, on_async_result, &result);
	g_assert (result == NULL);

	egg_test_wait ();

	ret = secret_service_prompt_finish (test->service, result, &error);
	g_assert_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED);
	g_assert (ret == FALSE);
	g_object_unref (result);

	/* Make sure everything completes */
	egg_test_wait_idle ();

	g_object_unref (prompt);
	egg_assert_not_object (prompt);
}

static void
test_service_path (Test *test,
                    gconstpointer unused)
{
	GError *error = NULL;
	GAsyncResult *result = NULL;
	SecretPrompt *prompt;
	gboolean ret;

	prompt = _secret_prompt_instance (test->service, "/org/freedesktop/secrets/prompts/simple");

	secret_service_prompt (test->service, prompt, NULL, on_async_result, &result);
	g_assert (result == NULL);

	g_object_unref (prompt);
	egg_test_wait ();

	ret = secret_service_prompt_finish (test->service, result, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_object_unref (result);

	/* Make sure everything completes */
	egg_test_wait_idle ();
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_set_prgname ("test-prompt");
	g_type_init ();

	g_test_add ("/prompt/run", Test, "mock-service-prompt.py", setup, test_perform_run, teardown);
	g_test_add ("/prompt/perform-sync", Test, "mock-service-prompt.py", setup, test_perform_sync, teardown);
	g_test_add ("/prompt/perform-async", Test, "mock-service-prompt.py", setup, test_perform_async, teardown);
	g_test_add ("/prompt/perform-cancel", Test, "mock-service-prompt.py", setup, test_perform_cancel, teardown);
	g_test_add ("/prompt/perform-fail", Test, "mock-service-prompt.py", setup, test_perform_fail, teardown);
	g_test_add ("/prompt/perform-vanish", Test, "mock-service-prompt.py", setup, test_perform_vanish, teardown);
	g_test_add ("/prompt/result", Test, "mock-service-prompt.py", setup, test_prompt_result, teardown);
	g_test_add ("/prompt/window-id", Test, "mock-service-prompt.py", setup, test_prompt_window_id, teardown);

	g_test_add ("/prompt/service-sync", Test, "mock-service-prompt.py", setup, test_service_sync, teardown);
	g_test_add ("/prompt/service-async", Test, "mock-service-prompt.py", setup, test_service_async, teardown);
	g_test_add ("/prompt/service-path", Test, "mock-service-prompt.py", setup, test_service_path, teardown);
	g_test_add ("/prompt/service-fail", Test, "mock-service-prompt.py", setup, test_service_fail, teardown);

	return egg_tests_run_with_loop ();
}
