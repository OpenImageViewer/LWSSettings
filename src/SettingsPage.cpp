#include "Internal.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#ifdef _WIN32
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace LWSSettings
{
    using Json = nlohmann::json;
    namespace
    {
        struct Invalid
        {
            Error error;
        };
        [[noreturn]] void Reject(const std::string& location, const std::string& message)
        {
            throw Invalid{{location, message}};
        }
        std::string Required(const Json& j, const char* key, const std::string& location)
        {
            if (!j.contains(key) || !j[key].is_string() || j[key].get<std::string>().empty())
                Reject(location + "/" + key, "Expected a nonempty string");
            return j[key].get<std::string>();
        }
        Node ReadNode(const Json& j, const std::string& location, std::unordered_set<std::string>& ids, int depth)
        {
            if (depth > 64)
                Reject(location, "Group nesting exceeds 64 levels");
            if (!j.is_object())
                Reject(location, "Expected a setting or group object");
            Node node;
            node.id = Required(j, "id", location);
            if (!ids.insert(node.id).second)
                Reject(location + "/id", "Duplicate ID: " + node.id);
            node.label = Required(j, "label", location);
            node.description = j.value("description", std::string{});
            const auto type = Required(j, "type", location);
            static const std::map<std::string, Control> types{
                {"group", Control::Group},       {"text", Control::Text},   {"integer", Control::Integer},
                {"number", Control::Number},     {"combo", Control::Combo}, {"radio", Control::Radio},
                {"checkbox", Control::Checkbox}, {"color", Control::Color}, {"file", Control::File}};
            auto it = types.find(type);
            if (it == types.end())
                Reject(location + "/type", "Unknown control type: " + type);
            node.control = it->second;
            if (node.control == Control::Group)
            {
                if (!j.contains("children") || !j["children"].is_array())
                    Reject(location, "Group requires children array");
                for (size_t i = 0; i < j["children"].size(); ++i)
                    node.children.push_back(
                        ReadNode(j["children"][i], location + "/children/" + std::to_string(i), ids, depth + 1));
            }
            else
            {
                for (auto key : {"min", "max"})
                    if (j.contains(key) && (!j[key].is_number() || !std::isfinite(j[key].get<double>())))
                        Reject(location + "/" + key, "Expected a finite numeric bound");
                if (node.control == Control::Integer)
                {
                    for (auto key : {"min", "max"})
                    {
                        if (!j.contains(key))
                            continue;
                        if (!j[key].is_number_integer() ||
                            (j[key].is_number_unsigned() && j[key].get<uint64_t>() > uint64_t(INT64_MAX)))
                            Reject(location + "/" + key, "Expected a signed 64-bit integer bound");
                    }
                    if (j.contains("min"))
                        node.integerMinimum = j["min"].get<int64_t>();
                    if (j.contains("max"))
                        node.integerMaximum = j["max"].get<int64_t>();
                    if (node.integerMinimum && node.integerMaximum && *node.integerMinimum > *node.integerMaximum)
                        Reject(location, "Minimum exceeds maximum");
                }
                else
                {
                    if (j.contains("min"))
                        node.minimum = j["min"].get<double>();
                    if (j.contains("max"))
                        node.maximum = j["max"].get<double>();
                    if (node.minimum && node.maximum && *node.minimum > *node.maximum)
                        Reject(location, "Minimum exceeds maximum");
                }
                if (node.control == Control::Combo || node.control == Control::Radio)
                {
                    if (!j.contains("options") || !j["options"].is_array() || j["options"].empty())
                        Reject(location, "Choice control requires nonempty options");
                    std::unordered_set<std::string> choices;
                    for (const auto& option : j["options"])
                    {
                        Option o{Required(option, "label", location + "/options"),
                                 Required(option, "value", location + "/options")};
                        if (!choices.insert(o.value).second)
                            Reject(location, "Duplicate choice value");
                        node.options.push_back(std::move(o));
                    }
                }
                if (j.contains("step"))
                {
                    if (node.control == Control::Integer)
                    {
                        if (!j["step"].is_number_integer() ||
                            (j["step"].is_number_unsigned() && j["step"].get<uint64_t>() > uint64_t(INT64_MAX)) ||
                            j["step"].get<int64_t>() <= 0)
                            Reject(location + "/step", "Expected a positive integer step");
                        node.integerStep = j["step"].get<int64_t>();
                    }
                    else if (node.control == Control::Number)
                    {
                        if (!j["step"].is_number() || !std::isfinite(j["step"].get<double>()) ||
                            j["step"].get<double>() <= 0)
                            Reject(location + "/step", "Expected a positive finite step");
                        node.numberStep = j["step"].get<double>();
                    }
                    else
                        Reject(location + "/step", "Step applies only to numeric settings");
                }
                if (!j.contains("default"))
                    Reject(location, "Setting requires a default");
                auto initial = ParseValue(node, j["default"]);
                if (!initial)
                    Reject(location + "/default", initial.error().message);
                node.initial = *initial;
            }
            return node;
        }
        Json ToJson(const Value& value)
        {
            return std::visit(
                [](const auto& v) -> Json
                {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, LLUtils::Color>)
                        return FormatValue(Value{v});
                    else
                        return v;
                },
                value);
        }
        std::expected<Json, Error> ReadFile(const std::filesystem::path& file)
        {
            try
            {
                std::ifstream input(file, std::ios::binary);
                if (!input)
                    return std::unexpected(Error{file.string(), "Cannot open file"});
                return Json::parse(input);
            }
            catch (const std::exception& e)
            {
                return std::unexpected(Error{file.string(), e.what()});
            }
        }
    }  // namespace

    std::string Lower(std::string text)
    {
        for (char& c : text)
            if (c >= 'A' && c <= 'Z')
                c = char(c + ('a' - 'A'));
        return text;
    }

    std::string FormatValue(const Value& value)
    {
        return std::visit(
            [](const auto& v) -> std::string
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>)
                    return v;
                else if constexpr (std::is_same_v<T, bool>)
                    return v ? "true" : "false";
                else if constexpr (std::is_same_v<T, LLUtils::Color>)
                {
                    std::ostringstream s;
                    s << '#' << std::hex << std::setfill('0');
                    for (auto channel : v.channels)
                        s << std::setw(2) << int(channel);
                    return s.str();
                }
                else if constexpr (std::is_same_v<T, double>)
                {
                    char buffer[64];
                    auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), v);
                    return ec == std::errc{} ? std::string(buffer, end) : std::string{};
                }
                else
                    return std::to_string(v);
            },
            value);
    }

    std::expected<Value, Error> ParseValue(const Node& node, const Json& j)
    {
        auto bad = [&](std::string message) -> std::expected<Value, Error>
        { return std::unexpected(Error{node.id, std::move(message)}); };
        if (j.is_string())
        {
            try
            {
                (void) j.dump();
            }
            catch (const std::exception&)
            {
                return bad("Expected valid UTF-8 text");
            }
        }
        switch (node.control)
        {
            case Control::Checkbox:
                if (j.is_boolean())
                    return Value{j.get<bool>()};
                return bad("Expected a boolean");
            case Control::Integer:
            case Control::Number:
            {
                if (!j.is_number())
                    return bad("Expected a number");
                double number = j.get<double>();
                if (!std::isfinite(number))
                    return bad("Expected a finite number");
                if (node.minimum && number < *node.minimum)
                    return bad("Value is below minimum");
                if (node.maximum && number > *node.maximum)
                    return bad("Value exceeds maximum");
                if (node.control == Control::Integer)
                {
                    if (!j.is_number_integer() || (j.is_number_unsigned() && j.get<uint64_t>() > uint64_t(INT64_MAX)))
                        return bad("Expected a signed 64-bit integer");
                    auto integer = j.get<int64_t>();
                    if (node.integerMinimum && integer < *node.integerMinimum)
                        return bad("Value is below minimum");
                    if (node.integerMaximum && integer > *node.integerMaximum)
                        return bad("Value exceeds maximum");
                    return Value{integer};
                }
                return Value{number};
            }
            case Control::Color:
            {
                if (!j.is_string())
                    return bad("Expected #RRGGBB or #RRGGBBAA");
                auto s = j.get<std::string>();
                if ((s.size() != 7 && s.size() != 9) || s[0] != '#')
                    return bad("Expected #RRGGBB or #RRGGBBAA");
                uint32_t c = 0;
                auto [end, ec] = std::from_chars(s.data() + 1, s.data() + s.size(), c, 16);
                if (ec != std::errc{} || end != s.data() + s.size())
                    return bad("Invalid hexadecimal color");
                if (s.size() == 7)
                    c = (c << 8) | 255;
                return Value{LLUtils::Color{c}};
            }
            case Control::Combo:
            case Control::Radio:
                if (!j.is_string())
                    return bad("Expected a choice value");
                for (const auto& option : node.options)
                    if (option.value == j.get_ref<const std::string&>())
                        return Value{option.value};
                return bad("Value is not one of the choices");
            case Control::Text:
            case Control::File:
                if (j.is_string())
                    return Value{j.get<std::string>()};
                return bad("Expected a UTF-8 string");
            default:
                return bad("Groups have no value");
        }
    }

    SettingsPage::SettingsPage(SettingsConfig config) : impl_(std::make_unique<Impl>(std::move(config)))
    {
        if (!std::isfinite(impl_->config.appearance.scale) || impl_->config.appearance.scale < 0.5f ||
            impl_->config.appearance.scale > 4.f)
            throw std::invalid_argument("Appearance scale must be between 0.5 and 4");
        if (!std::isfinite(impl_->config.appearance.fontSize) || impl_->config.appearance.fontSize < 8 ||
            impl_->config.appearance.fontSize > 48)
            throw std::invalid_argument("Appearance font size must be between 8 and 48 logical pixels");
    }
    SettingsPage::~SettingsPage() = default;
    SettingsPage::Connection SettingsPage::OnChange(std::function<void(const Change&)> callback)
    {
        // Keep the callable stable if a callback grows LLUtils' listener vector.
        auto target = std::make_shared<std::function<void(const Change&)>>(std::move(callback));
        return impl_->changes.Connect(
            [target](const Change& change)
            {
                auto retained = target;
                (*retained)(change);
            });
    }

    void SettingsPage::Impl::Index()
    {
        nodes.clear();
        breadcrumbs.clear();
        auto walk = [&](auto&& self, const std::vector<Node>& list, const std::string& path) -> void
        {
            for (const auto& node : list)
            {
                nodes.emplace(node.id, &node);
                auto full = path.empty() ? node.label : path + " / " + node.label;
                breadcrumbs.emplace(node.id, full);
                self(self, node.children, full);
            }
        };
        walk(walk, roots, {});
    }
    const Node* SettingsPage::Impl::Find(std::string_view id) const
    {
        auto it = nodes.find(id);
        return it == nodes.end() ? nullptr : it->second;
    }
    Status SettingsPage::Impl::Fail(std::string location, std::string message)
    {
        error = Error{std::move(location), std::move(message)};
        status = error->location + ": " + error->message;
        return std::unexpected(*error);
    }

    Status SettingsPage::LoadTemplate(const Json& document)
    {
        if (impl_->notifying)
            return impl_->Fail("template", "Cannot replace the template from a change callback");
        try
        {
            (void) document.dump();  // Validate UTF-8 even for JSON constructed directly by a caller.
            if (!document.is_object() || document.value("version", 0) != 1 || !document.contains("children") ||
                !document["children"].is_array())
                return impl_->Fail("/", "Expected version 1 and a children array");
            std::vector<Node> roots;
            std::unordered_set<std::string> ids;
            for (size_t i = 0; i < document["children"].size(); ++i)
                roots.push_back(ReadNode(document["children"][i], "/children/" + std::to_string(i), ids, 0));
            impl_->EndScrollbarDrag();
            impl_->AbortSpin();
            impl_->roots = std::move(roots);
            impl_->Index();
            impl_->values.clear();
            for (const auto& [id, node] : impl_->nodes)
                if (node->control != Control::Group)
                    impl_->values[id] = node->initial;
            impl_->applied = impl_->saved = impl_->values;
            impl_->edit.reset();
            impl_->colorStart.reset();
            impl_->colorId.clear();
            impl_->selected.clear();
            impl_->hovered.clear();
            impl_->collapsed.clear();
            impl_->dropdown = false;
            impl_->scroll = 0;
            impl_->error.reset();
            impl_->status.clear();
            impl_->RebuildRows();
            impl_->Paint();
            return {};
        }
        catch (const Invalid& e)
        {
            return impl_->Fail(e.error.location, e.error.message);
        }
        catch (const std::exception& e)
        {
            return impl_->Fail("/", e.what());
        }
    }
    Status SettingsPage::LoadTemplateFile(const std::filesystem::path& file)
    {
        auto document = ReadFile(file);
        if (!document)
            return impl_->Fail(document.error().location, document.error().message);
        return LoadTemplate(*document);
    }
    Status SettingsPage::LoadValues(const Json& document)
    {
        if (impl_->notifying)
            return impl_->Fail("values", "Cannot replace values from a change callback");
        try
        {
            (void) document.dump();  // Validate UTF-8 even for JSON constructed directly by a caller.
            if (!document.is_object() || document.value("version", 0) != 1 || !document.contains("values") ||
                !document["values"].is_object())
                return impl_->Fail("/", "Expected version 1 and a values object");
            Values pending;
            for (const auto& [id, node] : impl_->nodes)
            {
                if (node->control == Control::Group)
                    continue;
                if (!document["values"].contains(id))
                    pending[id] = node->initial;
                else
                {
                    auto v = ParseValue(*node, document["values"][id]);
                    if (!v)
                        return impl_->Fail(v.error().location, v.error().message);
                    pending[id] = *v;
                }
            }
            impl_->EndScrollbarDrag();
            impl_->AbortSpin();
            impl_->values = impl_->applied = impl_->saved = std::move(pending);
            impl_->edit.reset();
            impl_->colorStart.reset();
            impl_->colorId.clear();
            impl_->error.reset();
            impl_->status.clear();
            impl_->Paint();
            return {};
        }
        catch (const std::exception& e)
        {
            return impl_->Fail("/", e.what());
        }
    }
    Status SettingsPage::LoadValuesFile(const std::filesystem::path& file)
    {
        auto document = ReadFile(file);
        if (!document)
            return impl_->Fail(document.error().location, document.error().message);
        return LoadValues(*document);
    }
    Json SettingsPage::ExportValues() const
    {
        Json document{{"version", 1}, {"values", Json::object()}};
        for (const auto& [id, value] : impl_->values)
            document["values"][id] = ToJson(value);
        return document;
    }
    std::optional<Value> SettingsPage::GetValue(std::string_view id) const
    {
        auto it = impl_->values.find(id);
        return it == impl_->values.end() ? std::nullopt : std::optional<Value>{it->second};
    }
    std::optional<Value> SettingsPage::GetEffectiveValue(std::string_view id) const
    {
        auto it = impl_->applied.find(id);
        return it == impl_->applied.end() ? std::nullopt : std::optional<Value>{it->second};
    }

    void SettingsPage::Impl::Notify(Change change)
    {
        notifications.push_back(std::move(change));
        DrainNotifications();
    }
    void SettingsPage::Impl::DrainNotifications()
    {
        if (notifying)
            return;
        notifying = true;
        try
        {
            while (!notifications.empty())
            {
                auto next = std::move(notifications.front());
                notifications.pop_front();
                changes.Raise(next);
            }
            notifying = false;
        }
        catch (...)
        {
            notifying = false;
            notifications.clear();
            throw;
        }
    }
    Status SettingsPage::Impl::Assign(std::string_view id, Value value, ChangeKind kind, ChangeOrigin origin)
    {
        const Node* node = Find(id);
        if (!node || node->control == Control::Group)
            return Fail(std::string(id), "Unknown setting");
        auto checked = ParseValue(*node, ToJson(value));
        if (!checked)
            return Fail(checked.error().location, checked.error().message);
        value = *checked;
        std::string key{id};
        Value old = values.at(key);
        if (old == value)
        {
            error.reset();
            status.clear();
            return {};
        }
        values[key] = value;
        bool staged = config.workflow == Workflow::Staged;
        if (!staged)
            applied[key] = value;
        error.reset();
        status.clear();
        Notify({key, old, value, staged ? ChangeKind::Live : kind, origin,
                staged ? ValueDomain::Working : ValueDomain::Effective});
        if (kind == ChangeKind::Confirmed && !staged)
            AutoSave();
        Paint();
        return {};
    }
    Status SettingsPage::SetValue(std::string_view id, Value value)
    {
        const auto* node = impl_->Find(id);
        if (!node)
            return impl_->Fail(std::string(id), "Unknown setting");
        auto checked = ParseValue(*node, ToJson(value));
        if (!checked)
            return impl_->Fail(checked.error().location, checked.error().message);
        if (impl_->spin && impl_->spin->id == id)
            impl_->AbortSpin();
        // A valid external update supersedes an active draft, including a no-op assignment.
        if (impl_->edit && impl_->edit->id == id)
            impl_->edit.reset();
        if (impl_->colorId == id)
        {
            impl_->colorStart.reset();
            impl_->colorId.clear();
        }
        auto result = impl_->Assign(id, std::move(*checked), ChangeKind::Confirmed, ChangeOrigin::Programmatic);
        impl_->Paint();
        return result;
    }
    Status SettingsPage::Reset(std::string_view id)
    {
        return impl_->ResetNode(id);
    }
    Status SettingsPage::ResetAll()
    {
        return impl_->ResetAll();
    }
    Status SettingsPage::Impl::Apply()
    {
        if (!FinishEdit())
            return std::unexpected(*error);
        if (colorStart)
            CloseColor(false);
        if (config.workflow != Workflow::Staged)
            return {};
        const auto previous = applied;
        applied = values;
        for (const auto& [id, value] : applied)
            if (previous.at(id) != value)
                notifications.push_back(
                    {id, previous.at(id), value, ChangeKind::Confirmed, ChangeOrigin::User, ValueDomain::Effective});
        DrainNotifications();
        Paint();
        return {};
    }
    void SettingsPage::Impl::Cancel()
    {
        EndScrollbarDrag();
        AbortSpin();
        if (config.workflow != Workflow::Staged)
        {
            FinishEdit(true);
            if (colorStart)
                CloseColor(true);
            return;
        }
        edit.reset();
        colorStart.reset();
        colorId.clear();
        auto previous = values;
        values = applied;
        for (const auto& [id, value] : values)
            if (previous.at(id) != value)
                Notify({id, previous.at(id), value, ChangeKind::Live, ChangeOrigin::Cancel, ValueDomain::Working});
        error.reset();
        status.clear();
        Paint();
    }
    void SettingsPage::Impl::AutoSave()
    {
        if (!batch && config.workflow == Workflow::ImmediateAutoSave)
            (void) Write();
    }
    Status SettingsPage::Impl::Write()
    {
        if (config.valuesFile.empty())
            return Fail("save", "No valuesFile was configured");
        Json document{{"version", 1}, {"values", Json::object()}};
        for (const auto& [id, value] : values)
            document["values"][id] = ToJson(value);
        std::string bytes = document.dump(2) + "\n";
        std::string crlf;
        for (char c : bytes)
        {
            if (c == '\n')
                crlf += '\r';
            crlf += c;
        }
        auto temporary = config.valuesFile;
#ifdef _WIN32
        temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
                     std::to_wstring(reinterpret_cast<uintptr_t>(this));
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return Fail("save", "Cannot create temporary settings file");
        DWORD written = 0;
        bool ok = crlf.size() <= MAXDWORD && WriteFile(file, crlf.data(), DWORD(crlf.size()), &written, nullptr) &&
                  written == crlf.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (ok)
            ok = MoveFileExW(temporary.c_str(), config.valuesFile.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok)
        {
            DeleteFileW(temporary.c_str());
            return Fail("save", "Cannot write or replace settings file");
        }
#else
        temporary += ".tmp.XXXXXX";
        std::string path = temporary.string();
        int fd = mkstemp(path.data());
        if (fd < 0)
            return Fail("save", "Cannot create temporary settings file");
        size_t count = 0;
        while (count < crlf.size())
        {
            auto n = write(fd, crlf.data() + count, crlf.size() - count);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            count += size_t(n);
        }
        bool ok = count == crlf.size() && fsync(fd) == 0;
        if (close(fd) != 0)
            ok = false;
        if (ok)
            ok = rename(path.c_str(), config.valuesFile.c_str()) == 0;
        if (!ok)
        {
            unlink(path.c_str());
            return Fail("save", "Cannot write or replace settings file");
        }
#endif
        saved = values;
        error.reset();
        status = "Saved";
        Paint();
        return {};
    }
    Status SettingsPage::Impl::Save()
    {
        auto r = Apply();
        return r ? Write() : r;
    }
    Status SettingsPage::Apply()
    {
        return impl_->Apply();
    }
    void SettingsPage::Cancel()
    {
        impl_->Cancel();
    }
    Status SettingsPage::Save()
    {
        return impl_->Save();
    }
    bool SettingsPage::IsDirty() const
    {
        return impl_->values != impl_->saved;
    }
    bool SettingsPage::HasPendingChanges() const
    {
        return impl_->values != impl_->applied;
    }
    const std::optional<Error>& SettingsPage::LastError() const
    {
        return impl_->error;
    }
    Status SettingsPage::Show()
    {
        return impl_->Show();
    }
    void SettingsPage::Hide()
    {
        impl_->EndScrollbarDrag();
        if (impl_->window && impl_->FinishEdit())
        {
            if (impl_->colorStart)
                impl_->CloseColor(true);
            impl_->window->SetVisible(false);
        }
    }
    bool SettingsPage::IsVisible() const
    {
        return impl_->window && impl_->window->GetVisible();
    }
    void SettingsPage::SetFilter(std::string text)
    {
        impl_->SetFilter(std::move(text));
    }
}  // namespace LWSSettings
