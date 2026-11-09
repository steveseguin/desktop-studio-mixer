"""Score kick-compare.mjs evidence using platform IDs plus author and displayed text.

Run after the live collector finishes: python native-qt/tools/kick-compare-ids.py FOLDER
Requires the public reference to cover the full window and native IDs on every
captured ordinary comment. Gifts and reward notices without a chat ID have a
separate platform event reference and are excluded from ChatMessageEvent scoring.
Raw chat remains in the collector's ignored evidence directory.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import statistics
import sys
import unicodedata

parser = argparse.ArgumentParser()
parser.add_argument('folder', type=Path)
args = parser.parse_args()


def read(name):
    return json.loads((args.folder / name).read_text(encoding='utf-8-sig'))


def normal(value):
    return ' '.join(unicodedata.normalize('NFC', str(value or ''))
                    .replace('\u200b', '').replace('\ufeff', '').split())


def content_key(user, text):
    return normal(user).casefold(), normal(text)


summary, window, truth, captured = map(read, ['summary.json', 'window.json', 'truth.json', 'captured.json'])
begin, end = window['scoredFrom'], window['scoredUntil']
deleted, banned = dict(window['deleted']), dict(window['banned'])
by_id = defaultdict(list)
chat = [m for m in captured if m.get('event') != 'gift'
        and not (m.get('event') == 'reward' and not m.get('meta', {}).get('messageId'))]
missing_identity = 0
for message in chat:
    identity = message.get('meta', {}).get('messageId', '')
    if not re.fullmatch(r'[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}', identity, re.I):
        missing_identity += 1
    else:
        by_id[identity].append(message)

matched = missing = wrong = duplicates = removed = 0
delays = []
for reference in truth:
    if not begin <= reference['at'] <= end:
        continue
    rows = by_id.get(reference['id'], [])
    if not rows:
        times = ([event['at'] for event in window['moderationEvents']
                  if event.get('kind') == 'delete' and event.get('id') == reference['id']
                  or event.get('kind') == 'ban' and event.get('user') == reference['user']]
                 if 'moderationEvents' in window else [deleted.get(reference['id'], 0), banned.get(reference['user'], 0)])
        if any(0 <= at - reference['at'] <= 5000 for at in times):
            removed += 1
        else:
            missing += 1
        continue
    duplicates += len(rows) - 1
    message = rows[0]
    delays.append(message['seenMs'] - reference['at'])
    expected = re.sub(r'\[emote:\d+:([^\]]+)\]', lambda m: m[1], reference.get('text') or '')
    if content_key(message['chatname'], message['chatmessage']) == content_key(reference['user'], expected):
        matched += 1
    else:
        wrong += 1

reference_ids = {r['id'] for r in truth}
unexpected = sum(begin <= m['seenMs'] <= end and identity not in reference_ids
                 for identity, rows in by_id.items() for m in rows)
eligible = matched + wrong + missing
late = sum(abs(value) > 5000 for value in delays)
checks = {
    'reference covers the completed observation': summary.get('referenceHeld') is True and not summary.get('error'),
    'nonempty scored window': end > begin and eligible > 0,
    'capture contains one Kick channel': bool(chat) and all(m.get('type') == 'kick' for m in chat)
        and len({m.get('channel') for m in chat}) == 1,
    'all captured ordinary comments carry native IDs': missing_identity == 0,
    'each eligible ID has the original author and text': missing == 0 and wrong == 0,
    'no duplicate or unexpected IDs': duplicates == 0 and unexpected == 0,
    'matched receipt differences stay within five seconds': late == 0,
}
result = {'passed': all(checks.values()), 'checks': checks, 'eligible': eligible,
          'matchedIdAndText': matched, 'missingIds': missing, 'wrongTextOrAuthorForId': wrong,
          'duplicateIds': duplicates, 'unexpectedIds': unexpected, 'removedByModerators': removed,
          'separateSupportEvents': len(captured) - len(chat),
          'capturedChatRows': len(chat), 'rowsWithoutNativeId': missing_identity,
          'lateOccurrences': late, 'medianReceiptMs': statistics.median(delays) if delays else None,
          'maxAbsReceiptMs': max(map(abs, delays)) if delays else None}
with (args.folder / 'native-id-summary.json').open('w', encoding='utf-8') as file:
    json.dump(result, file, indent=2)
print(json.dumps(result))
sys.exit(0 if result['passed'] else 1)
