#include "express/logger.hpp"

#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>

namespace express {

namespace {

std::string fmtMs(double ms) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", ms);
    return b;
}

std::string dateCLF() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char b[64];
    std::strftime(b, sizeof b, "%d/%b/%Y:%H:%M:%S +0000", &tm);
    return b;
}

const char *presetFormat(const std::string &name) {
    if (name == "tiny") return ":method :url :status :res[content-length] - :response-time ms";
    if (name == "short")
        return ":remote-addr :method :url HTTP/:http-version :status :res[content-length] - :response-time ms";
    if (name == "common")
        return ":remote-addr - - [:date] \":method :url HTTP/:http-version\" :status :res[content-length]";
    if (name == "combined")
        return ":remote-addr - - [:date] \":method :url HTTP/:http-version\" :status :res[content-length] "
               "\":referrer\" \":user-agent\"";
    return nullptr;
}

std::string orDash(const std::string &s) { return s.empty() ? "-" : s; }

std::string render(const std::string &fmt, const Req &req, const FinishInfo &i) {
    std::string out;
    for (size_t p = 0; p < fmt.size();) {
        if (fmt[p] != ':') {
            out += fmt[p++];
            continue;
        }
        auto take = [&](const char *tok) {
            size_t n = std::char_traits<char>::length(tok);
            if (fmt.compare(p, n, tok) == 0) {
                p += n;
                return true;
            }
            return false;
        };
        auto bracket = [&](const char *tok, std::string &arg) {
            size_t n = std::char_traits<char>::length(tok);
            if (fmt.compare(p, n, tok) != 0) return false;
            size_t close = fmt.find(']', p + n);
            if (close == std::string::npos) return false;
            arg = fmt.substr(p + n, close - p - n);
            p = close + 1;
            return true;
        };
        std::string arg;
        auto raw = req.raw();
        if (take(":method")) out += req.method();
        else if (take(":url")) out += req.originalUrl() + (raw->query().empty() ? "" : "?" + raw->query());
        else if (take(":status")) out += std::to_string(i.status);
        else if (take(":response-time")) out += fmtMs(i.ms);
        else if (bracket(":res[", arg)) out += (arg == "content-length" ? std::to_string(i.bytes) : "-");
        else if (bracket(":req[", arg)) out += orDash(req.header(arg));
        else if (take(":remote-addr")) out += req.ip();
        else if (take(":date")) out += dateCLF();
        else if (take(":user-agent")) out += orDash(req.header("user-agent"));
        else if (take(":referrer")) out += orDash(req.header("referer"));
        else if (take(":http-version")) out += raw->versionString() ? std::string(raw->versionString()).substr(5) : "1.1";
        else out += fmt[p++];
    }
    return out;
}

std::string renderDev(const Req &req, const FinishInfo &i, bool colour) {
    const char *c = i.status >= 500 ? "\033[31m" : i.status >= 400 ? "\033[33m" : i.status >= 300 ? "\033[36m" : "\033[32m";
    std::string url = req.originalUrl() + (req.raw()->query().empty() ? "" : "?" + req.raw()->query());
    std::string s = req.method() + " " + url + " ";
    s += colour ? std::string(c) + std::to_string(i.status) + "\033[0m" : std::to_string(i.status);
    s += " " + fmtMs(i.ms) + " ms - " + std::to_string(i.bytes);
    return s;
}

}  // namespace

Middleware logger(const std::string &format, LoggerOptions opts) {
    std::string fmt = format;
    bool dev = format == "dev";
    if (const char *p = presetFormat(format)) fmt = p;
    bool colour = !opts.sink;  // colours only when writing to the terminal
    auto sink = opts.sink ? opts.sink : [](const std::string &line) {
        static std::mutex m;
        std::lock_guard<std::mutex> lk(m);
        std::cout << line << '\n' << std::flush;
    };
    auto skip = opts.skip;
    return [fmt, dev, colour, sink, skip](Req req, Res res, Next next) {
        res.onFinish([fmt, dev, colour, sink, skip, req](const FinishInfo &i) {
            if (skip && skip(req, i)) return;
            sink(dev ? renderDev(req, i, colour) : render(fmt, req, i));
        });
        next();
    };
}

}  // namespace express
