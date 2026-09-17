#!/bin/sh
# Setup-API probe az "Authenticate now" utra: TESZT OAuth-profil mentese -> /api/oauth/start -> URL-ellenorzes -> torles.
# Tokent nem kap, bejelentkezes nincs; a code_challenge/state csak hosszal jelenik meg.
# Hasznalat: sh test_oauth_start.sh 192.168.x.x
B="http://$1"
r=$(curl -s -m 10 -X POST -H "X-CMon: 1" --data-urlencode name=TESZT --data-urlencode transport=oauth \
  --data-urlencode enabled=0 --data-urlencode idx=-1 "$B/api/claude")
echo "save: $r"
idx=$(echo "$r" | sed -n 's/.*"idx":\([0-9]*\).*/\1/p')
[ -n "$idx" ] || exit 1
curl -s -m 5 "$B/api/config" | grep -o '"claude":\[[^]]*\]'
s=$(curl -s -m 10 -X POST -H "X-CMon: 1" --data-urlencode "idx=$idx" "$B/api/oauth/start")
url=$(echo "$s" | python3 -c 'import sys,json;print(json.load(sys.stdin).get("authorizeUrl",""))')
echo "$url" | python3 -c '
import sys, urllib.parse as u
url = sys.stdin.read().strip(); p = u.urlparse(url); q = u.parse_qs(p.query)
show = ("code", "client_id", "response_type", "redirect_uri", "scope", "code_challenge_method")
print(p.scheme + "://" + p.netloc + p.path)
for k, v in q.items(): print(" ", k, "=", v[0] if k in show else "len=%d" % len(v[0]))'
[ -n "$url" ] && curl -s -o /dev/null -w "authorize GET: %{http_code} -> %{redirect_url}\n" "$url" | cut -c1-90
curl -s -m 10 -X POST -H "X-CMon: 1" --data-urlencode "idx=$idx" "$B/api/claude/delete"; echo
curl -s -m 5 "$B/api/config" | grep -o '"claude":\[[^]]*\]'
