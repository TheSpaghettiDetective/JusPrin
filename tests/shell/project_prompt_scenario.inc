    // The project-prompt request recorder (--project-prompt-requests).
    // Included inside class Scenario.
    //
    // tests/project_prompt asks a live model for one step at a known point of
    // a project chat. This plays each of its scripted conversations through
    // the real app up to that point -- the person's words, a canvas issue
    // button, a file opened -- with the model's outputs taken from the script
    // and every tool run for real, and keeps each request the script marks.
    // Only the script's words and calls are written by hand: the tool results,
    // the workspace context, the history and its replay rules are the app's.
    // The page's prompt and the tool list are checked against the files the
    // suite sends and left out of the output.
    //
    // At each kept request the script's "answers" reads are then called in
    // the same turn, and their results kept as the app returned them: the
    // suite answers a model's read with them. That ends the conversation, so
    // each kept request has a pass of its own, replaying the conversation
    // from the start, and no answer reaches another case's history.

    // A person's edit of one or more print settings, through the print tab.
    void prompt_hand_edit(const nlohmann::json& changes)
    {
        DynamicPrintConfig diff;
        for (const auto& [key, value] : changes.items())
            diff.set_deserialize_strict(key, value.get<std::string>());
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(diff);
        wait_for([this] { return !m_plater->is_background_process_update_scheduled(); }, std::chrono::seconds(5));
    }

    // The print settings and filament the first conversation started with.
    std::optional<DynamicPrintConfig> m_prompt_print;
    std::string                       m_prompt_filament;

    // A new project keeps the edited print settings and the selected
    // filament, so an earlier conversation's changes are typed back and its
    // filament selected again.
    void prompt_restore_settings(const std::string& name)
    {
        PresetBundle& bundle = *wxGetApp().preset_bundle;
        if (!m_prompt_print) {
            m_prompt_print    = bundle.prints.get_edited_preset().config;
            m_prompt_filament = bundle.filaments.get_edited_preset().name;
            return;
        }
        const t_config_option_keys changed = bundle.prints.get_edited_preset().config.diff(*m_prompt_print);
        if (!changed.empty()) {
            DynamicPrintConfig back;
            back.apply_only(*m_prompt_print, changed);
            wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(back);
        }
        if (bundle.filaments.get_edited_preset().name != m_prompt_filament)
            SetupCommands::select_filament_preset(*m_plater, 0, m_prompt_filament);
        check(bundle.prints.get_edited_preset().config.diff(*m_prompt_print).empty() &&
                  bundle.filaments.get_edited_preset().name == m_prompt_filament,
              name + "_starts_from_the_first_settings");
    }

    // The project each conversation starts from, with its current plate sliced.
    // `plate_type` (a curr_bed_type value, such as "Cool Plate") is the plate
    // the person selected in the sidebar before the slice. Without one the
    // plate is whatever was selected last: the prepare conversation's printer
    // setup leaves the Textured PEI Plate selected for the conversations
    // after it.
    bool prompt_fixture(const std::string& kind, const std::string& name, const std::string& plate_type)
    {
        using namespace std::chrono_literals;
        prompt_restore_settings(name);
        if (kind == "overhang") {
            // print_issues_scenario's slab on one leg: slicing it warns.
            check(m_plater->new_project(true, true) != wxID_CANCEL, name + "_new_project");
            const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
            const auto strategy = LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence;
            check(m_plater->load_files(std::vector<std::string>{cube}, strategy, false).size() == 1, name + "_cube_loaded");
            check(m_plater->load_files(std::vector<std::string>{write_overhang_stl()}, strategy, false).size() == 1,
                  name + "_overhang_loaded");
            PartPlateList& plates = m_plater->get_partplate_list();
            check(plates.get_plate(0)->add_instance(0, 0, true) == 0, name + "_cube_centred");
            check(plates.get_plate(0)->add_instance(1, 0, true) == 0, name + "_overhang_centred");
            m_plater->canvas3D()->reload_scene(true, true);
            move_object(1, Vec3d(70, 0, 0));
        } else if (kind == "single") {
            // One box on one plate, as a person who just added a model has.
            check(m_plater->new_project(true, true) != wxID_CANCEL, name + "_new_project");
            const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
            const auto strategy = LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence;
            check(m_plater->load_files(std::vector<std::string>{cube}, strategy, false).size() == 1, name + "_cube_loaded");
            check(m_plater->get_partplate_list().get_plate(0)->add_instance(0, 0, true) == 0, name + "_cube_centred");
            m_plater->canvas3D()->reload_scene(true, true);
        } else {
            load_multi_plate_fixture();
        }
        if (!plate_type.empty()) {
            ConfigOptionEnum<BedType> plate;
            check(plate.deserialize(plate_type) && SetupCommands::select_bed_type(*m_plater, int(plate.value)),
                  name + "_plate_selected");
        }
        const auto slice = [this](std::chrono::seconds limit) {
            wait_for([this] { return !m_plater->is_background_process_update_scheduled(); }, 10s);
            installed_shell()->status_row()->request_slice();
            return wait_for([this] {
                const auto* plate = m_plater->get_partplate_list().get_curr_plate();
                return plate && plate->is_slice_result_valid() && !m_plater->is_background_process_slicing();
            }, limit);
        };
        const bool sliced = slice(60s) || slice(120s);
        if (!sliced) {
            issue_monitor().refresh_now();
            for (const auto& issue : issues().issues)
                std::cerr << "HARNESS PROMPT " << name << " fixture issue " << issue.id << " '" << issue.message << "'\n";
        }
        check(sliced, name + "_fixture_sliced");
        installed_shell()->status_row()->request_prepare();
        wait_for([this] { return m_notebook->GetSelection() == MainFrame::tp3DEditor && !m_plater->is_preview_shown(); }, 20s);

        Agent::AgentHost& host = project_host();
        if (!host.conversation().empty())
            host.on_page_message(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                                {"version", Agent::Protocol::kVersion},
                                                {"id", "prompt-new-" + name},
                                                {"type", Agent::Protocol::kCreateConversation},
                                                {"payload", nlohmann::json::object()}}
                                     .dump());
        const bool fresh = wait_for([&host] { return host.conversation().empty() && !host.busy(); }, 10s);
        check(fresh, name + "_starts_an_empty_chat");
        return sliced && fresh;
    }

    // Where a step's model is copied under a name that says nothing about it.
    static std::string prompt_model_path(const std::string& name)
    {
        return (fs::path(data_dir()) / "prompt-models" / name).string();
    }

    // Copies the source tree's `from` to prompt_model_path(`as`).
    bool prompt_copy_model(const nlohmann::json& copy, const std::string& name)
    {
        const fs::path target = prompt_model_path(copy.at("as").get<std::string>());
        boost::system::error_code error;
        fs::create_directories(target.parent_path(), error);
        fs::copy_file(fs::path(JUSPRIN_SOURCE_DIR) / copy.at("from").get<std::string>(), target,
                      fs::copy_options::overwrite_existing, error);
        check(!error, name + "_model_copied");
        return !error;
    }

    // The page's skills the recorder checks for, as the page sends each one:
    // the SKILL.md body (skillParser.ts). Only the file-loaded skill brings
    // a picture of the plate.
    static constexpr const char* kPromptFileLoadedSkill = "file-loaded";
    static const std::vector<std::string>& prompt_skill_names()
    {
        static const std::vector<std::string> names{kPromptFileLoadedSkill, "project-setup-changed"};
        return names;
    }
    static std::string prompt_skill_text(const std::string& name)
    {
        std::ifstream file((fs::path(JUSPRIN_SOURCE_DIR) / "src/slic3r/GUI/JusPrin/AgentUI/src/skills" / name / "SKILL.md").string(),
                           std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const std::size_t body = text.find("\n---\n");
        text = body == std::string::npos ? std::string() : text.substr(body + 5);
        text.erase(0, text.find_first_not_of('\n'));
        text.erase(text.find_last_not_of(" \n") + 1);
        return text;
    }

    // Which skill an input item is (a developer item that starts with its
    // text), or empty; and whether it is a plate picture (a user item
    // holding one image and nothing else).
    static std::string prompt_skill_of(const nlohmann::json& item)
    {
        if (item.value("role", "") != "developer" || !item.contains("content") || !item["content"].is_string())
            return {};
        for (const std::string& name : prompt_skill_names()) {
            const std::string text = prompt_skill_text(name);
            if (!text.empty() && item["content"].get<std::string>().rfind(text, 0) == 0)
                return name;
        }
        return {};
    }
    static bool prompt_is_picture(const nlohmann::json& item)
    {
        return item.value("role", "") == "user" && item.contains("content") && item["content"].is_array() &&
               item["content"].size() == 1 && item["content"][0].value("type", "") == "input_image";
    }

    // "{{file/<name>}}" is the path of a model a step copied under that name;
    // "{{attachment}}" the id of the file the person last attached;
    // "{{result/<pointer>}}" is the last tool result in this request;
    // "{{workspace/sessionId}}", "{{workspace/revision}}",
    // "{{workspace/activePlateId}}", "{{workspace/firstObjectId}}" (the
    // active plate's first object), "{{workspace/filamentPreset}}" and
    // "{{workspace/printerPreset}}" the project now; "{{object}}" the object
    // an "eachObject" answer is being read for.
    nlohmann::json prompt_fill(const nlohmann::json& value, const nlohmann::json& body, const std::string& name)
    {
        using nlohmann::json;
        if (value.is_object() || value.is_array()) {
            json filled = value;
            for (auto& [key, item] : filled.items())
                item = prompt_fill(item, body, name);
            return filled;
        }
        if (!value.is_string())
            return value;
        const std::string text = value;
        if (text.size() < 5 || text.rfind("{{", 0) != 0 || text.compare(text.size() - 2, 2, "}}") != 0)
            return value;
        const std::string path = text.substr(2, text.size() - 4);
        if (path == "object" && !m_prompt_object.empty())
            return m_prompt_object;
        if (path.rfind("file/", 0) == 0)
            return prompt_model_path(path.substr(5));
        if (path == "attachment" && !m_prompt_attachment.empty())
            return m_prompt_attachment;
        if (path == "workspace/sessionId")
            return std::to_string(installed_shell()->workspace()->snapshot().session.value());
        if (path == "workspace/revision")
            return installed_shell()->workspace()->snapshot().revision;
        if (path == "workspace/filamentPreset")
            return installed_shell()->workspace()->snapshot().setup.filament_preset;
        if (path == "workspace/printerPreset")
            return installed_shell()->workspace()->snapshot().setup.printer_preset;
        if (path == "workspace/activePlateId" || path == "workspace/firstObjectId")
            for (const auto& plate : installed_shell()->workspace()->snapshot().plates)
                if (plate.active && (path == "workspace/activePlateId" || !plate.objects.empty()))
                    return std::to_string(path == "workspace/activePlateId" ? plate.id.value() : plate.objects.front().id.value());
        if (path.rfind("result/", 0) == 0) {
            const json& input = body.at("input");
            for (auto item = input.rbegin(); item != input.rend(); ++item)
                if (item->value("type", "") == "function_call_output") {
                    const json output = json::parse(item->at("output").get<std::string>());
                    const json::json_pointer pointer(path.substr(6));
                    if (output.contains("result") && output["result"].contains(pointer))
                        return output["result"][pointer];
                    break;
                }
        }
        check(false, name + "_fills " + text);
        return value;
    }

    // Answers a held request with model output, as the service streams it.
    static void prompt_answer(HeldModelRequest& request, const nlohmann::json& output, const std::string& text)
    {
        using Event = Agent::AgentHttpEvent;
        const auto frame = [](const nlohmann::json& event) { return "event: message\ndata: " + event.dump() + "\n\n"; };
        if (!text.empty())
            request.answer(Event{Event::Kind::Data, frame({{"type", "response.output_text.delta"}, {"delta", text}}), {}, 0});
        request.answer(Event{Event::Kind::Data,
                             frame({{"type", "response.completed"}, {"response", {{"output", output}}}}), {}, 0});
        request.answer(Event{Event::Kind::Complete, {}, {}, 200});
    }

    // The next request of this turn the model is asked, past any chat title.
    std::optional<HeldModelRequest> prompt_next_request()
    {
        for (;;) {
            if (!wait_for([] { return !g_held_model_requests.empty(); }, std::chrono::seconds(120)))
                return std::nullopt;
            HeldModelRequest request = std::move(g_held_model_requests.front());
            g_held_model_requests.pop_front();
            if (request.body.contains("tools"))
                return request;
            prompt_answer(request, nlohmann::json::array({{{"type", "message"}, {"role", "assistant"},
                                                           {"content", nlohmann::json::array({{{"type", "output_text"},
                                                                                               {"text", "Fixture chat"}}})}}}),
                          "Fixture chat");
        }
    }

    std::string m_prompt_object;      // for "{{object}}"
    std::string m_prompt_attachment;  // for "{{attachment}}"

    // The person attaches a copied model to the chat, as the composer's file
    // picker does; its id, once the app has stored it, or empty.
    std::string prompt_attach(const nlohmann::json& copy, const std::string& name)
    {
        if (!prompt_copy_model(copy, name))
            return {};
        std::ifstream file(prompt_model_path(copy.at("as").get<std::string>()), std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        static int next = 0;
        const std::string client_id = "prompt-attachment-" + std::to_string(++next);
        Agent::AgentHost& host = project_host();
        host.on_page_message(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                            {"version", Agent::Protocol::kVersion},
                                            {"id", "prompt-attach-" + std::to_string(next)},
                                            {"type", Agent::Protocol::kAttachFile},
                                            {"payload", {{"clientAttachmentId", client_id},
                                                         {"name", copy.at("as").get<std::string>()},
                                                         {"source", "picker"},
                                                         {"dataBase64", wxBase64Encode(bytes.data(), bytes.size()).utf8_string()}}}}
                                 .dump());
        const auto record = persistence().document().find_attachment_by_client_id(client_id);
        check(record.has_value() && record->kind == "model", name + "_model_attached");
        return record ? record->id : std::string();
    }

    // The answers' reads called after the kept request, in the same turn;
    // returns each with the result the app gave, as the model would read it.
    // A read marked "eachObject" is called once for every object on every
    // plate, and an "allSettings" entry with it once for every object.
    // An "allSettings" entry reads every setting of its scope:
    // the keys come from the app's own settings_search (an empty query lists
    // them all, page by page; those pages are not kept), then settings_get
    // reads them 32 at a time. With "keysOf" it reads, under its scope, the
    // keys of the scopes named, read before it, that are not its own: how
    // the app answers a name asked for under the wrong scope. The workspace block every result
    // carries is kept once, beside them, and left out of each result that
    // repeats it exactly.
    nlohmann::json prompt_answers(const std::string& name, HeldModelRequest request, const nlohmann::json& script_reads,
                                  std::size_t& once)
    {
        using nlohmann::json;
        json answers = json::array();
        int  calls   = 0;
        const json kept = request.body.at("input");
        once = 0;
        // One read in the turn; the result as the app gave it, or nothing.
        const auto call = [&](const std::string& tool, const json& arguments) -> std::optional<std::string> {
            const std::string call_id = "call_" + name + "_answer_" + std::to_string(++calls);
            prompt_answer(request, json::array({{{"type", "function_call"}, {"call_id", call_id}, {"name", tool},
                                                 {"arguments", arguments.dump()}}}), "");
            std::optional<HeldModelRequest> next = prompt_next_request();
            check(next.has_value(), name + "_answer_" + tool + "_returned");
            if (!next)
                return std::nullopt;
            request = std::move(*next);
            if (calls == 1) {
                // What the app sent this request only: the items of the kept
                // request the next one does not repeat. The runner leaves
                // them out when it answers a read, as the app does.
                const json& input = request.body.at("input");
                std::size_t same = 0;
                while (same < kept.size() && same < input.size() && kept[same] == input[same])
                    ++same;
                once = kept.size() - same;
                bool repeated = false;
                for (const json& item : input)
                    repeated = repeated || !prompt_skill_of(item).empty() || prompt_is_picture(item);
                check(!repeated, name + "_skill_and_picture_not_on_the_next_request");
            }
            const std::string output = request.body.at("input").back().at("output");
            const bool succeeded = json::parse(output).value("state", "") == "succeeded";
            check(succeeded, name + "_answer_" + tool + "_succeeded");
            return succeeded ? std::optional<std::string>(output) : std::nullopt;
        };
        std::map<std::string, std::vector<std::string>> scope_keys;
        bool ok = true;
        for (const json& read : script_reads) {
            if (!ok)
                break;
            std::vector<std::string> objects{std::string()};
            if (read.value("eachObject", false)) {
                objects.clear();
                for (const auto& plate : installed_shell()->workspace()->snapshot().plates)
                    for (const auto& object : plate.objects)
                        objects.push_back(std::to_string(object.id.value()));
            }
            if (read.contains("allSettings")) {
                const std::string scope = read["allSettings"];
                for (const std::string& object : objects) {
                    if (!ok)
                        break;
                    m_prompt_object = object;
                    const json target = prompt_fill(read.value("target", json()), request.body, name);
                    std::vector<std::string> keys;
                    if (read.contains("keysOf")) {
                        const auto& own = scope_keys[scope];
                        for (const json& other : read["keysOf"])
                            for (const std::string& key : scope_keys[other.get<std::string>()])
                                if (std::find(own.begin(), own.end(), key) == own.end())
                                    keys.push_back(key);
                    } else if (scope_keys.count(scope)) {
                        keys = scope_keys[scope];  // every object of the scope has the same settings
                    } else {
                        std::string cursor;
                        do {
                            json search{{"scope", scope}, {"query", ""}, {"limit", 25}};
                            if (!target.is_null())
                                search["target"] = target;
                            if (!cursor.empty())
                                search["cursor"] = cursor;
                            const std::optional<std::string> page = call("settings_search", search);
                            if (!(ok = page.has_value()))
                                break;
                            const json result = json::parse(*page).at("result");
                            for (const json& item : result.at("items"))
                                keys.push_back(item.at("key"));
                            cursor = result.value("nextCursor", "");
                        } while (!cursor.empty());
                        scope_keys[scope] = keys;
                    }
                    for (std::size_t first = 0; ok && first < keys.size(); first += 32) {
                        json arguments{{"scope", scope},
                                       {"keys", std::vector<std::string>(keys.begin() + first, keys.begin() + std::min(keys.size(), first + 32))}};
                        if (!target.is_null())
                            arguments["target"] = target;
                        const std::optional<std::string> output = call("settings_get", arguments);
                        if ((ok = output.has_value()))
                            answers.push_back({{"tool", "settings_get"}, {"arguments", arguments}, {"output", *output}});
                    }
                    std::cerr << "HARNESS PROMPT " << name << " allSettings " << scope << (object.empty() ? "" : " " + object)
                              << (read.contains("keysOf") ? " (other scopes' keys)" : "") << " keys=" << keys.size() << '\n';
                }
                continue;
            }
            for (const std::string& object : objects) {
                m_prompt_object = object;
                const json arguments = prompt_fill(read.value("arguments", json::object()), request.body, name);
                const std::optional<std::string> output = call(read.at("call"), arguments);
                if (!(ok = output.has_value()))
                    break;
                answers.push_back({{"tool", read.at("call")}, {"arguments", arguments}, {"output", *output}});
            }
        }
        m_prompt_object.clear();
        prompt_answer(request, json::array(), "");
        wait_for([this] { return !g_held_model_requests.empty() || !project_host().busy(); }, std::chrono::seconds(10));
        json shared = json::object();
        for (json& answer : answers) {
            json output = json::parse(answer["output"].get<std::string>());
            const json block{{"workspace", output.value("workspace", json())}, {"workspaceRevision", output.value("workspaceRevision", json())}};
            if (shared.empty())
                shared = block;
            if (block != shared)
                continue;
            output.erase("workspace");
            output.erase("workspaceRevision");
            answer["output"] = output.dump();
            answer["sharedWorkspace"] = true;
        }
        return {{"reads", answers}, {"shared", shared}};
    }

    // One step of a conversation: what the person does, then the model's
    // scripted outputs, one per request the app sends, until they run out.
    // The record point numbered `target` is kept, with its answers, and ends
    // the conversation (2); otherwise 1, or 0 on a failure.
    int prompt_step(const std::string& name, const nlohmann::json& step, nlohmann::json& recorded,
                    const nlohmann::json& tools, const std::string& instructions, const nlohmann::json& reads,
                    int& point, int target)
    {
        using nlohmann::json;
        using namespace std::chrono_literals;
        Agent::AgentHost& host = project_host();
        const json        scripted = step.value("replies", json::array());
        std::deque<json>  replies(scripted.begin(), scripted.end());

        // A person answers seconds later, after OrcaSlicer has taken in the
        // last turn's changes: a filament switched by printer_setup leaves
        // the plate's slice current until Orca's scheduled update runs.
        wait_for([this] { return !m_plater->is_background_process_update_scheduled(); }, 5s);
        if (step.contains("copy"))
            prompt_copy_model(step["copy"], name);
        json attached = json::array();
        if (step.contains("attach")) {
            m_prompt_attachment = prompt_attach(step["attach"], name);
            if (!m_prompt_attachment.empty())
                attached.push_back(m_prompt_attachment);
        }
        if (step.contains("say")) {
            std::string said = step["say"];
            for (std::size_t at; (at = said.find("{{file/")) != std::string::npos;) {
                const std::size_t end = said.find("}}", at);
                said.replace(at, end + 2 - at, prompt_model_path(said.substr(at + 7, end - at - 7)));
            }
            static int next = 0;
            ++next;
            host.on_page_message(json{{"protocol", Agent::Protocol::kName},
                                      {"version", Agent::Protocol::kVersion},
                                      {"id", "prompt-" + std::to_string(next)},
                                      {"type", Agent::Protocol::kUserMessage},
                                      {"payload", {{"clientMessageId", "prompt-c-" + std::to_string(next)}, {"text", said},
                                                   {"attachmentIds", attached}}}}
                                     .dump());
        } else if (step.contains("issue")) {
            // A setting typed in by hand, the issue OrcaSlicer reports for
            // it, and a press of one of the issue's two buttons.
            prompt_hand_edit(step.at("set"));
            const std::string setting = step.at("set").begin().key();
            std::string       issue_id;
            wait_for([&] {
                issue_monitor().refresh_now();
                for (const auto& issue : issues().issues)
                    if (issues().checked && issue.source == PrintIssues::IssueSource::Validation && issue.setting == setting)
                        issue_id = issue.id;
                return !issue_id.empty();
            }, 30s);
            check(!issue_id.empty(), name + "_issue_reported");
            const std::string outcome = issue_id.empty() ? "" : installed_shell()->submit_issue(issue_id, step["issue"]).outcome;
            std::cerr << "HARNESS PROMPT " << name << " issue " << issue_id << " outcome=" << outcome << '\n';
            check(outcome == "started", name + "_issue_sent_to_the_chat");
        } else if (step.contains("drop")) {
            // A model with a name that says nothing about it, imported by the
            // person into an empty project.
            check(prompt_copy_model(json{{"from", step.at("model")}, {"as", step["drop"]}}, name), name + "_model_ready");
            check(m_plater->new_project(true, true) != wxID_CANCEL, name + "_empty_project");
            m_plater->add_model(false, prompt_model_path(step["drop"]));
        } else if (step.contains("open")) {
            // A project someone saved with an author note in its description,
            // written by the app's own exporter, then opened.
            if (!m_plater->model().model_info)
                m_plater->model().model_info = std::make_shared<ModelInfo>();
            m_plater->model().model_info->description = step.value("description", "");
            const fs::path file = fs::path(data_dir()) / (step["open"].get<std::string>() + ".3mf");
            check(m_plater->export_3mf(file, SaveStrategy::SplitModel | SaveStrategy::ShareMesh | SaveStrategy::Silence) >= 0,
                  name + "_project_saved");
            check(m_plater->new_project(true, true) != wxID_CANCEL, name + "_saved_project_closed");
            m_plater->load_project(from_u8(file.string()), "-");
        }

        bool expect_failure = false;
        int  calls          = 0;
        for (;;) {
            // The chat title is asked for after a reply ends; no case reads it.
            wait_for([&] { return !g_held_model_requests.empty() || (replies.empty() && !host.busy()); }, 120s);
            if (g_held_model_requests.empty()) {
                if (!replies.empty()) {
                    const auto snapshot = installed_shell()->workspace()->snapshot();
                    std::cerr << "HARNESS PROMPT " << name << " waiting: slicing.running=" << snapshot.slicing.running
                              << " orca_slicing=" << m_plater->is_background_process_slicing();
                    for (const auto& plate : snapshot.plates)
                        std::cerr << " plate " << plate.id.value() << " active=" << plate.active << " sliced=" << plate.sliced;
                    issue_monitor().refresh_now();
                    for (const auto& issue : issues().issues)
                        std::cerr << " | issue " << issue.id << " '" << issue.message << "'";
                    for (const auto& activity : host.tools().activities())
                        std::cerr << " | " << activity.tool << '=' << Agent::tool_state_name(activity.state);
                    std::cerr << '\n';
                }
                break;
            }
            HeldModelRequest request = std::move(g_held_model_requests.front());
            g_held_model_requests.pop_front();
            if (!request.body.contains("tools")) {
                prompt_answer(request, json::array({{{"type", "message"}, {"role", "assistant"},
                                                     {"content", json::array({{{"type", "output_text"}, {"text", "Fixture chat"}}})}}}),
                              "Fixture chat");
                continue;
            }
            const json& input = request.body.at("input");
            if (!input.empty() && input.back().value("type", "") == "function_call_output") {
                const json output = json::parse(input.back().at("output").get<std::string>());
                const std::string state = output.value("state", "");
                std::cerr << "HARNESS PROMPT " << name << " result state=" << state
                          << (output.contains("error") ? " error=" + output["error"].value("code", "") : std::string()) << '\n';
                check((state == "failed") == expect_failure && state != "running", name + "_call_" + std::to_string(calls) + "_outcome");
            }
            if (replies.empty()) {
                check(false, name + "_unscripted_request");
                prompt_answer(request, json::array(), "");
                continue;
            }
            const json reply = replies.front();
            replies.pop_front();
            if (reply.contains("set"))
                prompt_hand_edit(reply["set"]);
            if (reply.contains("record") && point++ == target) {
                // The suite sends its own copies; they must be what the app sends.
                const std::string case_name = reply["record"];
                // A message with a model attached adds object_import to the
                // tools, after the others in the registry's order; such a
                // point keeps its own list.
                json with_import = json::array();
                for (const json& tool : request.body.at("tools"))
                    if (tool.value("name", "") != "object_import")
                        with_import.push_back(tool);
                const bool imports = with_import.size() + 1 == request.body.at("tools").size();
                check((imports ? with_import : request.body.at("tools")) == tools, case_name + "_tools_match_project_tools_json");
                check(request.body.at("instructions") == instructions, case_name + "_instructions_are_the_page_prompt");
                json kept = request.body;
                if (!imports)
                    kept.erase("tools");
                kept.erase("instructions");
                const std::size_t items = input.size();
                // A point marked "skill" is the request where the model first
                // reads about that skill's event: it ends with that skill,
                // after the plate's picture for the file-loaded skill. No
                // other point holds a skill or a picture, and an import's
                // point holds the file-loaded skill alone.
                const std::string wanted   = reply.value("skill", "");
                const bool        pictured = wanted == kPromptFileLoadedSkill;
                const std::size_t tail     = wanted.empty() ? 0 : pictured ? 2 : 1;
                const bool has_skill = !wanted.empty() && items >= tail && prompt_skill_of(input[items - 1]) == wanted &&
                                       (!pictured || prompt_is_picture(input[items - 2]));
                bool elsewhere = false;
                for (std::size_t i = 0; i + (has_skill ? tail : 0) < items; ++i)
                    elsewhere = elsewhere || !prompt_skill_of(input[i]).empty() || prompt_is_picture(input[i]);
                for (const std::string& skill : prompt_skill_names())
                    check(!prompt_skill_text(skill).empty(), case_name + "_skill_text_read_" + skill);
                check(has_skill == !wanted.empty() && !elsewhere,
                      case_name + (wanted.empty() ? "_carries_no_skill" : "_ends_with_" + wanted));
                std::size_t once = 0;
                const json recorded_reads = prompt_answers(case_name, std::move(request), reads, once);
                const json& answers = recorded_reads["reads"];
                check(once == tail, case_name + "_sent_once_" + std::to_string(once));
                recorded[case_name] = {{"body", kept}, {"answers", answers}, {"sharedWorkspace", recorded_reads["shared"]}};
                if (once > 0)
                    recorded[case_name]["once"] = once;
                std::cerr << "HARNESS PROMPT recorded " << case_name << " input_items=" << items
                          << " bytes=" << kept.dump().size() << " answers=" << answers.size()
                          << " answer_bytes=" << answers.dump().size() << '\n';
                return 2;
            }
            json output = json::array();
            std::string text;
            if (reply.contains("text")) {
                text = reply["text"];
                output.push_back({{"type", "message"}, {"role", "assistant"},
                                  {"content", json::array({{{"type", "output_text"}, {"text", text}}})}});
            }
            if (reply.contains("call")) {
                ++calls;
                // The form the app replays a call in; a live model's own item
                // id and status are not reproducible.
                output.push_back({{"type", "function_call"},
                                  {"call_id", "call_" + name + "_" + std::to_string(calls)},
                                  {"name", reply["call"]},
                                  {"arguments", prompt_fill(reply.value("arguments", json::object()), request.body, name).dump()}});
                expect_failure = reply.value("fails", false);
                std::cerr << "HARNESS PROMPT " << name << " call " << reply["call"].get<std::string>() << ' '
                          << output.back()["arguments"].get<std::string>() << '\n';
            }
            prompt_answer(request, output, text);
        }
        check(replies.empty(), name + "_every_scripted_output_used");
        return replies.empty() ? 1 : 0;
    }

    void record_prompt_requests()
    {
        using nlohmann::json;
        m_state->deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
        const auto read = [](const fs::path& path) {
            std::ifstream file(path.string(), std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        };
        const json script = json::parse(read(m_state->prompt_script), nullptr, false);
        const json tools  = json::parse(read(fs::path(JUSPRIN_SOURCE_DIR) / "tests/project_prompt/project_tools.json"), nullptr, false);
        const std::string instructions =
            read(fs::path(JUSPRIN_SOURCE_DIR) / "src/slic3r/GUI/JusPrin/AgentUI/src/projectInstructions.fixture.txt");
        check(script.is_object() && script.contains("conversations"), "prompt_script_read");
        check(tools.is_array() && !instructions.empty(), "prompt_tools_and_instructions_read");
        json recorded = json::object();
        const json reads = script.is_object() ? script.value("answers", json::array()) : json::array();
        bool ok = script.is_object() && tools.is_array();
        if (ok)
            for (const json& conversation : script["conversations"]) {
                const std::string name = conversation.at("name");
                int points = 0;
                for (const json& step : conversation.at("steps"))
                    for (const json& reply : step.value("replies", json::array()))
                        points += reply.contains("record") ? 1 : 0;
                // One pass per kept request: its answers end the conversation.
                for (int target = 0; ok && target < points; ++target) {
                    std::cerr << "HARNESS PROMPT conversation " << name << " point " << target << '\n';
                    if (!(ok = prompt_fixture(conversation.value("fixture", "plates"), name, conversation.value("plateType", ""))))
                        break;
                    int point = 0, result = 1;
                    for (const json& step : conversation.at("steps"))
                        if (result == 1)
                            result = prompt_step(name, step, recorded, tools, instructions, reads, point, target);
                    check(result == 2, name + "_point_" + std::to_string(target) + "_kept");
                    ok = result == 2;
                }
                if (!ok)
                    break;
            }
        // The setting items of settings_get and settings_search results repeat
        // across reads and points: each distinct one is kept once, in
        // settingItems, and a result holds its index there instead.
        json items = json::array();
        std::map<std::string, std::size_t> index;
        for (auto& [case_name, kept] : recorded.items())
            for (json& answer : kept["answers"]) {
                if (answer["tool"] != "settings_get" && answer["tool"] != "settings_search")
                    continue;
                json output = json::parse(answer["output"].get<std::string>());
                for (const char* field : {"items", "unknownKeys"}) {
                    if (!output["result"].contains(field))
                        continue;
                    for (json& item : output["result"][field]) {
                        const auto [found, added] = index.emplace(item.dump(), items.size());
                        if (added)
                            items.push_back(item);
                        item = found->second;
                    }
                }
                answer["output"]       = output.dump();
                answer["settingItems"] = true;
            }
        std::ofstream output(m_state->prompt_requests.string(), std::ios::binary);
        output << json{{"settingItems", items}, {"cases", recorded}}.dump(1) << '\n';
        check(output.good(), "prompt_requests_written");
        finish();
    }
