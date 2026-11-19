#include "express/req.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <strings.h>

namespace express {

namespace detail {

namespace {
std::string lower(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(sep, i);
        if (j == std::string::npos) j = s.size();
        std::string part = trim(s.substr(i, j - i));
        if (!part.empty()) out.push_back(part);
        i = j + 1;
    }
    return out;
}
}  // namespace

std::string uriEncode(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' ||
            c == ')') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string uriDecode(const std::string &s) {
    if (s.find('%') == std::string::npos) return s;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '%') {
            out += s[i];
            continue;
        }
        if (i + 2 >= s.size()) return s;
        int a = val(s[i + 1]), b = val(s[i + 2]);
        if (a < 0 || b < 0) return s;  // like the cookie module: keep the raw value
        out += static_cast<char>((a << 4) | b);
        i += 2;
    }
    return out;
}

std::string mimeFor(const std::string &t) {
    if (t.find('/') != std::string::npos) return lower(t);
    static const std::map<std::string, std::string> m = {
        {"json", "application/json"},       {"html", "text/html"},
        {"text", "text/plain"},             {"txt", "text/plain"},
        {"xml", "application/xml"},         {"css", "text/css"},
        {"js", "application/javascript"},   {"urlencoded", "application/x-www-form-urlencoded"},
        {"form", "application/x-www-form-urlencoded"}, {"multipart", "multipart/form-data"},
        {"png", "image/png"},               {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},             {"svg", "image/svg+xml"},
        {"pdf", "application/pdf"},         {"csv", "text/csv"},
        {"octet", "application/octet-stream"}};
    auto it = m.find(lower(t));
    return it == m.end() ? lower(t) : it->second;
}

static bool mimeMatches(const std::string &pattern, const std::string &mime) {
    if (pattern == "*/*" || pattern == "*") return true;
    auto ps = pattern.find('/'), ms = mime.find('/');
    if (ps == std::string::npos || ms == std::string::npos) return pattern == mime;
    std::string pt = pattern.substr(0, ps), psub = pattern.substr(ps + 1);
    std::string mt = mime.substr(0, ms), msub = mime.substr(ms + 1);
    return (pt == "*" || pt == mt) && (psub == "*" || psub == msub);
}

std::string negotiate(const std::string &accept, const std::vector<std::string> &offered) {
    if (offered.empty()) return "";
    if (trim(accept).empty()) return offered.front();  // no Accept header: client takes anything
    struct Range {
        std::string type;
        double q;
        int specificity;
        size_t order;
    };
    std::vector<Range> ranges;
    size_t order = 0;
    for (const auto &part : split(accept, ',')) {
        auto params = split(part, ';');
        if (params.empty()) continue;
        Range r{lower(params[0]), 1.0, 0, order++};
        for (size_t i = 1; i < params.size(); i++) {
            auto p = lower(params[i]);
            if (p.rfind("q=", 0) == 0) r.q = std::atof(p.c_str() + 2);
        }
        r.specificity = r.type == "*/*" ? 0 : r.type.find("/*") != std::string::npos ? 1 : 2;
        ranges.push_back(r);
    }
    std::string best;
    double bestQ = 0;
    int bestSpec = -1;
    size_t bestOrder = 0, bestOffer = 0;
    for (size_t oi = 0; oi < offered.size(); oi++) {
        std::string mime = mimeFor(offered[oi]);
        // the most specific range that matches decides this offer's quality
        const Range *hit = nullptr;
        for (const auto &r : ranges)
            if (mimeMatches(r.type, mime) && (!hit || r.specificity > hit->specificity)) hit = &r;
        if (!hit || hit->q <= 0) continue;
        bool better = hit->q > bestQ ||
                      (hit->q == bestQ && (hit->specificity > bestSpec ||
                                           (hit->specificity == bestSpec && (hit->order < bestOrder ||
                                                                             (hit->order == bestOrder && oi < bestOffer)))));
        if (best.empty() || better) {
            best = offered[oi];
            bestQ = hit->q;
            bestSpec = hit->specificity;
            bestOrder = hit->order;
            bestOffer = oi;
        }
    }
    return best;
}

bool typeIs(const std::string &contentType, const std::string &t) {
    if (contentType.empty()) return false;
    std::string ct = lower(trim(contentType.substr(0, contentType.find(';'))));
    std::string want = mimeFor(t);
    if (t.rfind("+", 0) == 0) return ct.size() > t.size() && ct.compare(ct.size() - t.size(), t.size(), lower(t)) == 0;
    return mimeMatches(want, ct);
}

}  // namespace detail

std::vector<std::string> Req::ips() const {
    if (!s_->app || !s_->app->trustProxy) return {};
    return detail::split(header("x-forwarded-for"), ',');
}

std::string Req::ip() const {
    if (s_->app && s_->app->trustProxy) {
        auto chain = ips();
        if (!chain.empty()) return chain.front();
    }
    return s_->raw->peerAddr().toIp();
}

std::string Req::hostname() const {
    std::string host;
    if (s_->app && s_->app->trustProxy) {
        auto fwd = detail::split(header("x-forwarded-host"), ',');
        if (!fwd.empty()) host = fwd.front();
    }
    if (host.empty()) host = header("host");
    if (!host.empty() && host[0] == '[') {  // [::1]:8080
        auto close = host.find(']');
        return close == std::string::npos ? host : host.substr(0, close + 1);
    }
    auto colon = host.find(':');
    return colon == std::string::npos ? host : host.substr(0, colon);
}

std::string Req::protocol() const {
    if (s_->app && s_->app->trustProxy) {
        auto fwd = detail::split(header("x-forwarded-proto"), ',');
        if (!fwd.empty()) return detail::lower(fwd.front());
    }
    return s_->raw->isOnSecureConnection() ? "https" : "http";
}

bool Req::xhr() const { return strcasecmp(header("x-requested-with").c_str(), "XMLHttpRequest") == 0; }

bool Req::is(const std::string &type) const {
    if (s_->raw->body().empty()) return false;
    return detail::typeIs(header("content-type"), type);
}

std::unordered_map<std::string, std::string> Req::cookies() const {
    std::unordered_map<std::string, std::string> out;
    for (const auto &[k, v] : s_->raw->cookies()) {
        if (s_->signedNames.count(k)) continue;
        std::string val = v;
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
        out[k] = detail::uriDecode(val);
    }
    return out;
}

}  // namespace express
