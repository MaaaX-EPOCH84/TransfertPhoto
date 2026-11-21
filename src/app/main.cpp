#include "core/version.hpp"

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/string.h>

namespace {

class App final : public wxApp {
public:
    bool OnInit() override
    {
        if (!wxApp::OnInit()) {
            return false;
        }

        const auto version = transfertphoto::version();
        const auto title = wxString::Format(
            "TransfertPhoto %s", wxString::FromUTF8(version.data(), version.size()));

        // Not leaked: wxWidgets owns top-level windows and destroys them on close.
        auto* frame = new wxFrame(nullptr, wxID_ANY, title);
        frame->Show();
        return true;
    }
};

} // namespace

wxIMPLEMENT_APP(App);
