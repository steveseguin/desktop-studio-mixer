"""Score saved anonymous IRC input against Studio's captured-message ledger.

Read-only analysis, not an application test by itself. Collect the ledgers from
the packaged application and twitch-truth.mjs over the same continuous interval.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import statistics
import unicodedata

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('folder', type=Path)
parser.add_argument('--channel', required=True)
parser.add_argument('--delay-ms', type=int, required=True,
                    help='Independently observed Twitch non-moderator delay')
args = parser.parse_args()
if not re.fullmatch(r'[a-z0-9_]+', args.channel) or not 0 <= args.delay_ms <= 60000:
    parser.error('Invalid channel or platform delay')
root = args.folder


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def rows(path):
    return [json.loads(line) for line in path.read_text(encoding='utf-8-sig').splitlines() if line]


def normal(value):
    return ' '.join(unicodedata.normalize('NFC', str(value or '')).replace('\u200b', '').replace('\ufeff', '').split())


def identity(row):
    message = row['payload']['message']
    return normal(message.get('chatname')).casefold(), normal(message.get('chatmessage'))


def unescape(value):
    return re.sub(r'\\([snr: ])', lambda m: {'s': ' ', 'n': '\n', 'r': '\r', ':': ';', ' ': ' '}[m[1]], value).replace('\\\\', '\\')


reference = root / 'twitch-reference'
meta = read(reference / f'truth-twitch-{args.channel}-meta.json')
observer = read(root / 'chat-observer/meta.json')
truth = rows(reference / f'truth-twitch-{args.channel}.jsonl')
for item in truth:
    item['kind'] = 'message'
    item['delay'] = args.delay_ms
deletions, notices = [], []
for item in rows(reference / f'truth-twitch-{args.channel}-events.jsonl'):
    raw = item['payload'].get('rawIRC', '')
    parsed = re.match(r'^@([^ ]+) :[^ ]+ (USERNOTICE|CLEARMSG|CLEARCHAT) #([a-z0-9_]+)(?: :(.*))?$', raw)
    if not parsed or parsed[3] != args.channel:
        continue
    tags = dict((part.split('=', 1)[0], unescape(part.split('=', 1)[1])) for part in parsed[1].split(';') if '=' in part)
    if parsed[2] in ('CLEARMSG', 'CLEARCHAT'):
        deletions.append({'at': item['receivedMs'], 'id': tags.get('target-msg-id'),
                          'user': parsed[4] if parsed[2] == 'CLEARCHAT' else None,
                          'clearAll': parsed[2] == 'CLEARCHAT' and not parsed[4]})
    elif parsed[4]:
        kind = tags.get('msg-id', 'unknown')
        # Viewer milestone custom bodies arrive immediately. Subscription bodies
        # use the channel's ordinary chat delay. Keep other bodies in the input
        # ledger; an unsupported category must not silently make a run pass.
        notice = {'receivedMs': item['receivedMs'], 'kind': kind,
                  'delay': 0 if kind == 'viewermilestone' else args.delay_ms,
                  'payload': {'message': {'id': tags.get('id'), 'chatname': tags.get('display-name') or tags.get('login'),
                                          'username': tags.get('login'), 'chatmessage': parsed[4]}, 'tags': tags}}
        notices.append(notice)
truth.extend(notices)
truth.sort(key=lambda item: item['receivedMs'])
capture = [item for item in rows(root / 'chat-observer/captured.jsonl')
           if item['payload']['message'].get('type') == 'twitch' and not item['payload']['message'].get('event')]
begin = max(meta['startMs'], observer['startMs']) + max(30000, args.delay_ms + 5000)
end = min(meta.get('endMs', 0), observer['endMs'] - args.delay_ms) - 15000
eligible = [i for i, item in enumerate(truth) if begin <= item['receivedMs'] <= end]
moderated = {}
for i, item in enumerate(truth):
    if item['kind'] != 'message':
        continue
    message = item['payload']['message']
    hits = [d for d in deletions if item['receivedMs'] <= d['at'] <= item['receivedMs'] + item['delay']
            and (d['id'] == message.get('id') or d['clearAll'] or
                 d['user'] and d['user'].casefold() == str(message.get('username', '')).casefold())]
    if hits:
        moderated[i] = hits
available = defaultdict(list)
for j, item in enumerate(capture):
    available[identity(item)].append(j)
used, matches, late, mapped = set(), {}, set(), []
for limit in (5000, 15000):
    for i, item in enumerate(truth):
        if i in matches or i in moderated:
            continue
        key = identity(item)
        keys = [key]
        body = item['payload']['message'].get('chatmessage', '')
        if item['payload'].get('tags', {}).get('gifs') and body.startswith('[') and body.endswith(']'):
            keys.append((key[0], normal('Image: ' + body)))
        candidates = [j for candidate in keys for j in available[candidate] if j not in used
                      and abs(capture[j]['receivedMs'] - item['receivedMs'] - item['delay']) <= limit]
        if candidates:
            j = min(candidates, key=lambda j: abs(capture[j]['receivedMs'] - item['receivedMs'] - item['delay']))
            used.add(j)
            matches[i] = j
            if identity(capture[j]) != key:
                mapped.append(i)
            if limit > 5000:
                late.add(i)
scored = [i for i in eligible if i not in moderated]
missing = [i for i in scored if i not in matches]
surplus = [j for j, item in enumerate(capture) if j not in used and begin + args.delay_ms <= item['receivedMs'] <= end + args.delay_ms]
delays = [capture[matches[i]]['receivedMs'] - truth[i]['receivedMs'] - truth[i]['delay'] for i in scored if i in matches]
ids = [item['payload']['message'].get('id') for item in truth]
reference_continuous = bool(meta.get('endMs')) and any(interval['startMs'] <= begin and (interval.get('endMs') or 0) >= end for interval in meta['intervals'])
checks = {'continuousCompletedReference': reference_continuous, 'observerNoErrors': not observer['errors'],
          'nonemptyWindow': bool(scored) and end > begin, 'uniqueInputIds': all(ids) and len(ids) == len(set(ids)),
          'oneCapturedChannel': len({item['payload']['message'].get('channel') for item in capture}) == 1,
          'noMissing': not missing, 'noSurplus': not surplus, 'noLate': not set(scored).intersection(late),
          'knownNoticeTypes': all(truth[i]['kind'] in ('message', 'sub', 'resub', 'viewermilestone', 'announcement') for i in scored)}
clips = [i for i in scored if re.search(r'(clips\.twitch\.tv/|twitch\.tv/\w+/clip/)', truth[i]['payload']['message'].get('chatmessage', ''))]
cheers = [i for i in scored if truth[i]['payload'].get('tags', {}).get('bits')]
wrong_cheer_amounts = []
for i in cheers:
    if i not in matches:
        continue
    donation = normal(capture[matches[i]]['payload']['message'].get('hasDonation'))
    amount = re.fullmatch(r'([\d,]+) bits?', donation, re.I)
    if not amount or int(amount[1].replace(',', '')) != int(truth[i]['payload']['tags']['bits']):
        wrong_cheer_amounts.append(i)
checks['correctCheerAmounts'] = not wrong_cheer_amounts
result = {'passed': all(checks.values()), 'checks': checks, 'configuredDelayMs': args.delay_ms,
          'scoredSeconds': (end - begin) / 1000, 'eligible': len(scored), 'matched': len(scored) - len(missing),
          'missing': len(missing), 'surplus': len(surplus), 'late': len(set(scored).intersection(late)),
          'moderatedBeforeDisplay': len(set(eligible).intersection(moderated)),
          'inputKinds': dict(Counter(truth[i]['kind'] for i in scored)),
          'gifMappings': len(set(scored).intersection(mapped)), 'cheers': len(cheers), 'cheersMatched': sum(i in matches for i in cheers), 'wrongCheerAmounts': len(wrong_cheer_amounts),
          'clipInputs': len(clips), 'clipsMatched': sum(i in matches for i in clips),
          'medianBeyondPlatformDelayMs': statistics.median(delays) if delays else None,
          'maxBeyondPlatformDelayMs': max(delays) if delays else None}
details = {'missing': [truth[i] for i in missing], 'surplus': [capture[j] for j in surplus],
           'moderated': [{'input': truth[i], 'deletions': moderated[i]} for i in eligible if i in moderated],
           'cheers': [{'input': truth[i], 'capture': capture[matches[i]] if i in matches else None} for i in cheers],
           'clips': [{'input': truth[i], 'capture': capture[matches[i]] if i in matches else None} for i in clips]}
(root / 'twitch-ledger-comparison.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
(root / 'twitch-ledger-details.json').write_text(json.dumps(details, indent=2), encoding='utf-8')
print(json.dumps(result))
raise SystemExit(0 if result['passed'] else 1)
