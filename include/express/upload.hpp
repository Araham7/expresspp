#pragma once
/* express::upload: multer for Express++ (multipart/form-data file uploads).

     auto upload = express::upload({.dest = "uploads", .limits = {.fileSize = "5mb", .files = 5}});

     app.post("/avatar", upload.single("avatar"), [](express::Req req, express::Res res) {
         auto *f = express::file(req);                    // req.file in multer (nullptr if none was sent)
         res.json(express::obj("saved", f->path, "size", f->size, "name", express::fields(req)["name"]));
     });
     app.post("/photos", upload.array("photos", 8), handler);           // express::files(req)
     app.post("/profile", upload.fields({{"avatar", 1}, {"gallery", 8}}), handler);
     app.post("/any", upload.any(), handler);
     app.post("/form", upload.none(), handler);                         // text fields only, files -> error

   Storage: .dest = "uploads" saves to disk (random name + original extension, folder created);
            .memory = true keeps the bytes in f->buffer instead.
   Filter:  .fileFilter = [](const express::UploadedFile &f) { return f.mimetype.rfind("image/", 0) == 0; }
            (false = file is skipped, like multer's cb(null, false)); throw HttpError to reject the request.
   Errors (multer's codes), as express::HttpError:
            413 "File too large" (LIMIT_FILE_SIZE)     413 "Too many files" (LIMIT_FILE_COUNT)
            400 "Unexpected field" (LIMIT_UNEXPECTED_FILE)   400 "Too many parts" ...
   Remember Drogon's request body limit (1 MB by default): app.set("body limit", "50mb") for big uploads.  */

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "app.hpp"

namespace express {

struct UploadedFile {
    std::string fieldname;     // form field name
    std::string originalname;  // name on the user's computer
    std::string mimetype;      // from the file extension
    size_t size = 0;
    std::string destination;   // disk storage: folder
    std::string filename;      // disk storage: name inside the folder
    std::string path;          // disk storage: destination/filename
    std::string buffer;        // memory storage: the bytes
};

struct UploadLimits {
    std::string fileSize = "10mb";  // per file
    int files = 10;                 // max number of files
    int fields = 1000;              // max number of text fields
    std::string fieldSize = "1mb";  // max size of one text field
};

struct UploadOptions {
    std::string dest = "uploads";
    bool memory = false;
    UploadLimits limits;
    std::function<bool(const UploadedFile &)> fileFilter;
    // disk name chooser: (file) -> name inside dest (default: 32 random hex chars + original extension)
    std::function<std::string(const UploadedFile &)> filename;
};

class Uploader {
  public:
    explicit Uploader(UploadOptions o) : o_(std::move(o)) {}
    Middleware single(const std::string &field) const;
    Middleware array(const std::string &field, int maxCount = 10) const;
    Middleware fields(std::vector<std::pair<std::string, int>> spec) const;
    Middleware any() const;
    Middleware none() const;

  private:
    Middleware make(std::vector<std::pair<std::string, int>> allowed, bool anyField, bool noFiles) const;
    UploadOptions o_;
};

inline Uploader upload(UploadOptions o = {}) { return Uploader(std::move(o)); }

// results stored by the upload middleware
const UploadedFile *file(const Req &req, const std::string &field = "");  // first file (of that field)
const std::vector<UploadedFile> &files(const Req &req);
std::vector<UploadedFile> files(const Req &req, const std::string &field);
const Json &fields(const Req &req);  // text fields as a JSON object (req.body in multer)

}  // namespace express
