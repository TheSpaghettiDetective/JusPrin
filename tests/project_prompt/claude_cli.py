"""The claude-cli backend of run_prompt_tests.py: one recorded step answered by
the locally installed Claude Code CLI in headless mode, on the developer's
Claude subscription instead of a paid API key.

How a recorded request reaches the model:
  - instructions: the CLI's --system-prompt. The CLI puts two lines of its own
    before it (a billing header and "You are a Claude agent, built on
    Anthropic's Claude Agent SDK.") and adds a short "# Environment" block
    after the first message (working directory, platform, date, model name).
  - tools: the stub MCP server (claude_mcp_stub.py) serves the case's tools
    with their real descriptions and schemas. The model sees each under the
    CLI's MCP name, mcp__jusprin__<name>; the runner strips the prefix.
  - input: everything before the request's last turn is written as a
    session file the CLI resumes (--resume <file>), so earlier messages,
    calls and results reach the model as real turns, tool calls and tool
    results. The last turn's message (the person's words, the <workspace>
    block, the plate's picture) goes in on stdin as one stream-json user
    message. A developer-role item becomes a user text block wrapped in
    <developer_message> tags: the Messages API has no developer role.
  - a point recorded mid-turn (the request ends on a tool result) resumes a
    session whose last call is marked deferred: the CLI runs that call
    through the stub, which returns the recorded result (and the app's
    developer notes after it, as further text blocks of the same result),
    and then asks the model, adding its own "Continue from where you left
    off." after the result.
Every later call goes through the stub to the runner, which answers it with
answer() or ends the run by closing the process.
  - web search: the app offers its provider's own search on every reply
    request, so a run offers the CLI's WebSearch, the one built-in tool it
    has. The CLI runs a search itself; its query, the pages it found and the
    result the model read come back as a web_search output item, and the
    model goes on. A search in the history is resumed as the CLI's own call
    and result, as the app sends a response's items back.

Nothing from the developer's own Claude Code setup reaches the model:
--setting-sources "" (no settings, hooks or output style), no CLAUDE.md and no
auto-memory (environment), --disable-slash-commands (no skills),
--strict-mcp-config (only the stub), --tools "WebSearch" ("" for the judge:
no other built-in tool), a minimal environment, and an empty working
directory."""

import json
import os
import pathlib
import queue
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import uuid

HERE = pathlib.Path(__file__).resolve().parent
STUB = HERE / "claude_mcp_stub.py"
SERVER = "jusprin"
PREFIX = f"mcp__{SERVER}__"
# The tool the CLI adds for --json-schema.
STRUCTURED_OUTPUT = "StructuredOutput"
# The CLI's own search, which a run offers as the app offers its provider's.
WEB_SEARCH = "WebSearch"
# For an offline check against a fake Messages endpoint: its URL. The CLI is
# then given a dummy API key instead of the subscription's login.
TEST_ENDPOINT_ENV = "JUSPRIN_CLAUDE_CLI_TEST_ENDPOINT"
RUN_TIMEOUT = 900  # seconds for one run, every response included


def environment():
    """A minimal environment: the CLI reads the subscription's login from the
    user's keychain or config, never an API key or a parent session's."""
    env = {key: os.environ[key] for key in ("HOME", "USER", "LOGNAME", "PATH", "LANG", "TMPDIR", "SHELL", "TERM")
           if key in os.environ}
    env.update({"CLAUDE_CODE_DISABLE_CLAUDE_MDS": "1", "CLAUDE_CODE_DISABLE_AUTO_MEMORY": "1",
                "DISABLE_AUTOUPDATER": "1", "MCP_TOOL_TIMEOUT": str(RUN_TIMEOUT * 1000)})
    if os.environ.get(TEST_ENDPOINT_ENV):
        env.update({"ANTHROPIC_BASE_URL": os.environ[TEST_ENDPOINT_ENV], "ANTHROPIC_API_KEY": "sk-fake-for-tests"})
    return env


def isolation_args(tools=""):
    return ["--tools", tools, "--strict-mcp-config", "--setting-sources", "", "--disable-slash-commands",
            "--no-session-persistence"]


