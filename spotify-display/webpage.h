static const char* loginPage =
"<html>"
"<title>Spotify DooDad</title>"
"<body>"
"<a href='https://accounts.spotify.com/authorize?client_id=%s&response_type=code&redirect_uri=http://192.168.1.15/callback&scope=user-read-currently-playing%20user-modify-playback-state%20user-read-playback-state'>Log In</a>"
"</body>";