/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "s2n_test.h"

#include <stdint.h>
#include <s2n.h>

#include "tls/extensions/s2n_server_key_share.h"
#include "tls/extensions/s2n_client_key_share.h"

#include "tls/s2n_tls13.h"
#include "testlib/s2n_testlib.h"
#include "stuffer/s2n_stuffer.h"
#include "utils/s2n_safety.h"

#define S2N_STUFFER_READ_SKIP_TILL_END( stuffer ) do { \
    EXPECT_SUCCESS(s2n_stuffer_skip_read(stuffer,      \
        s2n_stuffer_data_available(stuffer)));         \
} while (0)

int main(int argc, char **argv)
{
    BEGIN_TEST();
    EXPECT_SUCCESS(s2n_enable_tls13());

    /* Test s2n_extensions_server_key_share_send_check */
    {
        struct s2n_connection *conn;

        EXPECT_NOT_NULL(conn = s2n_connection_new(S2N_SERVER));
        EXPECT_FAILURE(s2n_extensions_server_key_share_send_check(conn));

        conn->secure.server_ecc_params.negotiated_curve = &s2n_ecc_supported_curves[0];
        EXPECT_FAILURE(s2n_extensions_server_key_share_send_check(conn));

        conn->secure.client_ecc_params[0].negotiated_curve = &s2n_ecc_supported_curves[0];
        EXPECT_FAILURE(s2n_extensions_server_key_share_send_check(conn));

        EXPECT_SUCCESS(s2n_ecc_generate_ephemeral_key(&conn->secure.client_ecc_params[0]));
        EXPECT_SUCCESS(s2n_extensions_server_key_share_send_check(conn));

        EXPECT_SUCCESS(s2n_connection_free(conn));
    }

    /* Test s2n_extensions_server_key_share_send_size */
    {
        struct s2n_connection *conn;
        EXPECT_NOT_NULL(conn = s2n_connection_new(S2N_SERVER));
        EXPECT_EQUAL(0, s2n_extensions_server_key_share_send_size(conn));

        conn->secure.server_ecc_params.negotiated_curve = &s2n_ecc_supported_curves[0];
        EXPECT_EQUAL(s2n_ecc_supported_curves[0].share_size + 8, s2n_extensions_server_key_share_send_size(conn));

        conn->secure.server_ecc_params.negotiated_curve = &s2n_ecc_supported_curves[1];
        EXPECT_EQUAL(s2n_ecc_supported_curves[1].share_size + 8, s2n_extensions_server_key_share_send_size(conn));

        conn->secure.server_ecc_params.negotiated_curve = NULL;
        EXPECT_EQUAL(0, s2n_extensions_server_key_share_send_size(conn));

        EXPECT_SUCCESS(s2n_connection_free(conn));
    }

    /* Test s2n_extensions_server_key_share_send */
    {
        struct s2n_connection *conn;

        EXPECT_NOT_NULL(conn = s2n_connection_new(S2N_SERVER));
        struct s2n_stuffer* extension_stuffer = &conn->handshake.io;

        /* Error if no curve have been selected */
        EXPECT_FAILURE_WITH_ERRNO(s2n_extensions_server_key_share_send(conn, extension_stuffer), S2N_ERR_NULL);

        S2N_STUFFER_READ_SKIP_TILL_END(extension_stuffer);

        for (int i = 0; i < S2N_ECC_SUPPORTED_CURVES_COUNT; i++) {
            conn->secure.server_ecc_params.negotiated_curve = &s2n_ecc_supported_curves[i];
            conn->secure.client_ecc_params[i].negotiated_curve = &s2n_ecc_supported_curves[i];
            EXPECT_SUCCESS(s2n_ecc_generate_ephemeral_key(&conn->secure.client_ecc_params[i]));
            EXPECT_SUCCESS(s2n_extensions_server_key_share_send(conn, extension_stuffer));
            S2N_STUFFER_LENGTH_WRITTEN_EXPECT_EQUAL(extension_stuffer, s2n_ecc_supported_curves[i].share_size + 8);
            EXPECT_EQUAL(conn->secure.server_ecc_params.negotiated_curve, &s2n_ecc_supported_curves[i]);
            EXPECT_SUCCESS(s2n_ecc_params_free(&conn->secure.server_ecc_params));
        }

        EXPECT_SUCCESS(s2n_connection_free(conn));
    }

    /* Test s2n_extensions_server_key_share_send_check for failures */
    {
        struct s2n_connection *conn;

        EXPECT_NOT_NULL(conn = s2n_connection_new(S2N_SERVER));
        struct s2n_stuffer* extension_stuffer = &conn->handshake.io;

        EXPECT_FAILURE(s2n_extensions_server_key_share_send(conn, extension_stuffer));

        conn->secure.server_ecc_params.negotiated_curve = &s2n_ecc_supported_curves[0];
        EXPECT_FAILURE(s2n_extensions_server_key_share_send(conn, extension_stuffer));

        conn->secure.client_ecc_params[0].negotiated_curve = &s2n_ecc_supported_curves[0];
        EXPECT_FAILURE(s2n_extensions_server_key_share_send(conn, extension_stuffer));

        EXPECT_SUCCESS(s2n_ecc_generate_ephemeral_key(&conn->secure.client_ecc_params[0]));
        EXPECT_SUCCESS(s2n_extensions_server_key_share_send(conn, extension_stuffer));

        conn->secure.client_ecc_params[0].negotiated_curve = &s2n_ecc_supported_curves[1];
        EXPECT_FAILURE(s2n_extensions_server_key_share_send(conn, extension_stuffer));

        EXPECT_SUCCESS(s2n_ecc_params_free(&conn->secure.server_ecc_params));
        EXPECT_SUCCESS(s2n_connection_free(conn));
    }

    /* Test s2n_extensions_server_key_share_recv with supported curves */
    {
        for (int i = 0; i < S2N_ECC_SUPPORTED_CURVES_COUNT; i++) {
            struct s2n_connection *server_send_conn;
            struct s2n_connection *client_recv_conn;

            EXPECT_NOT_NULL(server_send_conn = s2n_connection_new(S2N_SERVER));
            EXPECT_NOT_NULL(client_recv_conn = s2n_connection_new(S2N_CLIENT));

            struct s2n_stuffer* extension_stuffer = &server_send_conn->handshake.io;

            server_send_conn->secure.server_ecc_params.negotiated_curve = &s2n_ecc_supported_curves[i];
            server_send_conn->secure.client_ecc_params[i].negotiated_curve = &s2n_ecc_supported_curves[i];
            EXPECT_SUCCESS(s2n_ecc_generate_ephemeral_key(&server_send_conn->secure.client_ecc_params[i]));

            EXPECT_SUCCESS(s2n_extensions_server_key_share_send(server_send_conn, extension_stuffer));

            S2N_STUFFER_READ_EXPECT_EQUAL(extension_stuffer, TLS_EXTENSION_KEY_SHARE, uint16);
            S2N_STUFFER_READ_EXPECT_EQUAL(extension_stuffer, s2n_extensions_server_key_share_send_size(server_send_conn) - 2, uint16);

            client_recv_conn->secure.client_ecc_params[i].negotiated_curve = &s2n_ecc_supported_curves[i];
            EXPECT_SUCCESS(s2n_ecc_generate_ephemeral_key(&client_recv_conn->secure.client_ecc_params[i]));

            /* Parse key share */
            EXPECT_SUCCESS(s2n_extensions_server_key_share_recv(client_recv_conn, extension_stuffer));
            EXPECT_EQUAL(s2n_stuffer_data_available(extension_stuffer), 0);

            EXPECT_EQUAL(server_send_conn->secure.server_ecc_params.negotiated_curve->iana_id, client_recv_conn->secure.server_ecc_params.negotiated_curve->iana_id);
            EXPECT_EQUAL(server_send_conn->secure.server_ecc_params.negotiated_curve, &s2n_ecc_supported_curves[i]);

            EXPECT_SUCCESS(s2n_connection_free(server_send_conn));
            EXPECT_SUCCESS(s2n_connection_free(client_recv_conn));
        }
    }

    /* Test s2n_extensions_server_key_share_recv with various sample payloads */
    {
        /* valid extension payloads */
        {
            const char *key_share_payloads[2] = {
                /* p256 */
                "001700410474cfd75c0ab7b57247761a277e1c92b5810dacb251bb758f43e9d15aaf292c4a2be43e886425ba55653ebb7a4f32fe368bacce3df00c618645cf1eb646f22552",
                /* p384 */
                "00180061040a27264201368540483e97d324a3093e11a5862b0a1be0cf5d8510bc47ec285f5304e9ec3ba01a0c375c3b6fa4bd0ad44aae041bb776aebc7ee92462ad481fe86f8b6e3858d5c41d0f83b0404f711832a4119aec3da2eac86266f424b50aa212"
            };

            for (int i = 0; i < 2; i++) {
                struct s2n_stuffer extension_stuffer;
                struct s2n_connection *client_conn;

                EXPECT_NOT_NULL(client_conn = s2n_connection_new(S2N_CLIENT));
                const char *payload = key_share_payloads[i];

                EXPECT_NULL(client_conn->secure.server_ecc_params.negotiated_curve);
                EXPECT_SUCCESS(s2n_stuffer_alloc_ro_from_hex_string(&extension_stuffer, payload));

                client_conn->secure.client_ecc_params[i].negotiated_curve = &s2n_ecc_supported_curves[i];
                EXPECT_SUCCESS(s2n_ecc_generate_ephemeral_key(&client_conn->secure.client_ecc_params[i]));

                EXPECT_SUCCESS(s2n_extensions_server_key_share_recv(client_conn, &extension_stuffer));
                EXPECT_EQUAL(client_conn->secure.server_ecc_params.negotiated_curve, &s2n_ecc_supported_curves[i]);
                EXPECT_EQUAL(s2n_stuffer_data_available(&extension_stuffer), 0);

                EXPECT_SUCCESS(s2n_stuffer_free(&extension_stuffer));
                EXPECT_SUCCESS(s2n_connection_free(client_conn));
            }
        }

        /* Test s2n_extensions_server_key_share_recv with unsupported curve (x25519) */
        {
            struct s2n_stuffer extension_stuffer;
            struct s2n_connection *client_conn;

            EXPECT_NOT_NULL(client_conn = s2n_connection_new(S2N_CLIENT));
            const char *x25519 = "001d00206b24ffd795c496899cd14b7742a5ffbdc453c23085a7f82f0ed1e0296adb9e0e";

            EXPECT_NULL(client_conn->secure.server_ecc_params.negotiated_curve);
            EXPECT_SUCCESS(s2n_stuffer_alloc_ro_from_hex_string(&extension_stuffer, x25519));

            EXPECT_FAILURE_WITH_ERRNO(s2n_extensions_server_key_share_recv(client_conn, &extension_stuffer), S2N_ERR_BAD_KEY_SHARE);

            EXPECT_SUCCESS(s2n_stuffer_free(&extension_stuffer));
            EXPECT_SUCCESS(s2n_connection_free(client_conn));
        }

        /* Test error handling parsing broken/trancated p256 key share */
        {
            struct s2n_stuffer extension_stuffer;
            struct s2n_connection *client_conn;

            EXPECT_NOT_NULL(client_conn = s2n_connection_new(S2N_CLIENT));
            const char *p256 = "001700410474cfd75c0ab7b57247761a277e1c92b5810dacb251bb758f43e9d15aaf292c4a2be43e886425ba55653ebb7a4f32fe368bacce3df00c618645cf1eb6";

            EXPECT_NULL(client_conn->secure.server_ecc_params.negotiated_curve);
            EXPECT_SUCCESS(s2n_stuffer_alloc_ro_from_hex_string(&extension_stuffer, p256));

            EXPECT_FAILURE_WITH_ERRNO(s2n_extensions_server_key_share_recv(client_conn, &extension_stuffer), S2N_ERR_BAD_KEY_SHARE);

            EXPECT_SUCCESS(s2n_stuffer_free(&extension_stuffer));
            EXPECT_SUCCESS(s2n_connection_free(client_conn));
        }

        /* Test failure for receiving p256 key share for client configured p384 key share */
        {
            struct s2n_stuffer extension_stuffer;
            struct s2n_connection *client_conn;

            EXPECT_NOT_NULL(client_conn = s2n_connection_new(S2N_CLIENT));
            const char *p256 = "001700410474cfd75c0ab7b57247761a277e1c92b5810dacb251bb758f43e9d15aaf292c4a2be43e886425ba55653ebb7a4f32fe368bacce3df00c618645cf1eb646f22552";

            EXPECT_NULL(client_conn->secure.server_ecc_params.negotiated_curve);
            EXPECT_SUCCESS(s2n_stuffer_alloc_ro_from_hex_string(&extension_stuffer, p256));

            client_conn->secure.client_ecc_params[1].negotiated_curve = &s2n_ecc_supported_curves[1];
            EXPECT_SUCCESS(s2n_ecc_generate_ephemeral_key(&client_conn->secure.client_ecc_params[1]));

            EXPECT_FAILURE_WITH_ERRNO(s2n_extensions_server_key_share_recv(client_conn, &extension_stuffer), S2N_ERR_BAD_KEY_SHARE);

            EXPECT_SUCCESS(s2n_stuffer_free(&extension_stuffer));
            EXPECT_SUCCESS(s2n_connection_free(client_conn));
        }
    }

    /* Test Shared Key Generation */
    {
        int shared_secret_size[2] = { 32, 48 };
        for (int i = 0; i < S2N_ECC_SUPPORTED_CURVES_COUNT; i++)
        {
            struct s2n_connection *client_conn;
            struct s2n_connection *server_conn;
            struct s2n_stuffer client_hello_key_share;
            struct s2n_stuffer server_hello_key_share;

            EXPECT_NOT_NULL(client_conn = s2n_connection_new(S2N_CLIENT));
            EXPECT_NOT_NULL(server_conn = s2n_connection_new(S2N_SERVER));
            EXPECT_SUCCESS(s2n_stuffer_growable_alloc(&client_hello_key_share, 1024));
            EXPECT_SUCCESS(s2n_stuffer_growable_alloc(&server_hello_key_share, 1024));

            /* Client sends ClientHello key_share */
            EXPECT_SUCCESS(s2n_extensions_client_key_share_send(client_conn, &client_hello_key_share));

            /* Server receives ClientHello key_share */
            S2N_STUFFER_READ_EXPECT_EQUAL(&client_hello_key_share, TLS_EXTENSION_KEY_SHARE, uint16);
            S2N_STUFFER_READ_EXPECT_EQUAL(&client_hello_key_share, s2n_extensions_client_key_share_size(server_conn) - 4, uint16);
            EXPECT_SUCCESS(s2n_extensions_client_key_share_recv(server_conn, &client_hello_key_share));
            EXPECT_EQUAL(s2n_stuffer_data_available(&client_hello_key_share), 0);

            EXPECT_NULL(server_conn->secure.server_ecc_params.negotiated_curve);

            /* Configure the "negotiated_curve" */
            server_conn->secure.server_ecc_params.negotiated_curve = &s2n_ecc_supported_curves[i];
            for (int j = 0; j < S2N_ECC_SUPPORTED_CURVES_COUNT; j++) {
                if (j != i) {
                    server_conn->secure.client_ecc_params[j].negotiated_curve = NULL;
                }
            }

            EXPECT_NOT_NULL(server_conn->secure.server_ecc_params.negotiated_curve);
            EXPECT_EQUAL(server_conn->secure.server_ecc_params.negotiated_curve->iana_id, s2n_ecc_supported_curves[i].iana_id);

            /* Server sends ServerHello key_share */
            EXPECT_SUCCESS(s2n_extensions_server_key_share_send(server_conn, &server_hello_key_share));

            /* Client receives ServerHello key_share */
            S2N_STUFFER_READ_EXPECT_EQUAL(&server_hello_key_share, TLS_EXTENSION_KEY_SHARE, uint16);
            S2N_STUFFER_READ_EXPECT_EQUAL(&server_hello_key_share, s2n_extensions_server_key_share_send_size(server_conn) - 2, uint16);
            EXPECT_SUCCESS(s2n_extensions_server_key_share_recv(client_conn, &server_hello_key_share));
            EXPECT_EQUAL(s2n_stuffer_data_available(&server_hello_key_share), 0);

            EXPECT_EQUAL(server_conn->secure.server_ecc_params.negotiated_curve, client_conn->secure.server_ecc_params.negotiated_curve);

            /* Ensure both client and server public key matches */
            s2n_public_ecc_keys_are_equal(&server_conn->secure.server_ecc_params, &client_conn->secure.server_ecc_params);
            s2n_public_ecc_keys_are_equal(&server_conn->secure.client_ecc_params[i], &client_conn->secure.client_ecc_params[i]);

            /* Server generates shared key based on Server's Key (EC Key) and Client's public key (EC Point) */
            struct s2n_blob server_shared_secret = { 0 };
            EXPECT_SUCCESS(s2n_ecc_compute_shared_secret_from_params(
                &server_conn->secure.server_ecc_params,
                &server_conn->secure.client_ecc_params[i],
                &server_shared_secret));

            /* Clients generates shared key based on Client's EC Key and Server's public key */
            struct s2n_blob client_shared_secret = { 0 };
            EXPECT_SUCCESS(s2n_ecc_compute_shared_secret_from_params(
                &client_conn->secure.client_ecc_params[i],
                &client_conn->secure.server_ecc_params,
                &client_shared_secret));

            /* Test that server shared secret matches client shared secret */
            EXPECT_EQUAL(server_shared_secret.size, shared_secret_size[i]);

            S2N_BLOB_EXPECT_EQUAL(server_shared_secret, client_shared_secret);

            EXPECT_SUCCESS(s2n_free(&client_shared_secret));
            EXPECT_SUCCESS(s2n_free(&server_shared_secret));

            /* Clean up */
            EXPECT_SUCCESS(s2n_stuffer_free(&client_hello_key_share));
            EXPECT_SUCCESS(s2n_stuffer_free(&server_hello_key_share));
            EXPECT_SUCCESS(s2n_connection_free(client_conn));
            EXPECT_SUCCESS(s2n_connection_free(server_conn));
        }
    }

    END_TEST();
    return 0;
}

