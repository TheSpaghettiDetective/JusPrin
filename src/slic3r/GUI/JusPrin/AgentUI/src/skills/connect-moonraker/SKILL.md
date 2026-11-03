---
name: connect-moonraker
trigger: connect_moonraker
---

# Connecting a Klipper printer through Moonraker

The rules above still apply. The person is trying to connect this printer. Your job is to guide them until it is connected or they choose to leave it, which they can do at any time. This explains how the connection works, so you can work out what is going on, including in situations it does not mention.

## Look it up before you guess

You have `web_search`. Before you say where something is in Mainsail, Fluidd or their printer's setup, such as where the API key is or how Moonraker decides who needs one, or what a failure might mean, search for it with what they run and their printer's model, and say what you found and that it comes from the web. Guess only what the search does not answer, and say it is a guess. How this app works is not on the web: that is here and in the app's notes.

## How the connection works

Each step depends on the ones before it, so a step that worked says nothing about the ones after it.

1. **Klipper runs the printer, and Moonraker is the program beside it that other apps talk to over the network.** People usually use it through a web page, Mainsail or Fluidd, in a browser. The app talks only to Moonraker.
2. **The person gives an address.** Moonraker's own port, 7125, answers, as in `http://192.168.1.50:7125`. The address of the Mainsail page answered too where that page passed the app's requests on to Moonraker, as a usual Mainsail setup does. A Mainsail page that did not pass them on answered with a web page instead. The address can be an IP address or a name, but a name this computer cannot look up gets no answer at all.
3. **The person types an API key into the app's form, if Moonraker asks for one.** The field can stay empty. A Moonraker set to trust this computer's address let it in without a key. One that did not answered HTTP 401 Unauthorized, the same answer for no key and for a wrong key. You have not seen where Mainsail or Fluidd show the key, so search for where their version shows it before you say, and ask what they see.
4. **The app asks Moonraker for its server information.** The app's note then says the connection is verified. That means Moonraker answered, and nothing more: it answered the same way while Klipper could not reach the printer's board, and while Klipper was not running at all.

## What the tools report

- `not_configured` means no connection has been saved yet. `unknown` means one is saved and has not been tested in this chat.
- `failed` comes with what the app's test was told, in the host's own words where it gave any.

## When a connection fails

What the test was told says which step to suspect. Each of these was seen against a real Moonraker:

- **HTTP 401 Unauthorized.** Moonraker answered and wants an API key, or a different one. The address is right.
- **HTTP 404.** Something answered at that address, but not as Moonraker answers. This came when OctoPrint was chosen for a Moonraker, and when Moonraker was chosen for an OctoPrint.
- **"Could not parse Moonraker server response".** A web server answered with a page. This came from a Mainsail page that did not pass requests on. Moonraker's port, 7125, on the same machine is worth trying.
- **"Connection refused".** The machine at that address answered at once, but nothing was listening on that port. The port may be wrong, or Moonraker may not be running.
- **"Connection timed out", after about 10 seconds.** Nothing answered. This came from an address with no device on it, and also from `https://` in front of a Moonraker that speaks plain `http://`, so despite what the message says about the network, an `https://` address is worth trying with `http://` first.
- **No answer after 30 seconds.** The test was still waiting. This came from a name ending in `.local` that no device answered to. The IP address, which their router or Mainsail may show, avoids looking up the name.

What is worth trying beyond this has not been tested here, so search for what people with the same setup report before you suggest it. Without a search, general knowledge gives: opening the same address in a browser on this computer, checking the printer and this computer are on the same network, or restarting Moonraker. When they want to try again, call `printer_connect` again with the address to use.
