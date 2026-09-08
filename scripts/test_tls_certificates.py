#!/usr/bin/env python3
"""Exercise the pinned upstream X.509 verifier on the host, not an ESP device.

Run with the isolated ESP-IDF Python environment (cryptography is required).
"""

from datetime import datetime, timedelta, timezone
from pathlib import Path
import subprocess
import tempfile

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

from build_tls_toolchain import CACHE, LOCK, verify_source


HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include "mbedtls/x509_crt.h"

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    mbedtls_x509_crt root, leaf;
    mbedtls_x509_crt_init(&root);
    mbedtls_x509_crt_init(&leaf);
    int result = mbedtls_x509_crt_parse_file(&root, argv[1]);
    if (result == 0) result = mbedtls_x509_crt_parse_file(&leaf, argv[2]);
    uint32_t flags = 0;
    if (result == 0) {
        result = mbedtls_x509_crt_verify(&leaf, &root, NULL,
                                       "broker.example.test", &flags, NULL, NULL);
    }
    printf("%d %u\n", result, (unsigned) flags);
    mbedtls_x509_crt_free(&leaf);
    mbedtls_x509_crt_free(&root);
    return 0;
}
'''


def certificate(key, issuer_key, subject, issuer, start, end, *, ca=False, hostname=None):
    builder = (x509.CertificateBuilder().subject_name(subject).issuer_name(issuer)
               .public_key(key.public_key()).serial_number(x509.random_serial_number())
               .not_valid_before(start).not_valid_after(end)
               .add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True))
    if hostname:
        builder = builder.add_extension(x509.SubjectAlternativeName([x509.DNSName(hostname)]), critical=False)
    return builder.sign(issuer_key, hashes.SHA256()).public_bytes(serialization.Encoding.PEM)


def main():
    verify_source("esp-idf", LOCK["sources"]["esp-idf"]["commit"])
    source = CACHE / "esp-idf/components/mbedtls/mbedtls"
    expected = subprocess.check_output(
        ["git", "rev-parse", "HEAD:components/mbedtls/mbedtls"], cwd=CACHE / "esp-idf", text=True).strip()
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    if actual != expected or subprocess.check_output(["git", "diff", "HEAD", "--name-only"], cwd=source):
        raise RuntimeError("X.509 source differs from pinned ESP-IDF submodule")
    build = CACHE / "host-mbedtls"
    subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                    "-DENABLE_PROGRAMS=OFF", "-DENABLE_TESTING=OFF", "-DGEN_FILES=OFF"], check=True)
    subprocess.run(["cmake", "--build", str(build), "-j", "4"], check=True)
    with tempfile.TemporaryDirectory(prefix="esp32base-certificates-") as temporary:
        directory = Path(temporary)
        (directory / "verify.c").write_text(HARNESS)
        executable = directory / "verify"
        subprocess.run(["cc", "-I", str(source / "include"), str(directory / "verify.c"),
                        str(build / "library/libmbedx509.a"), str(build / "library/libmbedcrypto.a"),
                        "-o", str(executable)], check=True)
        now = datetime.now(timezone.utc)
        root_key = ec.generate_private_key(ec.SECP256R1())
        leaf_key = ec.generate_private_key(ec.SECP256R1())
        root_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Ephemeral test CA")])
        leaf_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "broker.example.test")])
        (directory / "root.pem").write_bytes(certificate(
            root_key, root_key, root_name, root_name, now - timedelta(days=30), now + timedelta(days=30), ca=True))
        # Exact flags matter: an expired certificate rejected for a broken
        # signature would not prove that date validation actually happened.
        cases = (
            ("valid", -1, 1, "broker.example.test", root_key, 0),
            ("expired", -2, -1, "broker.example.test", root_key, 0x01),
            ("not-yet-valid", 1, 2, "broker.example.test", root_key, 0x200),
            ("wrong-hostname", -1, 1, "other.example.test", root_key, 0x04),
            ("untrusted-signer", -1, 1, "broker.example.test", leaf_key, 0x08),
        )
        for name, start, end, hostname, signer, expected_flags in cases:
            path = directory / (name + ".pem")
            path.write_bytes(certificate(leaf_key, signer, leaf_name, root_name,
                                        now + timedelta(days=start), now + timedelta(days=end), hostname=hostname))
            result = subprocess.check_output([str(executable), str(directory / "root.pem"), str(path)], text=True)
            code, flags = map(int, result.split())
            if flags != expected_flags or (code == 0) != (expected_flags == 0):
                raise RuntimeError(f"{name}: verification code={code}, flags={flags:#x}, expected={expected_flags:#x}")
            print(f"PASS {name}: flags={flags:#x}")
    print("Host X.509 behavior passed; this does not establish ESP TLS handshake or resource behavior.")


if __name__ == "__main__":
    main()