def web_search_line(item):
    """A search the model ran, as a line of a transcript: a provider's own
    search item (OpenRouter's openrouter:web_search, OpenAI's
    web_search_call) or the CLI's (web_search, from output_items). None for
    any other item."""
    if not str(item.get("type", "")).endswith(("web_search", "web_search_call")):
        return None
    action = item.get("action") or {}
    if item.get("result"):  # the CLI's: what the model read, its summary included
        return f"web search: {action.get('query', '')} -> {item['result']}"
    pages = [source["url"] for source in action.get("sources") or [] if source.get("url")]
    return f"web search: {action.get('query', '')}" + (" -> " + ", ".join(pages) if pages else "")


def check_auth(init):
    """Refuses a run billed to anything but the subscription's login."""
    source = init.get("apiKeySource")
    if source != "none" and not os.environ.get(TEST_ENDPOINT_ENV):
        raise RuntimeError(f"the Claude CLI would bill {source}, not the subscription; unset it or log in with "
                           "`claude auth login`")


def find_claude():
    claude = shutil.which("claude")
    if claude is None:
        sys.exit("the claude-cli backend needs the Claude Code CLI (`claude`) on PATH")
    return claude


# --- The recorded request as a session ----------------------------------------


def developer_block(text):
    return {"type": "text", "text": f"<developer_message>\n{text}\n</developer_message>"}


def user_blocks(item):
    """A user or developer item of the Responses API as Messages content."""
    if item["role"] == "developer":
        content = item["content"]
        return [developer_block(content if isinstance(content, str) else
                                "\n\n".join(part.get("text", "") for part in content))]
    content = item["content"]
    if isinstance(content, str):
        return [{"type": "text", "text": content}]
    blocks = []
    for part in content:
        if part["type"] == "input_text":
            blocks.append({"type": "text", "text": part["text"]})
        elif part["type"] == "input_image":
            header, data = part["image_url"].split(",", 1)  # data:image/png;base64,...
            blocks.append({"type": "image", "source": {"type": "base64", "media_type": header[5:].split(";")[0],
                                                       "data": data}})
        else:
            raise ValueError(f"unsupported input part {part['type']}")
    return blocks


def messages(items):
    """Responses API input items as Messages API messages, consecutive items
    of one role in one message."""
    out, searches = [], 0

    def push(role, blocks):
        if out and out[-1]["role"] == role:
            out[-1]["content"] += blocks
        else:
            out.append({"role": role, "content": list(blocks)})

    for item in items:
        kind = item.get("type")
        if kind == "function_call":
            push("assistant", [{"type": "tool_use", "id": item["call_id"], "name": PREFIX + item["name"],
                                "input": json.loads(item.get("arguments") or "{}")}])
        elif kind == "function_call_output":
            push("user", [{"type": "tool_result", "tool_use_id": item["call_id"], "content": item["output"]}])
        elif item.get("role") in ("user", "developer"):
            push("user", user_blocks(item))
        elif item.get("role") == "assistant":
            content = item["content"]
            text = content if isinstance(content, str) else "".join(part.get("text", "") for part in content)
            push("assistant", [{"type": "text", "text": text}])
        elif web_search_line(item):
            # Replayed as the app replays a response's items: here as the
            # CLI's own call and the result the model read.
            action = item.get("action") or {}
            searches += 1
            push("assistant", [{"type": "tool_use", "id": f"toolu_search_{searches}", "name": WEB_SEARCH,
                                "input": {"query": action.get("query", "")}}])
            push("user", [{"type": "tool_result", "tool_use_id": f"toolu_search_{searches}",
                           "content": item.get("result") or web_search_line(item)}])
        elif kind == "reasoning":
            continue  # the model's own
        else:
            raise ValueError(f"unsupported input item {kind or item.get('role')}")
    return out


