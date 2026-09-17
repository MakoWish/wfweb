"""Integration coverage for the server-side ADIF logbook API."""

import requests


def test_logbook_crud_and_adif_download(rest_url):
    base = rest_url.removesuffix("/radio") + "/logbook"
    qso = {
        "date": "20260917",
        "time": "120000",
        "call": "K1TEST",
        "freq": 14074000,
        "band": "20M",
        "mode": "FT8",
        "rstSent": "-08",
        "rstRcvd": "-11",
    }

    created = requests.post(base, json=qso, timeout=5)
    assert created.status_code == 202
    entry = created.json()
    assert entry["call"] == "K1TEST"
    assert entry["id"]

    listed = requests.get(base, timeout=5)
    assert listed.status_code == 200
    assert any(item["id"] == entry["id"] for item in listed.json()["entries"])

    qso["rstRcvd"] = "-09"
    updated = requests.put(f"{base}/{entry['id']}", json=qso, timeout=5)
    assert updated.status_code == 200
    assert updated.json()["rstRcvd"] == "-09"

    adif = requests.get(f"{base}/adif", timeout=5)
    assert adif.status_code == 200
    assert "<CALL:6>K1TEST" in adif.text
    assert "<RST_RCVD:3>-09" in adif.text

    deleted = requests.delete(f"{base}/{entry['id']}", timeout=5)
    assert deleted.status_code == 202
    missing = requests.get(base, timeout=5).json()["entries"]
    assert not any(item["id"] == entry["id"] for item in missing)
