#pragma once
#include <LWSSettings/SettingsDocument.hpp>
#include <LWSUI/Composites.hpp>
#include <LWSSettings/SettingsTheme.hpp>
#include <LWS/Bitmap.hpp>
#include <array>
namespace LWSSettings
{
    // Composes controls once per definition reload. Binding refresh is silent.
    // Description, search and default highlighting belong here, never in controls.
    namespace internal { class SettingsHeader; struct SettingsHeaderState; }
    struct RowResetPresentation
    {
        std::string caption = "Reset";
        bool enabled = true;
        LWSUI::EditPhase focusLossPhase = LWSUI::EditPhase::Commit;
    };
    enum class SettingValueFilter { All, Changed, Default };
    enum class FormValidationPolicy { SaveValidFields, ShowDecisionDialog };
    struct ValidationDialogOptions
    {
        bool saveValidFields = true;
        bool discardAll = true;
    };
    class SettingsView : public LWSUI::Container
    {
      public:

        explicit SettingsView(SettingsDocument& document, std::string title = "Settings", SettingsTheme theme = {},
                              bool showRevert = false, bool collapseGroups = false);
        ~SettingsView();
        void SetFilter(std::string text);
        std::string GetFilter() const { return search_->Text(); }
        void SetValueFilter(SettingValueFilter filter);
        SettingValueFilter GetValueFilter() const { return valueFilter_; }
        void SetIcon(LWS::BitmapSharedPtr icon);
        void SetTheme(const SettingsTheme& theme);
        // Throws std::logic_error if externally hosted header content has been removed.
        LWSUI::StackPanel& HeaderActions();
        // Optional controls above the form action buttons.
        LWSUI::StackPanel& OperationOptions();
        // Applies presentation to existing rows in one pass; reset behavior stays in the document.
        void SetRowResetPresentation(const std::function<RowResetPresentation(std::string_view)>& present);
        bool Finish(LWSUI::EditPhase phase) override;
        // Returns false for unknown fields. An empty policy restores host inheritance.
        bool SetFieldInvalidEditPolicy(std::string_view id, std::optional<LWSUI::InvalidEditPolicy> policy);
        void SetSaveValidationPolicy(FormValidationPolicy policy) { saveValidationPolicy_ = policy; }
        void SetSaveDialogOptions(ValidationDialogOptions options) { saveDialogOptions_ = options; }
        // Cancellation never depends on field navigation behavior.
        bool PrepareClose();
        bool FinishForClose(FormValidationPolicy policy, ValidationDialogOptions options = {},
                            std::function<void()> closed = {});
        const std::string& ValidationMessage() const { return validation_; }
        void Report(const Status& status);

      protected:

        LWSUI::Size OnMeasure(LWSUI::Size) override;
        void OnArrange() override;
        void OnAttach() override;
        void OnDetach() override;
        bool OnPreviewInput(const LWSUI::Input&) override;

      private:
        // Child roles are owned by this composite.
        using LWSUI::Container::Add;
        using LWSUI::Container::Emplace;
        using LWSUI::Container::Remove;
        using LWSUI::Container::Clear;