def split(items):
    """(history, last message, deferred call) for a recorded request.
    A request that ends on the person's message (with the app's notes after
    it): the history is everything before that message. One that ends
    mid-turn on a tool result: the history runs to the last call, which is
    deferred, and its result and the notes after it are the deferred answer."""
    end = len(items)
    while end and items[end - 1].get("role") == "developer":
        end -= 1
    if end and items[end - 1].get("type") == "function_call_output":
        output, call = items[end - 1], items[end - 2]
        if call.get("type") != "function_call" or call["call_id"] != output["call_id"] or \
                items[end - 3].get("type") == "function_call":
            raise ValueError("a mid-turn point must end on one call and its result")
        notes = [block["text"] for item in items[end:] for block in user_blocks(item)]
        return items[:end - 1], None, (call, [output["output"]] + notes)
    start = end
    while start and items[start - 1].get("role") in ("user", "developer"):
        start -= 1
    return items[:start], items[start:], None


def session_file(path, history, cwd, deferred):
    """A session the CLI can resume: the history as a chain of entries, and
    the deferred call's marker when the point is mid-turn."""
    session, parent, lines = str(uuid.uuid4()), None, []

    def add(kind, **fields):
        nonlocal parent
        entry = {"parentUuid": parent, "isSidechain": False, "userType": "external", "cwd": str(cwd),
                 "sessionId": session, "version": "2.1.283", "type": kind, "uuid": str(uuid.uuid4()),
                 "timestamp": "2026-10-19T00:00:00.000Z", **fields}
        lines.append(json.dumps(entry, ensure_ascii=False))
        parent = entry["uuid"]

    for index, message in enumerate(messages(history)):
        if message["role"] == "assistant":
            add("assistant", message={"id": f"msg_recorded_{index}", "type": "message", "role": "assistant",
                                      "model": "recorded", "content": message["content"],
                                      "stop_reason": "tool_use" if message["content"][-1]["type"] == "tool_use"
                                      else "end_turn", "usage": {"input_tokens": 0, "output_tokens": 0}})
        else:
            add("user", message=message)
    if deferred:
        call = deferred[0]
        add("attachment", attachment={"type": "hook_deferred_tool", "toolUseID": call["call_id"],
                                      "toolName": PREFIX + call["name"],
                                      "toolInput": json.loads(call.get("arguments") or "{}"),
                                      "hookName": "settings", "hookEvent": "PreToolUse", "permissionMode": "default"})
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def key(name, arguments):
    return name + " " + json.dumps(arguments, sort_keys=True, separators=(",", ":"))


# --- One run ------------------------------------------------------------------


