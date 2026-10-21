// The 2026-10-20 "yes" turn from the live app, as the host sent it: the
// person answered "yes" to switching printers and the model made seventeen
// tool calls before it spoke. Strings are the saved chat's own. The preset
// rows carry the location the host now records; the original run predates it.

import { ChangeInfo, ToolActivityInfo, WireMessage } from '../bridge/protocol';

export const yesTurnMessages: WireMessage[] = [
  {
    "id": "m-2",
    "role": "assistant",
    "state": "complete",
    "text": "The arrow headband is about **254 × 252 mm**, which is too big for your selected **Bambu Lab A1 mini** (its bed is 180 × 180 mm). Moving or turning it won't help. The file was made for a Bambu Lab P1S, which has a bigger bed.\n\nOf the printers you've set up, it fits on your **Bambu Lab A1** (256 mm bed) or your **Qidi X-Plus 4**. The A1 is closest to what the file was designed for. If you switch, the PLA and print settings would also change to their A1 versions, since the current ones are made for the A1 mini.\n\nShould I switch the setup to your Bambu Lab A1 and place the headband on its bed?",
    "attempt": 1,
    "createdAt": "2026-10-20T22:47:13Z",
    "inReplyTo": "m-1"
  },
  {
    "id": "m-3",
    "role": "user",
    "state": "complete",
    "text": "yes",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:06Z"
  },
  {
    "id": "m-4",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:06Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-5",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:10Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-6",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:10Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-7",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:12Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-8",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:15Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-9",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:15Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-10",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:19Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-11",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:22Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-12",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:24Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-13",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:27Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-14",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:31Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-15",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:32Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-16",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:38Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-17",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:38Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-18",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:45Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-19",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:48Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-20",
    "role": "assistant",
    "state": "complete",
    "text": "",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:50Z",
    "inReplyTo": "m-3"
  },
  {
    "id": "m-21",
    "role": "assistant",
    "state": "complete",
    "text": "I switched the printer to the **Bambu Lab A1**, but I couldn't get the headband onto its bed. The plate now shows no printable objects, so nothing can be sliced yet.\n\n**What changed:**\n- **Printer:** Bambu Lab A1 (256 mm bed).\n- **Filament:** Bambu PLA Basic, replacing PLA Matte. Both are PLA, so the material is the same.\n- **Print settings:** 0.20 mm Standard for the A1.\n- **Plate:** Smooth High Temp Plate, replacing Textured Cool Plate.\n\n**Why it doesn't fit:** The headband is about 254 × 252 mm. That is only about 1 mm smaller than the A1's bed on each side. I placed it in the centre and tried turning it, but the software still says it sits outside the printable area. Auto-arrange couldn't place it either.\n\n**Options:**\n1. **Use the Qidi X-Plus 4** (305 mm bed). It fits easily, and I can set it up with matching PLA settings.\n2. **Scale the headband down** to about 95% (around 240 mm). It would fit the A1, but it would be slightly smaller than designed.\n3. **Stay on the A1 and cut the headband into two halves** and print them separately, then glue them together.\n\nWhich would you like?",
    "attempt": 1,
    "createdAt": "2026-10-20T22:48:50Z",
    "inReplyTo": "m-3"
  }
];

