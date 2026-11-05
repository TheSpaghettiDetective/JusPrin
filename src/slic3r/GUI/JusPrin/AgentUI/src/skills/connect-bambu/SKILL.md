---
name: connect-bambu
trigger: connect_bambu
---

# Connecting a Bambu Lab printer

The rules above still apply. The app adds this when a Bambu Lab printer is added, and when a chat about one opens, so it does not mean the person wants to connect: many never do. Start connecting only once they say they want to. Right after a printer is added, unless they have already said whether they want it connected, offer it once: end that reply with exactly "Want to connect it so you can send prints straight to it?" and then "Choices: Connect it | Not now". When they have already asked to connect it, go straight on.

Once they want to connect, your job is to guide them until it is connected or they choose to leave it, which they can do at any time. This explains how the connection works, so you can work out what is going on, including in situations it does not mention.

## How the connection works

Each step depends on the ones before it, so a step that worked says nothing about the ones after it.

1. **The printer announces itself on the network, and the app listens.** On a real P1S the first announcement took between 8 and 40 seconds after the search began. A printer that is off or asleep is not heard, nor is one on a different network from this computer, such as a guest Wi-Fi. A printer with LAN Only off is still heard. Being heard takes one small message getting through, so it does not show that the network is good.
2. **LAN Only has to be on.** The announcement says which mode the printer is in, and the app connects only in LAN mode. It is set on the printer's screen. You have not seen the menus on this model, so search for where it is before you say, and ask what they see.
3. **The person types the access code into the app's form.** The code is shown on the printer's screen. Never ask for it in chat. `printer_bambu_connect` shows the form in the chat, and the call stays open until the person is done with it, so its result is the outcome. The form has three ways out: Connect, "Help me find the code", and Cancel. After Connect the result says whether a code was typed: a form left empty uses the code the app already holds for this printer.
4. **This computer connects to the printer's port 8883 and logs in with the code.** Bambu's network plug-in does this. It reports only how it ended, and it takes about 24 seconds to give up.
5. **The printer sends its status.** The result then says the app connected.

## What the tools report

Once they want to connect, read `printer_connection_status` first if nothing has been read yet, and again whenever the person says something changed. The app's notes report the same things in words.

- `not_configured` means no connection has been verified yet. It stays true until one is, so it is not news.
- Until a printer of this model is heard, `printer_connection_status` listens during the call for up to 30 seconds. `listening` with `candidates` and `seen` empty after that means nothing was heard in those seconds, which is silence and not a finding: on a real P1S the first announcement once came more than two minutes in. It does not mean the printer is off or absent. The search goes on, and when a printer of this model is heard the app posts a note and starts your next turn. Say you are still looking, ask whether the printer is on, and end your message. Reading the status again listens for another 30 seconds.
- `port_taken` means another app on this computer holds the port printers announce on, which may be another slicer, so nothing can be heard whatever the printer is doing. `not_listening` means this app is not listening and takes that port only as it starts. Both need a restart of this app, the first after closing the other one.
- `candidates` lists printers the app knows in LAN mode, heard on the network or saved from an earlier connection. Only an attempt shows whether one answers. With one, if they have asked to connect, call `printer_bambu_connect` with its `deviceId` without asking again. With more than one, ask which, one choice each.
- `printer_bambu_connect` returns once the person is done with its form. When they asked there for help finding the code, or closed the form, nothing was sent, so it says nothing about the printer or the code. The code is shown on the printer's screen; where depends on the model, so search before you say. When they closed the form, ask whether they want to go on.
- `cloud_mode` means the printer was heard with LAN Only off, and it cannot be connected until that is on. Say so with its address. Turning it on is their choice, and they can prepare prints for it either way.
- `linked_elsewhere` means this device is already the saved printer named in `detail`, and a device connects to one saved printer. The way forward is Connect… in that printer's menu on Home.
- `unknown_device` means `printer_bambu_connect` refused the name it was given, and its message says why. When no search has run, call `printer_connection_status`.
- `unavailable` means it cannot connect, and `message` says why. When that is Bambu's network plug-in, the app shows a notice with an Install button under the message. Wait for the app's note that it is installed before reading the status again. The search is `not_started` then, and says nothing about the printer.

## When a connection fails

Say what the app saw in the person's terms, then work out why. A `failed` result says what the plug-in reported and what the app saw at port 8883 just before the attempt and right after it failed. Code -1 does not say why: on a real P1S it came with a wrong access code, with the right code, and with an address where no printer exists. Read the two looks together for which step to suspect:

- **Nothing answered before the attempt.** Step 4 could not work, so the code was never tested and is probably not the problem. Suspect the network between this computer and the printer, or the printer itself, even though it was heard.
- **It answered before, and nothing answered after.** The code may be right or wrong, and the attempt may not be what caused it. A real P1S went quiet like this for minutes at a time, and hours after the last attempt it was still very slow, with a strong Wi-Fi signal and while it went on announcing. Why is not known. On that slow printer a login with the right code was accepted but took 28 seconds, longer than the plug-in waits, so a slow printer may be enough to fail with the right code. A new attempt while it is like this says nothing about the code, so waiting several minutes or restarting the printer comes before trying again.
- **It answered both times.** The network reached the printer, so suspect the login, and a mistyped code is the likeliest. This has not been seen on a real printer, so it is a guess too. Owners on Bambu's forum report that the code changes when LAN Only is switched off and on, so an older code may no longer be right.
- **It turned the connection down at once.** The printer is reachable but not taking logins. This has not been seen on a real printer. A printer that is still starting up is one guess.

What is worth trying for the network or the printer has not been tested on this printer, so search for what owners of this model report before you suggest it. Without a search, general knowledge gives: wait a few minutes, restart the printer, ask what its screen shows about Wi-Fi signal, or bring it closer to the router. When they want to try again, call `printer_bambu_connect` again, which shows the form again.

## When they are stuck

The JusPrin developers can only fix what they hear about, and they have not tested most printers. A person who has tried and wants them to know can report it with `issue_report_open`, which opens a GitHub issue page in their browser with your summary filled in. It files nothing: they read the page, change it if they like, and submit it with their own GitHub account.