class Run:
    """The CLI answering one recorded request, response by response.
    next_response() returns the model's next response as Responses API
    output items; answer() gives the results of its calls, and the CLI asks
    the model again. close() ends it, whatever it is doing."""

    def __init__(self, instructions, tools, items, model, effort, keep=None):
        self.directory = pathlib.Path(tempfile.mkdtemp(prefix="jp-cli-", dir="/tmp"))  # a short socket path
        self.keep = keep
        self.events = queue.Queue()
        self.answers, self.answers_lock, self.closed = {}, threading.Condition(), False
        self.usage = []
        self.started = time.monotonic()
        cwd = self.directory / "cwd"
        cwd.mkdir()
        (self.directory / "tools.json").write_text(json.dumps(tools), encoding="utf-8")
        self.socket_path = str(self.directory / "s")
        (self.directory / "mcp.json").write_text(json.dumps({"mcpServers": {SERVER: {
            "command": sys.executable, "args": [str(STUB), str(self.directory / "tools.json"), self.socket_path]}}}))
        history, last, deferred = split(items)
        if deferred:
            self.provide([(deferred[0]["name"], json.loads(deferred[0].get("arguments") or "{}"), deferred[1])])
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listener.bind(self.socket_path)
        self.listener.listen()
        threading.Thread(target=self.serve, daemon=True).start()

        command = [find_claude(), "-p", "--model", model, "--effort", effort, "--system-prompt", instructions,
                   "--mcp-config", str(self.directory / "mcp.json"), "--allowedTools", f"mcp__{SERVER},{WEB_SEARCH}",
                   "--output-format", "stream-json", "--verbose", "--include-partial-messages"] + \
                  isolation_args(WEB_SEARCH)
        if history or deferred:
            session = self.directory / "session.jsonl"
            session_file(session, history, cwd, deferred)
            command += ["--resume", str(session)]
        stdin = None
        if last:
            command += ["--input-format", "stream-json"]
            stdin = json.dumps({"type": "user", "message": {"role": "user", "content": messages(last)[0]["content"]}},
                               ensure_ascii=False) + "\n"
        self.log = open(self.directory / "stream.jsonl", "w", encoding="utf-8")
        self.stderr = open(self.directory / "stderr.txt", "w", encoding="utf-8")
        self.process = subprocess.Popen(command, cwd=cwd, env=environment(), stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=self.stderr, encoding="utf-8",
                                        start_new_session=True)
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()
        if stdin:
            self.process.stdin.write(stdin)
        self.process.stdin.close()

    # The stub's calls, each on its own connection.
    def serve(self):
        while True:
            try:
                connection, _ = self.listener.accept()
            except OSError:
                return
            threading.Thread(target=self.handle, args=(connection,), daemon=True).start()

    def handle(self, connection):
        with connection:
            call = json.loads(connection.makefile("r", encoding="utf-8").readline())
            wanted = key(call["name"], call["arguments"])
            with self.answers_lock:
                while not self.answers.get(wanted) and not self.closed:
                    self.answers_lock.wait()
                if self.closed:
                    return  # the run ended on this call: it gets no result
                output = self.answers[wanted].pop(0)
            connection.sendall((json.dumps({"output": output}) + "\n").encode())

    def provide(self, answers):
        with self.answers_lock:
            for name, arguments, output in answers:
                self.answers.setdefault(key(name, arguments), []).append(output)
            self.answers_lock.notify_all()

    def read(self):
        for line in self.process.stdout:
            self.log.write(line)
            try:
                self.events.put(json.loads(line))
            except json.JSONDecodeError:
                continue
        self.events.put(None)

    def next_response(self):
        """The model's next response that ends on a reply or on calls to the
        stub. A message whose only calls are web searches does not end it:
        the CLI runs them and asks the model again, and the searches join the
        output in order. Its usage is that of every message in it."""
        blocks, usage, start, started_output = [], {}, 0, 0
        while True:
            remaining = RUN_TIMEOUT - (time.monotonic() - self.started)
            try:
                event = self.events.get(timeout=max(remaining, 0.1))
            except queue.Empty:
                raise RuntimeError(f"no response from the Claude CLI in {RUN_TIMEOUT} s")
            if event is None:
                raise RuntimeError("the Claude CLI exited without a response: " + self.stderr_tail())
            kind = event.get("type")
            if kind == "system" and event.get("subtype") == "init":
                check_auth(event)
                if not any(server["name"] == SERVER and server["status"] == "connected"
                           for server in event.get("mcp_servers", [])):
                    raise RuntimeError(f"the tool stub did not connect: {event.get('mcp_servers')}")
            elif kind == "assistant" and not event.get("parent_tool_use_id"):
                blocks += event["message"]["content"]
            elif kind == "user" and isinstance(event.get("tool_use_result"), dict) and \
                    "query" in event["tool_use_result"]:
                # The result the model read; with two searches at once, the
                # one that names this query.
                search = event["tool_use_result"]
                reads = [block["content"] for block in event["message"]["content"] if block.get("type") == "tool_result"]
                reads = [read if isinstance(read, str) else json.dumps(read, ensure_ascii=False) for read in reads]
                read = next((read for read in reads if f'query: "{search["query"]}"' in read), reads[0] if reads else "")
                blocks.append({"type": "web_search", **search, "read": read})
            elif kind == "stream_event":
                raw = event["event"]
                if raw["type"] == "message_start":
                    start = len(blocks)
                    for name, value in (raw["message"].get("usage") or {}).items():
                        if isinstance(value, int):
                            usage[name] = usage.get(name, 0) + value
                    started_output = usage.get("output_tokens", 0)
                elif raw["type"] == "message_delta":
                    usage["output_tokens"] = started_output + (raw.get("usage") or {}).get("output_tokens", 0)
                elif raw["type"] == "message_stop":
                    calls = [block for block in blocks[start:] if block["type"] == "tool_use"]
                    if calls and all(block["name"] == WEB_SEARCH for block in calls):
                        continue
                    self.usage.append(usage)
                    return output_items(blocks), usage
            elif kind == "result":
                if event.get("is_error"):
                    raise RuntimeError("the Claude CLI failed: " + str(event.get("result"))[:500])
                raise RuntimeError("the Claude CLI finished without a response")

    def answer(self, calls, results):
        self.provide([(call["name"], json.loads(call.get("arguments") or "{}"), result)
                      for call, result in zip(calls, results)])

    def stderr_tail(self):
        try:
            return (self.directory / "stderr.txt").read_text(errors="replace")[-500:]
        except OSError:
            return ""

    def close(self):
        with self.answers_lock:
            self.closed = True
            self.answers_lock.notify_all()
        if self.process.poll() is None:
            try:
                os.killpg(self.process.pid, signal.SIGTERM)
                self.process.wait(timeout=10)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                try:
                    os.killpg(self.process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        self.reader.join(timeout=10)
        self.process.stdout.close()
        self.listener.close()
        self.log.close()
        self.stderr.close()
        if self.keep:
            target = pathlib.Path(self.keep) / self.directory.name
            shutil.copytree(self.directory, target, ignore=shutil.ignore_patterns("s"))
        shutil.rmtree(self.directory, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def output_items(blocks):
    """The CLI's content blocks as the Responses API output items Step reads.
    Thinking blocks are the model's own and are left out, as reasoning items
    are. A web search is the CLI's own call: its result, with the query and
    the pages found, becomes a web_search item, shaped as OpenRouter's, with
    the result the model read."""
    items = []
    for block in blocks:
        if block["type"] == "text" and block["text"].strip():
            items.append({"type": "message", "role": "assistant",
                          "content": [{"type": "output_text", "text": block["text"]}]})
        elif block["type"] == "tool_use" and block["name"] != WEB_SEARCH:
            name = block["name"][len(PREFIX):] if block["name"].startswith(PREFIX) else block["name"]
            items.append({"type": "function_call", "call_id": block["id"], "name": name,
                          "arguments": json.dumps(block.get("input") or {}, ensure_ascii=False)})
        elif block["type"] == "web_search":
            pages = [page for result in block.get("results") or [] if isinstance(result, dict)
                     for page in result.get("content") or [] if isinstance(page, dict)]
            items.append({"type": "web_search", "result": block.get("read", ""),
                          "action": {"query": block["query"],
                                     "sources": [{"url": page.get("url"), "title": page.get("title")} for page in pages]}})
    return items


# --- The judge ----------------------------------------------------------------


def judge(instructions, prompt, schema, model, effort):
    """One structured answer from the CLI (--json-schema), with no tools.
    Returns (the answer, its usage)."""
    with tempfile.TemporaryDirectory(prefix="jp-judge-", dir="/tmp") as directory:
        command = [find_claude(), "-p", "--model", model, "--effort", effort, "--system-prompt", instructions,
                   "--json-schema", json.dumps(schema), "--output-format", "json", "--verbose"] + \
                  isolation_args()
        result = subprocess.run(command, cwd=directory, env=environment(), input=prompt, capture_output=True,
                                encoding="utf-8", timeout=RUN_TIMEOUT)
    events = json.loads(result.stdout) if result.stdout.strip().startswith("[") else \
        [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    if isinstance(events, dict):
        events = [events]
    init = next((event for event in events if event.get("subtype") == "init"), {})
    check_auth(init)
    final = next((event for event in reversed(events) if event.get("type") == "result"), None)
    if final is None or final.get("is_error") or "structured_output" not in final:
        raise RuntimeError("the Claude CLI judge failed: " + (str(final.get("result")) if final else result.stderr)[:500])
    return final["structured_output"], final.get("usage") or {}