        friend class SettingsEditor;
        void MoveHeaderTo(LWSUI::MenuBar& bar);
        struct Binding;
        void Rebuild();
        Binding* FindBinding(std::string_view id) const;
        void Refresh(const Change& change);
        void StyleChanged(Binding&);
        void ReloadValues();
        enum class Command { Save, Apply, Reset, ResetAll, Cancel, Revert };
        void Execute(Command command);
        bool HandleFormCommand(const LWSUI::Input& input);
        void UpdateFilter();
        void RequestFilterUpdate();
        void StyleFilterButtons();
        void ClearSearch();
        void SetGroupsExpanded(bool expanded);
        bool PrepareNavigation();
        void UpdateGroupActions();
        void Describe(const std::string& title, const std::string& path, const std::string& text);
        void StyleLocation();
        void RememberSetting(LWSUI::Control* clicked);
        void RestoreSelectedDescription();
        enum class FormOperation { Save, Apply, Close };
        enum class FormAction { SaveValidFields, DiscardAll };
        struct InvalidField
        {
            std::string id, label;
            LWSUI::ControlHandle editor;
            std::vector<LWSUI::ValidationIssue> issues;
        };
        using ValidationResults = std::vector<InvalidField>;
        ValidationResults CollectInvalidFields() const;
        static std::string FormatValidation(const ValidationResults& invalid);
        bool RequestOperation(FormOperation operation, FormValidationPolicy policy, ValidationDialogOptions options,
                              std::function<void()> closed = {});
        bool CompleteOperation(FormOperation operation, FormAction action, const ValidationResults& invalid);
        void ShowValidationDecision(FormOperation operation, ValidationDialogOptions options,
                                    const ValidationResults& invalid, std::function<void()> closed);
        FormValidationPolicy saveValidationPolicy_ = FormValidationPolicy::ShowDecisionDialog;
        ValidationDialogOptions saveDialogOptions_;
        bool decisionPending_ = false;
        void UpdateStatus();
        void ClearStatus();
        SettingsDocument& document_;
        SettingsTheme theme_;
        bool collapseGroups_ = false;
        SettingValueFilter valueFilter_ = SettingValueFilter::All;
        bool filterPending_ = false, filterDirty_ = false;
        std::array<LWSUI::Button*, 3> filterButtons_{};
        std::array<LWSUI::Button*, 2> groupButtons_{};
        LWSUI::Label* empty_ = nullptr;
        std::shared_ptr<internal::SettingsHeaderState> headerState_;
        internal::SettingsHeader* ownedHeader_ = nullptr;
        LWSUI::ControlHandle externalHeader_;
        LWSUI::Panel* surface_;
        LWSUI::DockPanel* layout_ = nullptr;
        LWSUI::SplitPanel* split_ = nullptr;
        LWSUI::Panel *searchPanel_ = nullptr, *treePanel_ = nullptr, *descriptionPanel_ = nullptr,
                     *footerPanel_ = nullptr;
        LWSUI::FlowPanel* searchFlow_ = nullptr;
        LWSUI::Label* descriptionTitle_;
        LWSUI::TextBox* search_;
        LWSUI::Button* clearSearch_;
        LWSUI::ScrollView* treeScroll_;
        LWSUI::TreeView* tree_;
        LWSUI::ScrollView* descriptionScroll_;
        LWSUI::TextBox* description_;
        LWSUI::TextBox* location_;
        LWSUI::Label* status_;
        LWSUI::FlowPanel* actions_;
        LWSUI::StackPanel* operationOptions_;
        std::vector<LWSUI::Button*> primaryActions_;
        std::string filter_, validation_, operationError_, information_, selectedSetting_;
        std::map<std::string, std::optional<LWSUI::InvalidEditPolicy>, std::less<>> fieldValidation_;
        // Sorted by ID for lookup; visual order belongs to the control tree.
        // Sorting unique_ptrs preserves the addresses captured by editor callbacks.
        std::vector<std::unique_ptr<Binding>> bindings_;
        bool reloadingValues_ = false, preservingDrafts_ = false;
        struct Group
        {
            LWSUI::TreeView::Branch* branch;
            std::string search;
        };
        std::vector<Group> groups_;
        std::vector<LWSUI::Event<void()>::Connection> clicks_, hovers_;
        LWSUI::Event<void(const LWSUI::Input&, bool&)>::Connection commandConnection_;
        LWSUI::Event<void(const std::string&, LWSUI::EditPhase)>::Connection searchConnection_;
        LWSUI::Event<void(const Change&)>::Connection changeConnection_;
        LWSUI::Event<void(const std::string&, EditPhase, ChangeOrigin)>::Connection finishedConnection_;
        LWSUI::Event<void(const Error&)>::Connection errorConnection_;
        LWSUI::Event<void()>::Connection reloadConnection_, valuesReloadConnection_, persistenceConnection_, leaveConnection_;
        LWSUI::Event<void(const std::string&)>::Connection refreshConnection_;
    };
}  // namespace LWSSettings
