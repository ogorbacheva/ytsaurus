#include "x509_vfy.h"

namespace NOpenSSL {

    TX509LookupMethod::TX509LookupMethod(
        const char* name,
        int (*newItem) (X509_LOOKUP *ctx),
        void (*free) (X509_LOOKUP *ctx),
        int (*init) (X509_LOOKUP *ctx),
        int (*shutdown) (X509_LOOKUP *ctx),
        X509_LOOKUP_ctrl_fn ctrl,
        X509_LOOKUP_get_by_subject_fn getBySubject,
        X509_LOOKUP_get_by_issuer_serial_fn getByIssuerSerial,
        X509_LOOKUP_get_by_fingerprint_fn getByFingerprint,
        X509_LOOKUP_get_by_alias_fn getByAlias
    )
        : THolder(name)
    {
        X509_LOOKUP_meth_set_new_item(*this, newItem);
        X509_LOOKUP_meth_set_free(*this, free);
        X509_LOOKUP_meth_set_init(*this, init);
        X509_LOOKUP_meth_set_shutdown(*this, shutdown);
        X509_LOOKUP_meth_set_ctrl(*this, ctrl);
        X509_LOOKUP_meth_set_get_by_subject(*this, getBySubject);
        X509_LOOKUP_meth_set_get_by_issuer_serial(*this, getByIssuerSerial);
        X509_LOOKUP_meth_set_get_by_fingerprint(*this, getByFingerprint);
        X509_LOOKUP_meth_set_get_by_alias(*this, getByAlias);
    }

} // namespace NOpenSSL
