#include "express/json.hpp"

#include <memory>
#include <sstream>
#include <stdexcept>

namespace express {

Json parseJson(std::string_view text) {
    thread_local std::unique_ptr<::Json::CharReader> r(::Json::CharReaderBuilder().newCharReader());
    Json out;
    std::string errs;
    if (!r->parse(text.data(), text.data() + text.size(), &out, &errs))
        throw std::runtime_error("Express++: invalid JSON: " + errs);
    return out;
}

std::string stringify(const Json &v, bool pretty) {
    // building a StreamWriterBuilder is expensive (it is itself a Json::Value): make the writers once per thread
    thread_local std::unique_ptr<::Json::StreamWriter> compact, indented;
    auto &w = pretty ? indented : compact;
    if (!w) {
        ::Json::StreamWriterBuilder b;
        b["indentation"] = pretty ? "  " : "";
        b["emitUTF8"] = true;
        w.reset(b.newStreamWriter());
    }
    std::ostringstream out;
    w->write(v, &out);
    return out.str();
}

}  // namespace express
