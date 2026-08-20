#include "airscope_identity.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "psa/crypto.h"

#define IDENTITY_NAMESPACE "airscope_tls"
#define CERTIFICATE_KEY "certificate"
#define PRIVATE_KEY_KEY "private_key"
#define CERTIFICATE_CAPACITY 2048
#define PRIVATE_KEY_CAPACITY 1024

static const char *TAG = "airscope_identity";

static esp_err_t load_blob(nvs_handle_t nvs, const char *key, uint8_t **data,
                           size_t *length)
{
    size_t size = 0;
    esp_err_t err = nvs_get_blob(nvs, key, NULL, &size);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t *buffer = malloc(size);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    err = nvs_get_blob(nvs, key, buffer, &size);
    if (err != ESP_OK) {
        free(buffer);
        return err;
    }
    *data = buffer;
    *length = size;
    return ESP_OK;
}

static esp_err_t save_identity(nvs_handle_t nvs,
                               const airscope_https_identity_t *identity)
{
    esp_err_t err = nvs_set_blob(nvs, CERTIFICATE_KEY, identity->certificate_pem,
                                 identity->certificate_len);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, PRIVATE_KEY_KEY, identity->private_key_pem,
                           identity->private_key_len);
    }
    return err == ESP_OK ? nvs_commit(nvs) : err;
}

static esp_err_t generate_identity(airscope_https_identity_t *identity)
{
    esp_err_t result = ESP_FAIL;
    int rc = 0;
    psa_status_t psa_status;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    bool key_created = false;
    mbedtls_pk_context key;
    mbedtls_x509write_cert certificate;
    uint8_t serial[16];

    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&certificate);

    psa_set_key_type(
        &attributes,
        PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(
        &attributes, PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(
        &attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));

    psa_status = psa_generate_key(&attributes, &key_id);
    if (psa_status != PSA_SUCCESS) {
        rc = (int)psa_status;
        goto done;
    }
    key_created = true;
    rc = mbedtls_pk_copy_from_psa(key_id, &key);
    if (rc != 0) {
        goto done;
    }
    psa_status = psa_generate_random(serial, sizeof(serial));
    if (psa_status != PSA_SUCCESS) {
        rc = (int)psa_status;
        goto done;
    }
    serial[0] &= 0x7f;
    if (serial[0] == 0) {
        serial[0] = 1;
    }

    mbedtls_x509write_crt_set_version(&certificate, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&certificate, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&certificate, &key);
    mbedtls_x509write_crt_set_issuer_key(&certificate, &key);
    if (mbedtls_x509write_crt_set_subject_name(
            &certificate, "CN=AirScope,O=AirScope Experimental AP") != 0 ||
        mbedtls_x509write_crt_set_issuer_name(
            &certificate, "CN=AirScope,O=AirScope Experimental AP") != 0 ||
        mbedtls_x509write_crt_set_serial_raw(&certificate, serial,
                                             sizeof(serial)) != 0 ||
        mbedtls_x509write_crt_set_validity(&certificate, "20240101000000",
                                           "20491231235959") != 0 ||
        mbedtls_x509write_crt_set_basic_constraints(&certificate, 0, -1) != 0 ||
        mbedtls_x509write_crt_set_subject_key_identifier(&certificate) != 0 ||
        mbedtls_x509write_crt_set_authority_key_identifier(&certificate) != 0 ||
        mbedtls_x509write_crt_set_key_usage(
            &certificate, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) != 0) {
        goto done;
    }

    identity->certificate_pem = calloc(1, CERTIFICATE_CAPACITY);
    identity->private_key_pem = calloc(1, PRIVATE_KEY_CAPACITY);
    if (identity->certificate_pem == NULL || identity->private_key_pem == NULL) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }
    rc = mbedtls_x509write_crt_pem(&certificate, identity->certificate_pem,
                                   CERTIFICATE_CAPACITY);
    if (rc != 0) {
        goto done;
    }
    rc = mbedtls_pk_write_key_pem(&key, identity->private_key_pem,
                                  PRIVATE_KEY_CAPACITY);
    if (rc != 0) {
        goto done;
    }
    identity->certificate_len =
        strlen((const char *)identity->certificate_pem) + 1;
    identity->private_key_len =
        strlen((const char *)identity->private_key_pem) + 1;
    result = ESP_OK;

done:
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS identity generation failed: mbedTLS=%d", rc);
        airscope_identity_free(identity);
    }
    mbedtls_x509write_crt_free(&certificate);
    mbedtls_pk_free(&key);
    if (key_created) {
        psa_destroy_key(key_id);
    }
    psa_reset_key_attributes(&attributes);
    return result;
}

esp_err_t airscope_identity_load_or_create(airscope_https_identity_t *identity)
{
    if (identity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(identity, 0, sizeof(*identity));

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(IDENTITY_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    esp_err_t cert_err = load_blob(nvs, CERTIFICATE_KEY,
                                   &identity->certificate_pem,
                                   &identity->certificate_len);
    esp_err_t key_err = load_blob(nvs, PRIVATE_KEY_KEY,
                                  &identity->private_key_pem,
                                  &identity->private_key_len);
    if (cert_err == ESP_OK && key_err == ESP_OK) {
        nvs_close(nvs);
        return ESP_OK;
    }

    airscope_identity_free(identity);
    err = generate_identity(identity);
    if (err == ESP_OK) {
        err = save_identity(nvs, identity);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        airscope_identity_free(identity);
    } else {
        ESP_LOGI(TAG, "Created persistent device HTTPS identity");
    }
    return err;
}

void airscope_identity_free(airscope_https_identity_t *identity)
{
    if (identity == NULL) {
        return;
    }
    free(identity->certificate_pem);
    free(identity->private_key_pem);
    memset(identity, 0, sizeof(*identity));
}
