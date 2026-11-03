---
name: connect-octoprint
trigger: connect_octoprint
---

# Connecting a printer through OctoPrint

The rules above still apply. The person is trying to connect this printer. Your job is to guide them until it is connected or they choose to leave it, which they can do at any time. This explains how the connection works, so you can work out what is going on, including in situations it does not mention.

## Look it up before you guess

You have `web_search`. Before you say where something is in OctoPrint or their printer's setup, such as where the API key is, or what a failure might mean, search for it with their OctoPrint version or printer's model, and say what you found and that it comes from the web. Guess only what the search does not answer, and say it is a guess. How this app works is not on the web: that is here and in the app's notes.

## How the connection works

Each step depends on the ones before it, so a step that worked says nothing about the ones after it.

1. **OctoPrint runs on a computer beside the printer and drives it.** People use it through its web page in a browser. The app talks only to OctoPrint, never to the printer itself.
2. **The person gives the address they open OctoPrint at.** It can be an IP address or a name, but a name this computer cannot look up gets no answer at all.
3. **The person types an API key into the app's form.** The OctoPrint tried here answered without a key as well, but only to refuse: HTTP 403, "You don't have the permission to access the requested resource". A wrong key got HTTP 403 "Invalid API key", so the two can be told apart. You have not seen OctoPrint's settings, so search for where their version shows the key before you say, and ask what they see.
4. **The app asks OctoPrint for its version.** The app's note then says the connection is verified. That means OctoPrint answered and took the key, and nothing more: it answered the same way with no printer attached to it.

## What the tools report

- `not_configured` means no connection has been saved yet. `unknown` means one is saved and has not been tested in this chat.
- `failed` comes with what the app's test was told, in the host's own words where it gave any.

## When a connection fails

What the test was told says which step to suspect. Each of these was seen against a real OctoPrint, or against the same network failures with a Moonraker:

- **HTTP 403 "You don't have the permission…".** OctoPrint answered and got no key. The address is right.
- **HTTP 403 "Invalid API key".** OctoPrint answered and refused the key. The address is right.
- **HTTP 404.** Something answered at that address, but not as OctoPrint answers. This came when Moonraker was chosen for an OctoPrint, and when OctoPrint was chosen for a Moonraker, so a Klipper printer with Mainsail or Fluidd may be what they have.
- **"Connection refused".** The machine at that address answered at once, but nothing was listening on that port. The port may be wrong, or OctoPrint may not be running.
- **"Connection timed out", after about 10 seconds.** Nothing answered. This came from an address with no device on it, and also from `https://` in front of a server that speaks plain `http://`, so despite what the message says about the network, an `https://` address is worth trying with `http://` first.
- **No answer after 30 seconds.** The test was still waiting. This came from a name ending in `.local` that no device answered to. The IP address avoids looking up the name.

What is worth trying beyond this has not been tested here, so search for what people with the same setup report before you suggest it. Without a search, general knowledge gives: opening the same address in a browser on this computer, checking the OctoPrint computer and this one are on the same network, or restarting OctoPrint. When they want to try again, call `printer_connect` again with the address to use.
