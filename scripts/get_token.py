"""
One-time Spotify refresh token generator.

Run this locally once to authorize your own Spotify account and get a
refresh token. Paste the printed refresh token into your secrets.h.

Setup:
    pip install requests
    Edit CLIENT_ID / CLIENT_SECRET below (from your Spotify Developer app)

Usage:
    python3 get_token.py
"""

import base64
import http.server
import socketserver
import urllib.parse
import webbrowser
import requests

CLIENT_ID = "YOUR_CLIENT_ID"
CLIENT_SECRET = "YOUR_CLIENT_SECRET"
REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPE = "user-read-currently-playing user-read-playback-state"

auth_url = (
    "https://accounts.spotify.com/authorize?"
    + urllib.parse.urlencode({
        "client_id": CLIENT_ID,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "scope": SCOPE,
    })
)

auth_code = {"code": None}


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        query = urllib.parse.urlparse(self.path).query
        params = urllib.parse.parse_qs(query)
        auth_code["code"] = params.get("code", [None])[0]
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"You can close this tab and go back to the terminal.")


print("Opening browser for Spotify login...")
webbrowser.open(auth_url)

with socketserver.TCPServer(("127.0.0.1", 8888), Handler) as httpd:
    httpd.handle_request()

code = auth_code["code"]
if not code:
    print("No code received. Something went wrong.")
    exit(1)

token_url = "https://accounts.spotify.com/api/token"
auth_header = base64.b64encode(f"{CLIENT_ID}:{CLIENT_SECRET}".encode()).decode()

resp = requests.post(
    token_url,
    data={
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": REDIRECT_URI,
    },
    headers={"Authorization": f"Basic {auth_header}"},
)

data = resp.json()
print("\n--- RESULT ---")
print("Access token:", data.get("access_token"))
print("Refresh token:", data.get("refresh_token"))
print("--------------\n")
print("Save the refresh token — you'll paste it into secrets.h")
