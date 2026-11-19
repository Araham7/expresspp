#include "express/static.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>

namespace express {

namespace fs = std::filesystem;

namespace {

std::string httpDate(std::time_t t) {
    std::tm tm{};
    gmtime_r(&t, &tm);
    char b[64];
    std::strftime(b, sizeof b, "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return b;
}

std::time_t parseHttpDate(const std::string &s) {
    std::tm tm{};
    if (!strptime(s.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm)) return -1;
    return timegm(&tm);
}

std::time_t toTimeT(fs::file_time_type ft) {
    auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sys);
}

bool regularFile(const fs::path &p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

}  // namespace

Middleware serveStatic(const std::string &rootDir, StaticOptions o) {
    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::absolute(rootDir, ec), ec);
    return [root, o](Req req, Res res, Next next) {
        std::string m = req.method();
        if (m != "GET" && m != "HEAD") {
            next();
            return;
        }
        const std::string &rel = req.path();
        if (rel.find('\0') != std::string::npos) throw HttpError(400, "Bad Request");

        // split, refuse "..", check dotfiles
        std::vector<std::string> parts;
        size_t i = 0;
        while (i < rel.size()) {
            while (i < rel.size() && (rel[i] == '/' || rel[i] == '\\')) i++;
            size_t j = i;
            while (j < rel.size() && rel[j] != '/' && rel[j] != '\\') j++;
            if (j > i) parts.push_back(rel.substr(i, j - i));
            i = j;
        }
        for (const auto &p : parts) {
            if (p == "..") throw HttpError(403, "Forbidden");
            if (p[0] == '.' && p != ".") {
                if (o.dotfiles == "deny") throw HttpError(403, "Forbidden");
                if (o.dotfiles != "allow") {
                    if (o.fallthrough) return next();
                    throw HttpError(404, "Not Found");
                }
            }
        }
        fs::path file = root;
        for (const auto &p : parts) file /= p;

        std::error_code ec2;
        if (fs::is_directory(file, ec2)) {
            if (o.index.empty()) file.clear();
            else file /= o.index;
        }
        if (!file.empty() && !regularFile(file)) {
            fs::path found;
            for (const auto &ext : o.extensions) {
                fs::path cand = file;
                cand += ext;
                if (regularFile(cand)) {
                    found = cand;
                    break;
                }
            }
            file = found;
        }
        if ((file.empty() || !regularFile(file)) && !o.spaFallback.empty() && !req.accepts("html").empty() &&
            req.header("accept").find("html") != std::string::npos) {
            file = root / o.spaFallback;
        }
        if (file.empty() || !regularFile(file)) {
            if (o.fallthrough) return next();
            throw HttpError(404, "Not Found");
        }
        // the resolved file must still be inside root (symlinks included)
        fs::path real = fs::weakly_canonical(file, ec2);
        auto rootStr = root.string(), realStr = real.string();
        if (realStr.compare(0, rootStr.size(), rootStr) != 0 ||
            (realStr.size() > rootStr.size() && realStr[rootStr.size()] != '/'))
            throw HttpError(403, "Forbidden");

        auto size = fs::file_size(real, ec2);
        std::time_t mtime = toTimeT(fs::last_write_time(real, ec2));
        std::string etag = "W/\"" + std::to_string(size) + "-" + std::to_string(mtime) + "\"";
        if (o.etag) res.set("ETag", etag);
        if (o.lastModified) res.set("Last-Modified", httpDate(mtime));
        std::string cc = "public, max-age=" + std::to_string(o.maxAge);
        if (o.immutable) cc += ", immutable";
        res.set("Cache-Control", cc);

        const std::string &inm = req.header("if-none-match");
        const std::string &ims = req.header("if-modified-since");
        bool fresh = false;
        if (o.etag && !inm.empty())
            fresh = inm == etag || inm == "*" || inm.find(etag) != std::string::npos;
        else if (o.lastModified && !ims.empty()) {
            std::time_t t = parseHttpDate(ims);
            fresh = t >= 0 && mtime <= t;
        }
        if (fresh) {
            res.sendStatus(304);
            return;
        }
        res.sendFile(real.string());
    };
}

}  // namespace express
