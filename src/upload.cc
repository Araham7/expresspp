#include "express/upload.hpp"

#include <drogon/MultiPart.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>

#include "express/body.hpp"

namespace express {

namespace fs = std::filesystem;

namespace {

const char *KEY_FILES = "express.upload.files";
const char *KEY_FIELDS = "express.upload.fields";

std::string lower(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string mimeFromExt(const std::string &ext) {
    static const std::map<std::string, std::string> m = {
        {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"},
        {".webp", "image/webp"}, {".svg", "image/svg+xml"}, {".bmp", "image/bmp"}, {".ico", "image/x-icon"},
        {".pdf", "application/pdf"}, {".txt", "text/plain"}, {".csv", "text/csv"}, {".json", "application/json"},
        {".xml", "application/xml"}, {".html", "text/html"}, {".zip", "application/zip"},
        {".gz", "application/gzip"}, {".tar", "application/x-tar"}, {".mp3", "audio/mpeg"}, {".wav", "audio/wav"},
        {".mp4", "video/mp4"}, {".webm", "video/webm"}, {".mov", "video/quicktime"},
        {".doc", "application/msword"},
        {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".xls", "application/vnd.ms-excel"},
        {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".bin", "application/octet-stream"}, {".hex", "application/octet-stream"}};
    auto it = m.find(lower(ext));
    return it == m.end() ? "application/octet-stream" : it->second;
}

// extension safe to put on disk: ".png" -> ".png", anything odd -> ""
std::string safeExt(const std::string &name) {
    std::string ext = fs::path(name).extension().string();
    if (ext.size() > 10) return "";
    for (size_t i = 1; i < ext.size(); i++)
        if (!std::isalnum(static_cast<unsigned char>(ext[i]))) return "";
    return lower(ext);
}

std::string randomHex(size_t bytes) {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    static const char *hex = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < bytes; i++) {
        unsigned v = static_cast<unsigned>(rng() & 0xFF);
        s += hex[v >> 4];
        s += hex[v & 15];
    }
    return s;
}

}  // namespace

Middleware Uploader::single(const std::string &field) const { return make({{field, 1}}, false, false); }
Middleware Uploader::array(const std::string &field, int maxCount) const { return make({{field, maxCount}}, false, false); }
Middleware Uploader::fields(std::vector<std::pair<std::string, int>> spec) const { return make(std::move(spec), false, false); }
Middleware Uploader::any() const { return make({}, true, false); }
Middleware Uploader::none() const { return make({}, false, true); }

Middleware Uploader::make(std::vector<std::pair<std::string, int>> allowed, bool anyField, bool noFiles) const {
    UploadOptions o = o_;
    size_t maxFile = parseBytes(o.limits.fileSize);
    size_t maxField = parseBytes(o.limits.fieldSize);
    return [o, allowed, anyField, noFiles, maxFile, maxField](Req req, Res, Next next) {
        if (!req.is("multipart")) {  // not a form upload: nothing to do (like multer)
            next();
            return;
        }
        drogon::MultiPartParser parser;
        if (parser.parse(req.raw()) != 0) throw HttpError(400, "Malformed multipart/form-data");

        Json fieldsJson(::Json::objectValue);
        const auto &params = parser.getParameters();
        if (static_cast<int>(params.size()) > o.limits.fields) throw HttpError(413, "Too many fields");
        for (const auto &[k, v] : params) {
            if (v.size() > maxField) throw HttpError(413, "Field value too long");
            fieldsJson[k] = v;
        }

        std::vector<UploadedFile> out;
        std::map<std::string, int> perField;
        const auto &incoming = parser.getFiles();
        if (noFiles && !incoming.empty()) throw HttpError(400, "Unexpected field", obj("field", incoming[0].getItemName()));
        if (static_cast<int>(incoming.size()) > o.limits.files) throw HttpError(413, "Too many files");
        for (const auto &f : incoming) {
            const std::string &field = f.getItemName();
            if (!anyField) {
                auto it = std::find_if(allowed.begin(), allowed.end(), [&](const auto &a) { return a.first == field; });
                if (it == allowed.end()) throw HttpError(400, "Unexpected field", obj("field", field));
                if (++perField[field] > it->second) throw HttpError(400, "Unexpected field", obj("field", field));
            }
            if (f.fileLength() > maxFile) throw HttpError(413, "File too large");

            UploadedFile u;
            u.fieldname = field;
            u.originalname = fs::path(f.getFileName()).filename().string();
            u.mimetype = mimeFromExt(fs::path(u.originalname).extension().string());
            u.size = f.fileLength();
            if (o.fileFilter && !o.fileFilter(u)) continue;
            if (o.memory) {
                u.buffer.assign(f.fileData(), f.fileLength());
            } else {
                std::error_code ec;
                fs::create_directories(o.dest, ec);
                u.destination = o.dest;
                u.filename = o.filename ? fs::path(o.filename(u)).filename().string() : randomHex(16) + safeExt(u.originalname);
                if (u.filename.empty() || u.filename == "." || u.filename == "..") u.filename = randomHex(16);
                u.path = (fs::path(o.dest) / u.filename).string();
                std::ofstream file(u.path, std::ios::binary);
                file.write(f.fileData(), static_cast<std::streamsize>(f.fileLength()));
                if (!file) throw std::runtime_error("Express++ upload: could not write " + u.path);
            }
            out.push_back(std::move(u));
        }
        req.set<std::vector<UploadedFile>>(KEY_FILES, std::move(out));
        req.set<Json>(KEY_FIELDS, std::move(fieldsJson));
        next();
    };
}

const std::vector<UploadedFile> &files(const Req &req) {
    static const std::vector<UploadedFile> none;
    if (!req.has(KEY_FILES)) return none;
    return req.get<std::vector<UploadedFile>>(KEY_FILES);
}

std::vector<UploadedFile> files(const Req &req, const std::string &field) {
    std::vector<UploadedFile> out;
    for (const auto &f : files(req))
        if (f.fieldname == field) out.push_back(f);
    return out;
}

const UploadedFile *file(const Req &req, const std::string &field) {
    for (const auto &f : files(req))
        if (field.empty() || f.fieldname == field) return &f;
    return nullptr;
}

const Json &fields(const Req &req) {
    static const Json empty(::Json::objectValue);
    if (!req.has(KEY_FIELDS)) return empty;
    return req.get<Json>(KEY_FIELDS);
}

}  // namespace express
