"""Integration coverage for the server-side ADIF logbook REST API.

The log is never returned whole: GET pages newest-first with a cursor.  These
tests add a handful of QSOs out of order and check ordering, paging, the
callsign filter, edit/delete, the ADIF download, import and clear.
"""

from pathlib import Path

import requests


def _base(rest_url):
    return rest_url.removesuffix("/radio") + "/logbook"


def _qso(call, date, time, **extra):
    q = {"date": date, "time": time, "call": call, "freq": 14074000,
         "band": "20M", "mode": "FT8", "rstSent": "-08", "rstRcvd": "-11"}
    q.update(extra)
    return q


LOGBOOK_DIR = Path("/tmp/wfweb-test/.local/share/wfweb/wfweb")   # HOME of the wfweb_instance fixture


def _clear(base):
    had = requests.get(base, timeout=5).json()["total"]
    before = set(LOGBOOK_DIR.glob("logbook.adi.*.bak"))
    r = requests.delete(base, timeout=5)
    assert r.status_code == 202
    assert requests.get(base, timeout=5).json()["total"] == 0
    new_baks = set(LOGBOOK_DIR.glob("logbook.adi.*.bak")) - before
    if had:
        # clearing never discards data: the previous file is kept as a .bak
        assert len(new_baks) == 1, new_baks
        assert new_baks.pop().read_text().count("<EOR>") == had
    else:
        assert not new_baks


def test_logbook_crud_and_adif_download(rest_url):
    base = _base(rest_url)
    _clear(base)

    created = requests.post(base, json=_qso("k1test", "20260917", "120000", theirGrid="fn42"), timeout=5)
    assert created.status_code == 202
    entry = created.json()
    assert entry["call"] == "K1TEST"          # normalised to upper case
    assert entry["theirGrid"] == "FN42"
    assert entry["id"]

    listed = requests.get(base, timeout=5).json()
    assert listed["total"] == 1
    assert [e["id"] for e in listed["entries"]] == [entry["id"]]
    assert "next" not in listed                # single page

    updated = requests.put(f"{base}/{entry['id']}", json=_qso("K1TEST", "20260917", "120000", rstRcvd="-09"), timeout=5)
    assert updated.status_code == 200
    assert updated.json()["rstRcvd"] == "-09"
    assert updated.json()["id"] == entry["id"]  # id survives an edit

    adif = requests.get(f"{base}/adif", timeout=5)
    assert adif.status_code == 200
    assert "attachment" in adif.headers.get("Content-Disposition", "")
    assert "<EOH>" in adif.text
    assert "<CALL:6>K1TEST" in adif.text
    assert "<RST_RCVD:3>-09" in adif.text
    assert f"<APP_WFWEB_ID:{len(entry['id'])}>{entry['id']}" in adif.text

    deleted = requests.delete(f"{base}/{entry['id']}", timeout=5)
    assert deleted.status_code == 202
    assert requests.get(f"{base}/{entry['id']}", timeout=5).status_code == 404
    assert requests.get(base, timeout=5).json()["total"] == 0


def test_logbook_rejects_bad_input(rest_url):
    base = _base(rest_url)
    assert requests.post(base, json={"date": "20260917"}, timeout=5).status_code == 400
    assert requests.post(base, json={"call": "X" * 40}, timeout=5).status_code == 400
    assert requests.put(f"{base}/no-such-id", json=_qso("K1A", "20260917", "120000"), timeout=5).status_code == 404
    assert requests.delete(f"{base}/no-such-id", timeout=5).status_code == 404
    assert requests.patch(base, timeout=5).status_code == 405


def test_logbook_orders_pages_and_filters(rest_url):
    base = _base(rest_url)
    _clear(base)

    # Added out of chronological order on purpose.
    for call, date, time in [("N2MID", "20260102", "120000"),
                             ("N1OLD", "20260101", "090000"),
                             ("N4NEW", "20260103", "180000"),
                             ("N3", "20260103", "100000"),
                             ("N1OLD", "20260101", "230000")]:
        assert requests.post(base, json=_qso(call, date, time), timeout=5).status_code == 202

    # Newest first regardless of insertion order.
    full = requests.get(base, timeout=5).json()
    assert full["total"] == 5
    assert [e["call"] for e in full["entries"]] == ["N4NEW", "N3", "N2MID", "N1OLD", "N1OLD"]
    assert [e["time"] for e in full["entries"]][3:] == ["230000", "090000"]

    # Walk the log two at a time with the cursor.
    seen, cursor, pages = [], "", 0
    while True:
        page = requests.get(base, params={"limit": 2, **({"before": cursor} if cursor else {})}, timeout=5).json()
        seen += [e["id"] for e in page["entries"]]
        pages += 1
        cursor = page.get("next", "")
        if not cursor:
            break
    assert pages == 3
    assert seen == [e["id"] for e in full["entries"]]

    # Callsign filter, also paged.
    n1 = requests.get(base, params={"call": "n1old"}, timeout=5).json()
    assert [e["time"] for e in n1["entries"]] == ["230000", "090000"]
    assert n1["total"] == 5   # total is the whole log, not the match count
    assert requests.get(base, params={"call": "NOBODY"}, timeout=5).json()["entries"] == []

    # A cursor pointing at a since-deleted entry still resolves.
    first = requests.get(base, params={"limit": 1}, timeout=5).json()
    requests.delete(f"{base}/{first['entries'][0]['id']}", timeout=5)
    after = requests.get(base, params={"limit": 2, "before": first["next"]}, timeout=5).json()
    assert [e["call"] for e in after["entries"]] == ["N3", "N2MID"]

    _clear(base)


def test_logbook_import_merges_and_dedupes(rest_url):
    base = _base(rest_url)
    _clear(base)
    assert requests.post(base, json=_qso("K1TEST", "20260917", "120000"), timeout=5).status_code == 202

    # Lower-case tags, CRLF, a UTF-8 name whose byte length differs from its
    # character count, one duplicate of the existing QSO, no APP_WFWEB_ID.
    adif = ("Some other logger\r\n<adif_ver:5>3.1.4 <eoh>\r\n"
            "<call:6>K1TEST <qso_date:8>20260917 <time_on:6>120000 <freq:9>14.074000 <mode:3>FT8 <eor>\r\n"
            "<call:5>IK1ZZ <qso_date:8>20200505 <time_on:4>1010 <freq:8>7.074000 <band:3>40M <mode:3>FT8 "
            "<name:5>José <gridsquare:4>JN45 <eor>\r\n").encode("utf-8")
    imported = requests.post(f"{base}/adif", data=adif, timeout=5)
    assert imported.status_code == 202
    assert imported.json() == {"added": 1, "total": 2}

    entries = requests.get(base, timeout=5).json()["entries"]
    assert [e["call"] for e in entries] == ["K1TEST", "IK1ZZ"]
    ik1 = entries[1]
    assert ik1["name"] == "José"
    assert ik1["freq"] == 7074000
    assert ik1["theirGrid"] == "JN45"
    assert ik1["id"]

    # Re-importing the same document adds nothing.
    assert requests.post(f"{base}/adif", data=adif, timeout=5).json()["added"] == 0
    _clear(base)
