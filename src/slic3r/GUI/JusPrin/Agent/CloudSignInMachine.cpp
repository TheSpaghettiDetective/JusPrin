#include "CloudSignInMachine.hpp"

#include "Base64.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Jobs/BoostThreadWorker.hpp"
#include "slic3r/GUI/Jobs/OAuthJob.hpp"
#include "slic3r/GUI/Jobs/ProgressIndicator.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/log/trivial.hpp>
#include <boost/system/system_error.hpp>

#include <openssl/rand.h>
#include <openssl/sha.h>

#include <wx/utils.h>

#include <algorithm>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Agent {

namespace {

std::string base64_url(const std::string& bytes)
{
    std::string text = base64_bytes(bytes);
    std::replace(text.begin(), text.end(), '+', '-');
    std::replace(text.begin(), text.end(), '/', '_');
    text.erase(std::remove(text.begin(), text.end(), '='), text.end());
    return text;
}

std::string random_bytes(std::size_t count)
{
    std::string bytes(count, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), static_cast<int>(count)) != 1)
        throw std::runtime_error("OpenSSL could not produce random bytes for a JusPrin sign-in");
    return bytes;
}

// The worker reports nothing to a progress bar: waiting for the browser is
// not progress anyone watches.
class NoProgress final : public ProgressIndicator
{
public:
    void clear_percent() override {}
    void show_error_info(wxString, int, wxString, wxString) override {}
    void set_range(int) override {}
    void set_cancel_callback(CancelFn) override {}
    void set_progress(int) override {}
    void set_status_text(const char*) override {}
    int  get_range() const override { return 0; }
};

// OrcaSlicer's OAuthJob, with its result handed on once it is final.
class SignInJob final : public Job
{
public:
    SignInJob(const CloudAuthorization& authorization, std::function<void(CloudAuthorizationResult)> done)
        : m_result(std::make_shared<OAuthResult>()), m_done(std::move(done)),
          m_job(OAuthData{OAuthParams{authorization.authorize_url, authorization.client_id, authorization.port,
                                      authorization.redirect_uri, authorization.scope, "code", authorization.success_url,
                                      authorization.failure_url, authorization.token_url, authorization.code_verifier,
                                      authorization.state},
                          m_result})
    {
        // OAuthJob tells this window it has finished; nothing listens there,
        // because finalize() below is where its result is read.
        m_job.set_event_handle(wxGetApp().mainframe);
    }

    // OAuthJob stops its loopback server in its own finalize(). That runs
    // here, on the worker, so the port is free before a sign-in started
    // right after a cancelled one opens it again; the worker would otherwise
    // start the next job before the GUI thread got to finalize this one.
    void process(Ctl& ctl) override
    {
        m_job.process(ctl);
        std::exception_ptr none;
        m_job.finalize(ctl.was_canceled(), none);
    }

    void finalize(bool canceled, std::exception_ptr& eptr) override
    {
        if (eptr) {
            // Another program holding the loopback port is the one failure
            // expected here; the person is told sign-in didn't finish and can
            // try again. Anything else is a fault and goes on.
            try {
                std::rethrow_exception(eptr);
            } catch (const boost::system::system_error& error) {
                BOOST_LOG_TRIVIAL(warning) << "JusPrin sign-in could not listen for the browser: " << error.what();
                eptr = nullptr;
                m_done({false, {}, error.what()});
            }
            return;
        }
        if (canceled)
            return;
        m_done({m_result->success, m_result->access_token, m_result->error_message});
    }

private:
    std::shared_ptr<OAuthResult>                  m_result;
    std::function<void(CloudAuthorizationResult)> m_done;
    OAuthJob                                      m_job;
};

} // namespace

CloudAccountDeps make_cloud_account_deps()
{
    // The worker is a thread of its own, started by the first sign-in: every
    // agent rebuild makes these dependencies, and most never sign in.
    auto worker = std::make_shared<std::unique_ptr<BoostThreadWorker>>();

    CloudAccountDeps deps;
    deps.authorize = [worker](const CloudAuthorization& authorization, std::function<void(CloudAuthorizationResult)> done) {
        if (!*worker)
            *worker = std::make_unique<BoostThreadWorker>(std::make_shared<NoProgress>(), "jusprin_sign_in");
        if (!replace_job(**worker, std::make_unique<SignInJob>(authorization, std::move(done))))
            return false;
        wxLaunchDefaultBrowser(wxString::FromUTF8(authorization.authorize_url));
        return true;
    };
    deps.cancel_authorize = [worker] {
        if (*worker)
            (*worker)->cancel_all();
    };
    deps.pump = [worker] {
        if (*worker)
            (*worker)->process_events();
    };
    deps.get = [](const std::string& url, const std::string& token, std::function<void(CloudHttpResult)> done) {
        auto http = Slic3r::Http::get(url);
        http.header("Authorization", "Bearer " + token)
            .header("Accept", "application/json")
            .tls_verify(true)
            .timeout_connect(10)
            .timeout_max(30)
            .on_complete([done](std::string body, unsigned status) { done({status, std::move(body)}); })
            .on_error([done](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(warning) << "JusPrin account request failed (" << status << "): " << error;
                done({status, std::move(body)});
            })
            .perform();
    };
    deps.revoke = [](const std::string& url, const std::string& client_id, const std::string& token) {
        auto http = Slic3r::Http::post(url);
        http.form_add("token", token)
            .form_add("client_id", client_id)
            .timeout_connect(10)
            .timeout_max(30)
            .on_error([](std::string, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(warning) << "JusPrin sign-out could not revoke the token (" << status << "): " << error;
            })
            .perform();
    };
    deps.pkce = [](std::string& verifier, std::string& challenge, std::string& state) {
        verifier = base64_url(random_bytes(32));
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(verifier.data()), verifier.size(), digest);
        challenge = base64_url(std::string(reinterpret_cast<const char*>(digest), sizeof(digest)));
        state     = base64_url(random_bytes(16));
    };
    return deps;
}

} // namespace Slic3r::GUI::JusPrin::Agent
