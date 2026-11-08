"""Compare public Kick paid-gift occurrences with an independently polled Studio ledger.

The reference must subscribe to chatrooms.* and channel_* (kick-gift-reference.mjs).
Ledger rows use {receivedMs, payload: {message: ...}}; observer metadata supplies
startMs/endMs/errors. Capture must be a dedicated profile with only one Kick channel.
This qualifies author and displayed amount, not gift artwork or subscription gifts.
"""
import argparse
import json
import sys
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--reference', required=True, type=Path)
p.add_argument('--capture', required=True, type=Path)
p.add_argument('--out', required=True, type=Path)
a = p.parse_args()

def read(file):
    return json.loads(file.read_text(encoding='utf-8-sig'))

def rows(file):
    return [json.loads(line) for line in file.read_text(encoding='utf-8-sig').splitlines() if line.strip()]

ref = read(a.reference / 'summary.json')
meta = read(a.capture / 'meta.json')
raw_truth = [r for r in rows(a.reference / 'gifts.jsonl') if r['event'] == 'KicksGifted']
truth = []
seen = set()
invalid_reference = 0
for r in raw_truth:
    d = r['data']
    key = d.get('gift_transaction_id')
    if not key or not d.get('sender', {}).get('username') or not isinstance(d.get('gift', {}).get('amount'), (int, float)):
        invalid_reference += 1
        continue
    if key not in seen:
        seen.add(key)
        truth.append(r)
truth.sort(key=lambda r: r['receivedMs'])
captured = [r for r in rows(a.capture / 'captured.jsonl')
            if r['payload']['message'].get('type') == 'kick'
            and r['payload']['message'].get('event') == 'gift']
captured.sort(key=lambda r: r['receivedMs'])
channels = {r['payload']['message'].get('channel') for r in captured}
begin = max(meta['startMs'], ref.get('subscribedAtMs') or ref['startedMs']) + 5000
end = min(meta['endMs'], ref['endedMs']) - 15000
used = set()
matches = {}
for r in truth:
    d = r['data']
    amount = d['gift']['amount']
    label = f'{amount:g} ' + ('KICK' if amount == 1 else 'KICKs')
    # Occurrence matching preserves separate equal gifts by the same person.
    # Independent socket and UI-poll receipt times can lead/lag each other.
    for i, c in enumerate(captured):
        m = c['payload']['message']
        if (i not in used and abs(c['receivedMs'] - r['receivedMs']) <= 5000
                and m.get('chatname', '').casefold() == d['sender']['username'].casefold()
                and m.get('hasDonation') == label):
            used.add(i)
            matches[d['gift_transaction_id']] = c
            break
scored = [r for r in truth if begin <= r['receivedMs'] <= end]
missing = [r['data']['gift_transaction_id'] for r in scored if r['data']['gift_transaction_id'] not in matches]
surplus = [c['payload']['message']['id'] for i, c in enumerate(captured)
           if i not in used and begin <= c['receivedMs'] <= end]
delays = [matches[r['data']['gift_transaction_id']]['receivedMs'] - r['receivedMs']
          for r in scored if r['data']['gift_transaction_id'] in matches]
checks = {
    'reference observer completed': ref.get('complete') is True,
    'reference held both subscriptions': ref['referenceHeld'],
    'reference has no malformed paid gifts': invalid_reference == 0,
    'capture observer has no read errors': not meta.get('errors'),
    'only one captured Kick channel': len(channels) == 1,
    'eligible real paid gifts observed': bool(scored) and end > begin,
    'each eligible gift matches one author and amount': not missing,
    'no duplicate or unexpected captured gift occurrences': not surplus,
}
result = {'checks': checks, 'passed': all(checks.values()), 'startMs': begin, 'endMs': end,
          'expected': len(scored), 'matched': len(scored)-len(missing),
          'missing': len(missing), 'surplus': len(surplus),
          'missingTransactionIds': missing, 'surplusCaptureIds': surplus,
          'maxAbsReceiptDifferenceMs': max(map(abs, delays)) if delays else None,
          'meaning': 'Exactly-once occurrence comparison of displayed author/amount within five seconds; no gift artwork or subscription-gift claim.'}
with a.out.open('x', encoding='utf-8') as f:
    json.dump(result, f, indent=2)
print(json.dumps(result, indent=2))
sys.exit(0 if result['passed'] else 1)
