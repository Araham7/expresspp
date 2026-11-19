#pragma once
/* express::crypto: Node's built-in `crypto` module for Express++ (OpenSSL underneath).

     namespace crypto = express::crypto;
     std::string token = crypto::randomBytes(32).toString("hex");
     std::string id    = crypto::randomUUID();
     int dice          = crypto::randomInt(1, 7);                                  // 1..6

     crypto::createHash("sha256").update("abc").digest("hex");                   // ba7816bf...
     crypto::createHmac("sha256", secret).update(body).digest("base64");          // webhook signatures
     crypto::timingSafeEqual(sigA, sigB);                                          // compare secrets safely

     auto key = crypto::pbkdf2Sync("password", salt, 100000, 32, "sha256");       // Buffer
     auto key = crypto::scryptSync("password", salt, 32);

     auto iv = crypto::randomBytes(12);
     auto c = crypto::createCipheriv("aes-256-gcm", key, iv);
     std::string enc = c.update("secret data", "utf8", "hex") + c.final("hex");
     auto tag = c.getAuthTag();
     auto d = crypto::createDecipheriv("aes-256-gcm", key, iv);
     d.setAuthTag(tag);
     std::string plain = d.update(enc, "hex", "utf8") + d.final("utf8");          // throws if tampered

   Encodings everywhere: "hex", "base64", "base64url", "utf8" / "binary" / "latin1" (raw bytes).
   Hashes: md5 sha1 sha224 sha256 sha384 sha512 sha3-256 sha3-512 (anything OpenSSL knows).
   Ciphers: aes-128/192/256-gcm, aes-*-cbc, aes-*-ctr, chacha20-poly1305 (anything OpenSSL knows).   */

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace express::crypto {

// Node's Buffer, just the parts crypto needs: raw bytes + conversions
class Buffer {
  public:
    Buffer() = default;
    explicit Buffer(std::string bytes) : bytes_(std::move(bytes)) {}
    static Buffer from(const std::string &text, const std::string &encoding = "utf8");  // "hex" | "base64" | ...
    std::string toString(const std::string &encoding = "utf8") const;
    const std::string &bytes() const { return bytes_; }
    size_t size() const { return bytes_.size(); }
    size_t length() const { return bytes_.size(); }
    bool operator==(const Buffer &o) const { return bytes_ == o.bytes_; }

  private:
    std::string bytes_;
};

class Error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

Buffer randomBytes(size_t n);
std::string randomUUID();
long long randomInt(long long min, long long max);  // [min, max), like Node
long long randomInt(long long max);                  // [0, max)

class Hash {
  public:
    Hash &update(const std::string &data, const std::string &inputEncoding = "utf8");
    Hash &update(const Buffer &data) { return update(data.bytes(), "binary"); }
    std::string digest(const std::string &encoding);
    Buffer digest();
    struct Impl;
    explicit Hash(std::shared_ptr<Impl> i) : i_(std::move(i)) {}

  private:
    std::shared_ptr<Impl> i_;
};

class Hmac {
  public:
    Hmac &update(const std::string &data, const std::string &inputEncoding = "utf8");
    Hmac &update(const Buffer &data) { return update(data.bytes(), "binary"); }
    std::string digest(const std::string &encoding);
    Buffer digest();
    struct Impl;
    explicit Hmac(std::shared_ptr<Impl> i) : i_(std::move(i)) {}

  private:
    std::shared_ptr<Impl> i_;
};

Hash createHash(const std::string &algorithm);
Hmac createHmac(const std::string &algorithm, const std::string &key);
Hmac createHmac(const std::string &algorithm, const Buffer &key);
std::vector<std::string> getHashes();

// constant-time comparison (false when the sizes differ)
bool timingSafeEqual(const std::string &a, const std::string &b);
bool timingSafeEqual(const Buffer &a, const Buffer &b);

Buffer pbkdf2Sync(const std::string &password, const std::string &salt, int iterations, size_t keylen,
                  const std::string &digest = "sha256");
Buffer pbkdf2Sync(const std::string &password, const Buffer &salt, int iterations, size_t keylen,
                  const std::string &digest = "sha256");
struct ScryptOptions {
    uint64_t N = 16384;
    uint64_t r = 8;
    uint64_t p = 1;
};
Buffer scryptSync(const std::string &password, const std::string &salt, size_t keylen, ScryptOptions o = {});
Buffer scryptSync(const std::string &password, const Buffer &salt, size_t keylen, ScryptOptions o = {});

class Cipher;

/* What final() returns. Node code writes `c.update(x, "utf8", "hex") + c.final("hex")`; in C++ the two
   sides of `+` may run in either order, so final() doesn't finish the cipher right away: it finishes
   when its result is used (converted to std::string / Buffer, added with +, or getAuthTag() is called).
   That way the Node one-liner works exactly as in Node.                                            */
class [[nodiscard]] FinalResult {
  public:
    struct Impl;
    FinalResult(std::shared_ptr<void> cipher, std::string encoding) : c_(std::move(cipher)), enc_(std::move(encoding)) {}
    Buffer buffer() const;                       // finishes the cipher (throws if authentication fails)
    std::string str() const;  // in the encoding given to final()
    const std::string bytes() const { return buffer().bytes(); }
    std::string toString(const std::string &enc) const { return buffer().toString(enc); }
    operator std::string() const { return str(); }
    operator Buffer() const { return buffer(); }
    friend std::string operator+(const std::string &a, const FinalResult &b) { return a + b.str(); }
    friend std::string operator+(const char *a, const FinalResult &b) { return std::string(a) + b.str(); }

  private:
    std::shared_ptr<void> c_;
    std::string enc_;
};

class Cipher {
  public:
    // update(data, inputEncoding, outputEncoding) like Node; returns the encrypted/decrypted part
    std::string update(const std::string &data, const std::string &inputEncoding = "utf8",
                       const std::string &outputEncoding = "binary");
    Buffer update(const Buffer &data);
    FinalResult final(const std::string &outputEncoding = "binary");
    Cipher &setAAD(const std::string &aad);  // extra authenticated data (GCM / ChaCha20-Poly1305)
    Buffer getAuthTag() const;                // after final(), encryption only
    Cipher &setAuthTag(const Buffer &tag);    // before final(), decryption only
    Cipher &setAutoPadding(bool on = true);   // CBC
    struct Impl;
    explicit Cipher(std::shared_ptr<Impl> i) : i_(std::move(i)) {}

  private:
    std::shared_ptr<Impl> i_;
};
using Decipher = Cipher;

Cipher createCipheriv(const std::string &algorithm, const Buffer &key, const Buffer &iv);
Decipher createDecipheriv(const std::string &algorithm, const Buffer &key, const Buffer &iv);

}  // namespace express::crypto
