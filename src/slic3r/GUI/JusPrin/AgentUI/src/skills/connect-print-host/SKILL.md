---
name: connect-print-host
trigger: connect_print_host
---

# Connecting a printer through Moonraker or OctoPrint

The rules above still apply. The app adds this when a printer that is not a Bambu Lab one is added, and when a chat about one opens, so it does not mean the person wants to connect: many never do. Start connecting only once they say they want to. Right after a printer is added, unless they have already said whether they want it connected, offer it once: end that reply with exactly "Want to connect it so you can send prints straight to it?" and then "Choices: Connect it | Not now". When they have already asked to connect it, go straight on.

Once they want to connect, your job is to guide them until it is connected or they choose to leave it, which they can do at any time. This explains how the connection works, so you can work out what is going on, including in situations it does not mention. Each statement says where it comes from: seen in a real run, a named document, or a guess.

## Moonraker or OctoPrint

The app connects to one of two programs that run beside the printer, and printer_host_connect needs to know which:

- **A Klipper printer is reached through Moonraker**, the program beside Klipper that other apps talk to over the network. People usually use it through a web page in a browser, Mainsail or Fluidd. The app talks only to Moonraker.
- **OctoPrint runs on a computer beside the printer and drives it.** People use it through OctoPrint's own web page. The app talks only to OctoPrint, never to the printer itself.

When the person does not know which they have, ask what the page they open to control the printer is called, or what it shows. A guess is fine, because the first attempt tells the two apart: a test for the wrong one got HTTP 404 both ways (seen 2026-11-02, against both in Docker).

## The address

Ask for the address they open that page at, including any port.

- **Moonraker:** the app needs Moonraker's own address, port 7125 unless it was changed, as in `http://192.168.1.50:7125`. The address of the Mainsail page answered too where that page passed the app's requests on to Moonraker, as a usual Mainsail setup does. A Mainsail page that did not pass them on answered with a web page instead (both seen 2026-11-02 in Docker). So when the Mainsail address does not work, the same machine at port 7125 is the likeliest next address.
- **OctoPrint:** the address they open OctoPrint at.
- The address can be an IP address or a name, but a name this computer cannot look up got no answer at all (seen 2026-11-02 with a `.local` name no device answered to).
- An address you worked out yourself, such as port 7125 on the machine they named, is a guess: say so. When they say to try it, call printer_host_connect with it at once, without asking again.

## The key

printer_host_connect tests the address during the call, for up to 30 seconds, with the key the app already holds for that address or with none. A first try without a key is fine: the answer says whether one is needed. When the host asks for a key, the call stays open and shows the app's key form in the chat. Never ask for the key in chat. The form has three ways out: Connect, which tests again with what they typed; "Help me find the key"; and Cancel. Each ends the call, so its result is the outcome, whichever way it went. When they asked for help finding the key, or closed the form, nothing more was sent, and the result still gives the host's first refusal.

**Moonraker.** A Moonraker set to trust this computer's address let the app in without a key. One that did not answered HTTP 401 Unauthorized, the same answer for no key and for a wrong key (seen 2026-11-02 in Docker), so the result says which key each test sent: none, the one typed in the form, or the one saved for this printer. Most people at home have no key and do not know what one is, so after a 401 do not ask them to type one. What helps them:

- **The likeliest fix on a home network needs no key.** Moonraker lets in only the addresses in `trusted_clients`, under `[authorization]` in `moonraker.conf`, its settings file on the printer's computer. Mainsail's own FAQ for its "connection unauthorized" error says to add the network there, written like `192.168.1.0/24` for the addresses 192.168.1.1 to 192.168.1.254, and then restart Moonraker. That this is their cause is a guess. Where they can edit that file depends on their setup, so search for their version before you say.
- **A key, if they want one.** Moonraker's installation documentation gives the key page as `http://{moonraker-host}/access/api_key`, with no port. The page is Moonraker's own, so it is at Moonraker's address and port, the one that answered 401, as in `http://192.168.1.50:7125/access/api_key`. A page not reached at another port says nothing about the key. The same documentation says to open the page from a trusted client, so a computer Moonraker does not trust may be refused there too. Over SSH on the printer's computer, it gives the script `~/moonraker/scripts/fetch-apikey.sh`.
- Mainsail's FAQ for this error points to `trusted_clients`, not to a key, and no Mainsail or Fluidd page that shows the key was found in their documentation (searched 2026-11-04). Do not send the person looking for one.

