#!/usr/bin/env python3
"""Does the project chat's model answer a how-to question from the app guide?

Each question in guide_questions.json belongs to one page of docs/guide. A run
sends the question as a chat's first message and passes when the model's reply
tells the person what the question's "answer" says, in the judge's reading. The
run's line also says whether the model read the page's skill
(skills/guide-<area>/SKILL.md) on the way.

The request is the recorded "advice-infill" point with the person's sentence
replaced, so the prompt, the tools and the workspace are the app's, and a
skill_read of any guide page is answered with its skill as checked in: a
question may be answered from a neighbouring page, and the run's line names
the page read when it is not the question's own. Everything else is
run_prompt_tests.py: the same options, backends and judge.

    tests/project_prompt/run_guide_tests.py --backend claude-cli --judge claude-cli --runs 1
    tests/project_prompt/run_guide_tests.py --area print-settings --no-guide ...

--area keeps the questions of one page. --no-guide takes every guide off the
list skill_read carries, which is the app before the guide: the gap between
the two runs is what the guide does.
"""
import copy
import json
import pathlib
import sys

import run_prompt_tests as runner

HERE = pathlib.Path(__file__).resolve().parent
SKILLS = HERE / "../../src/slic3r/GUI/JusPrin/AgentUI/src/skills"
QUESTIONS = json.loads((HERE / "guide_questions.json").read_text(encoding="utf-8"))
POINT = "advice-infill"
GUIDES = sorted(path.parent.name[len("guide-"):] for path in SKILLS.glob("guide-*/SKILL.md"))


def skill_text(area):
    """The text skill_read returns: the skill file after its front matter."""
    return (SKILLS / f"guide-{area}" / "SKILL.md").read_text(encoding="utf-8").split("\n---\n", 1)[1].strip()


def add_case(area, entry, with_guide):
    class GuideQuestion(runner.Case):
        def check(self, step):
            if step.tool:
                return (runner.FAIL if step.changes() else runner.CONTINUE), str(step)
            read = sorted({other for line in self.log if "skill_read" in line
                           for other in GUIDES if f'"guide-{other}"' in line})
            yes, why = self.judged(step, f"Does the assistant's last message tell the person {entry['answer']}?")
            tag = ("[read the guide] " if area in read else
                   f"[read {', '.join('guide-' + other for other in read)} instead] " if read else "[did not read the guide] ")
            return (runner.PASS, tag + str(step)) if yes else (runner.FAIL, tag + "WRONG OR MISSING PLACE: " + why)

    GuideQuestion.name = f"guide-{area}-{entry['id']}"
    GuideQuestion.__doc__ = entry["question"]
    point = copy.deepcopy(runner.REQUESTS[POINT])
    point["body"]["input"][0]["content"][0]["text"] = entry["question"]
    if with_guide:
        recorded = next(a for a in point["answers"] if a["tool"] == "skill_read")
        for other in GUIDES:
            answer = copy.deepcopy(recorded)
            output = json.loads(answer["output"])
            output["result"] = {"name": f"guide-{other}", "text": skill_text(other)}
            answer["arguments"] = {"name": f"guide-{other}"}
            answer["output"] = json.dumps(output, ensure_ascii=False, separators=(",", ":"))
            point["answers"].append(answer)
    runner.REQUESTS[GuideQuestion.name] = point
    runner.CASES[GuideQuestion.name] = GuideQuestion
    return GuideQuestion.name


def take(flag, has_value):
    """Removes this script's own option from the command line and returns it."""
    if flag not in sys.argv:
        return None
    at = sys.argv.index(flag)
    value = sys.argv[at + 1] if has_value else True
    del sys.argv[at:at + (2 if has_value else 1)]
    return value


def main():
    area = take("--area", True)
    with_guide = not take("--no-guide", False)
    if area is not None and area not in QUESTIONS:
        sys.exit(f"no questions for {area}: guide_questions.json has {', '.join(sorted(QUESTIONS))}")
    if not with_guide:
        tool = next(tool for tool in runner.TOOLS if tool.get("name") == "skill_read")
        tool["description"] = "\n".join(line for line in tool["description"].split("\n") if not line.startswith("- guide-"))
    names = [add_case(each, entry, with_guide)
             for each in sorted(QUESTIONS) if area in (None, each) for entry in QUESTIONS[each]]
    if "--case" not in sys.argv:
        for name in names:
            sys.argv += ["--case", name]
    # The runner's default spreads its whole budget over the cases named.
    if "--runs" not in sys.argv:
        sys.argv += ["--runs", "3"]
    runner.main()


if __name__ == "__main__":
    main()