export const yesTurnActivities: ToolActivityInfo[] = [
  {
    "actionId": "t-1",
    "correlationId": "m-4",
    "server": "jusprin-native",
    "tool": "presets_list",
    "title": "List printer, filament, or process presets",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 9,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-2",
    "correlationId": "m-5",
    "server": "jusprin-native",
    "tool": "presets_list",
    "title": "List printer, filament, or process presets",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 9,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-3",
    "correlationId": "m-6",
    "server": "jusprin-native",
    "tool": "printer_setup_preview",
    "title": "Preview a printer setup",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 9,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-4",
    "correlationId": "m-7",
    "server": "jusprin-native",
    "tool": "presets_list",
    "title": "List printer, filament, or process presets",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 9,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-5",
    "correlationId": "m-8",
    "server": "jusprin-native",
    "tool": "presets_list",
    "title": "List printer, filament, or process presets",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 9,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-6",
    "correlationId": "m-9",
    "server": "jusprin-native",
    "tool": "printer_setup",
    "title": "Set up Bambu Lab A1; replaces process 0.20mm Standard @BBL A1M; replaces filament Bambu PLA Matte @BBL A1M; replaces filament Bambu PLA Matte @BBL A1M; replaces filament Bambu PLA Matte @BBL A1M",
    "arguments": {},
    "actionClass": "mutation",
    "sessionId": "2",
    "expectedRevision": 9,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-7",
    "correlationId": "m-10",
    "server": "jusprin-native",
    "tool": "object_place",
    "title": "Place arrow headband.STL: move to 128, 128, drop to the bed",
    "arguments": {},
    "actionClass": "mutation",
    "sessionId": "2",
    "expectedRevision": 10,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-8",
    "correlationId": "m-11",
    "server": "jusprin-native",
    "tool": "object_analyze",
    "title": "Analyze an object",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 12,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-9",
    "correlationId": "m-12",
    "server": "jusprin-native",
    "tool": "view_render",
    "title": "Picture a plate",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 12,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-10",
    "correlationId": "m-13",
    "server": "jusprin-native",
    "tool": "object_place",
    "title": "Place arrow headband.STL: rotate 0/0/-44.946 degrees, move to 128, 128, drop to the bed",
    "arguments": {},
    "actionClass": "mutation",
    "sessionId": "2",
    "expectedRevision": 12,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-11",
    "correlationId": "m-14",
    "server": "jusprin-native",
    "tool": "object_analyze",
    "title": "Analyze an object",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 15,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-12",
    "correlationId": "m-15",
    "server": "jusprin-native",
    "tool": "object_place",
    "title": "Place arrow headband.STL: rotate 0/0/44.946 degrees, move to 128, 128, drop to the bed",
    "arguments": {},
    "actionClass": "mutation",
    "sessionId": "2",
    "expectedRevision": 15,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-13",
    "correlationId": "m-16",
    "server": "jusprin-native",
    "tool": "object_analyze",
    "title": "Analyze an object",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 18,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-14",
    "correlationId": "m-17",
    "server": "jusprin-native",
    "tool": "workspace_inspect",
    "title": "Inspect the live workspace",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 18,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-15",
    "correlationId": "m-18",
    "server": "jusprin-native",
    "tool": "plate_layout",
    "title": "Lay out: arrange Plate 1",
    "arguments": {},
    "actionClass": "mutation",
    "sessionId": "2",
    "expectedRevision": 18,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-16",
    "correlationId": "m-19",
    "server": "jusprin-native",
    "tool": "workspace_inspect",
    "title": "Inspect the live workspace",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 20,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  },
  {
    "actionId": "t-17",
    "correlationId": "m-20",
    "server": "jusprin-native",
    "tool": "object_analyze",
    "title": "Analyze an object",
    "arguments": {},
    "actionClass": "read_only",
    "sessionId": "2",
    "expectedRevision": 20,
    "state": "succeeded",
    "progress": {
      "current": 1,
      "total": 1
    }
  }
];

export const yesTurnChanges: ChangeInfo[] = [
  {
    "seq": 18,
    "createdAt": "2026-10-20T22:48:19Z",
    "kind": "preset",
    "actor": "agent",
    "label": "0.20mm Standard @BBL A1",
    "conversationId": "c-1",
    "afterId": "t-6",
    "location": "process"
  },
  {
    "seq": 19,
    "createdAt": "2026-10-20T22:48:19Z",
    "kind": "preset",
    "actor": "agent",
    "label": "Bambu PLA Basic @BBL A1",
    "conversationId": "c-1",
    "afterId": "t-6",
    "location": "filament"
  },
  {
    "seq": 20,
    "createdAt": "2026-10-20T22:48:19Z",
    "kind": "preset",
    "actor": "agent",
    "label": "Bambu Lab A1",
    "conversationId": "c-1",
    "afterId": "t-6",
    "location": "printer"
  },
  {
    "seq": 23,
    "createdAt": "2026-10-20T22:48:22Z",
    "kind": "step",
    "actor": "agent",
    "label": "Place object",
    "conversationId": "c-1",
    "afterId": "t-7"
  },
  {
    "seq": 30,
    "createdAt": "2026-10-20T22:48:31Z",
    "kind": "step",
    "actor": "agent",
    "label": "Place object",
    "conversationId": "c-1",
    "afterId": "t-10"
  },
  {
    "seq": 35,
    "createdAt": "2026-10-20T22:48:38Z",
    "kind": "step",
    "actor": "agent",
    "label": "Place object",
    "conversationId": "c-1",
    "afterId": "t-12"
  },
  {
    "seq": 42,
    "createdAt": "2026-10-20T22:48:48Z",
    "kind": "step",
    "actor": "agent",
    "label": "Lay out plates",
    "conversationId": "c-1",
    "afterId": "t-15"
  }
];
