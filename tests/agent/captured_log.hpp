#pragma once

// What the app log receives while a CapturedLog lives. With no formatter, the
// sink writes each record's message alone.

#include <boost/core/null_deleter.hpp>
#include <boost/log/core.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>

#include <ostream>
#include <sstream>
#include <string>

struct CapturedLog
{
    using Sink = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;

    CapturedLog()
    {
        auto backend = boost::make_shared<boost::log::sinks::text_ostream_backend>();
        backend->add_stream(boost::shared_ptr<std::ostream>(&text, boost::null_deleter()));
        sink = boost::make_shared<Sink>(backend);
        boost::log::core::get()->add_sink(sink);
    }
    ~CapturedLog() { boost::log::core::get()->remove_sink(sink); }
    CapturedLog(const CapturedLog&)            = delete;
    CapturedLog& operator=(const CapturedLog&) = delete;

    std::string lines()
    {
        sink->flush();
        return text.str();
    }

    std::ostringstream       text;
    boost::shared_ptr<Sink>  sink;
};
