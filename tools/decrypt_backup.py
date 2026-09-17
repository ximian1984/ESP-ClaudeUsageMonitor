#!/usr/bin/env python3
"""Titkositott beallitas-mentes visszafejtese a dongle NELKUL (katasztrofa-helyreallitas, fuggetlen ellenorzes).

Formatum: src/backup_crypto.h. PBKDF2-HMAC-SHA256 -> AES-256-GCM, AAD = "<format>/<version>".
Fuggoseg: pip install cryptography

Hasznalat:
  python3 decrypt_backup.py device-config-....-ENCRYPTED.json            # jelszot bekeri (nem latszik)
  python3 decrypt_backup.py fajl.json --summary                           # csak osszegzes, titok nelkul
A kimenet (teljes mod) TITKOT tartalmaz: jelszokent kezelendo.
"""
import argparse
import base64
import getpass
import hashlib
import json
import sys

from cryptography.hazmat.primitives.ciphers.aead import AESGCM


def decrypt(envelope: dict, password: str) -> dict:
    if envelope.get("format") != "device-config-encrypted" or envelope.get("version") != 1:
        raise ValueError("nem titkositott beallitas-mentes (format/version)")
    if envelope.get("kdf") != "pbkdf2-hmac-sha256" or envelope.get("cipher") != "aes-256-gcm":
        raise ValueError("ismeretlen kdf/cipher")
    salt = base64.b64decode(envelope["salt"])
    iv = base64.b64decode(envelope["iv"])
    tag = base64.b64decode(envelope["tag"])
    data = base64.b64decode(envelope["data"])
    key = hashlib.pbkdf2_hmac("sha256", password.encode(), salt, int(envelope["iterations"]), 32)
    aad = f'{envelope["format"]}/{envelope["version"]}'.encode()
    plain = AESGCM(key).decrypt(iv, data + tag, aad)  # rossz jelszonal InvalidTag
    return json.loads(plain)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--summary", action="store_true", help="csak profilnevek/SSID-k es titok-hosszak")
    ap.add_argument("--password-file", help="jelszo fajlbol (automatizalt teszthez)")
    a = ap.parse_args()
    env = json.load(open(a.file, encoding="utf-8"))
    pw = open(a.password_file, encoding="utf-8").read() if a.password_file else getpass.getpass("Admin password: ")
    try:
        cfg = decrypt(env, pw)
    except Exception as e:  # noqa: BLE001
        print(f"visszafejtes sikertelen: {type(e).__name__}", file=sys.stderr)
        return 1
    if a.summary:
        print("format:", cfg.get("format"), "secrets:", cfg.get("secrets"), "exportedAt:", cfg.get("exportedAt"))
        print("wifi:", [(w["ssid"], "password len=%d" % len(w.get("password", ""))) for w in cfg.get("wifi", [])])
        print("claude:", [(c["name"], c["transport"], "auth len=%d" % len(c.get("auth", "")),
                           "refresh len=%d" % len(c.get("refresh", ""))) for c in cfg.get("claude", [])])
        print("tz:", cfg.get("tz"), "rotationSec:", cfg.get("rotationSec"), "refreshSec:", cfg.get("refreshSec"))
    else:
        json.dump(cfg, sys.stdout, indent=1, ensure_ascii=False)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