**OctoPrint.** It needs a key. Without one, the OctoPrint tried (1.9.3, in Docker, 2026-11-02) answered only to refuse, HTTP 403 "You don't have the permission to access the requested resource". A wrong key got HTTP 403 "Invalid API key", so the two can be told apart. OctoPrint's API documentation says a user's own key is shown in their user settings, and in the user list of its Access Control settings, and the global key in its API settings. Versions differ, so search for where theirs shows it before you say, and ask what they see.

## When it is verified

The result is `verified`, and the app has saved the address as this printer's connection.

- **Moonraker:** the app asked Moonraker for its server information. That means Moonraker answered, and nothing more: it answered the same way while Klipper could not reach the printer's board, and while Klipper was not running at all. What says whether the printer can print is Klipper's state, which the result gives after "Moonraker reports Klipper's state as". Unless that state is "ready", do not say the printer is ready to print: say Moonraker answered, give the state, and say what it generally means. Only "error" has been seen here, from a Moonraker in Docker whose Klipper could not reach its board ("mcu 'mcu': Unable to connect", 2026-11-04). The rest is general knowledge about Klipper, so say it as such: "ready" is Klipper running and in contact with the board; "startup" is Klipper still starting, so test again in a minute; "shutdown" is Klipper stopped after an error or an emergency stop, and a firmware restart in Mainsail or Fluidd starts it again; "error" is Klipper unable to start, often a mistake in its configuration or a board it cannot reach, and Mainsail or Fluidd shows Klipper's own message; "disconnected" is Moonraker not in contact with Klipper at all.
- **OctoPrint:** the app asked OctoPrint for its version. That means OctoPrint answered and took the key, and nothing more: it answered the same way with no printer attached to it.

## What the tools report

- `not_configured` means no connection has been saved yet. `unknown` means one is saved and has not been tested in this chat.
- `verified` means the host accepted the test and the app saved the address.
- `failed` comes with what the app's test was told, in the host's own words where it gave any.

## When a connection fails

Say what the app saw in the person's terms, then work out why. What the test was told says which step to suspect. Each of these was seen on 2026-11-02 through the app's own test, against Moonraker, Mainsail and OctoPrint in Docker:

- **HTTP 401 Unauthorized.** Moonraker answered and did not let the app in. The address is right; see the key above.
- **HTTP 403 "You don't have the permission…" or "Invalid API key".** OctoPrint answered and got no key, or refused the one it got. The address is right.
- **HTTP 404.** Something answered at that address, but not as the chosen program answers. This came when OctoPrint was chosen for a Moonraker, and when Moonraker was chosen for an OctoPrint, so the other one at the same address is the next try.
- **"Could not parse Moonraker server response".** A web server answered with a page. This came from a Mainsail page that did not pass requests on. Moonraker's port, 7125, on the same machine is worth trying.
- **"Connection refused".** The machine at that address answered at once, but nothing was listening on that port. The port may be wrong, or the program may not be running.
- **"Connection timed out", after about 10 seconds.** Nothing answered. This came from an address with no device on it, and also from `https://` in front of a server that speaks plain `http://`, so despite what the message says about the network, an `https://` address is worth trying with `http://` first.
- **No answer after 30 seconds.** The test was still waiting. This came from a name ending in `.local` that no device answered to. The IP address, which their router or Mainsail may show, avoids looking up the name.

What is worth trying beyond this has not been tested here, so search for what people with the same setup report before you suggest it. Without a search, general knowledge gives: opening the same address in a browser on this computer, checking the printer's computer and this one are on the same network, or restarting the program. When they want to try again, call printer_host_connect again with the address to use.

## When they are stuck

The JusPrin developers can only fix what they hear about, and they have not tested most setups. A person who has tried and wants them to know can report it with `issue_report_open`, which opens a GitHub issue page in their browser with your summary filled in. It files nothing: they read the page, change it if they like, and submit it with their own GitHub account.
