#include "express/crypto.hpp"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace express::crypto {

namespace {

std::string lower(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string opensslError(const std::string &what) {
    unsigned long e = ERR_get_error();
    char buf[256] = {0};
    if (e) ERR_error_string_n(e, buf, sizeof buf);
    return "express::crypto: " + what + (e ? std::string(": ") + buf : "");
}

const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64encode(const std::string &in, bool url) {
    std::string out;
    size_t i = 0;
    auto put = [&](unsigned v) {
        char c = B64[v & 63];
        if (url) c = c == '+' ? '-' : c == '/' ? '_' : c;
        out += c;
    };
    for (; i + 2 < in.size(); i += 3) {
        unsigned n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) |
                     static_cast<unsigned char>(in[i + 2]);
        put(n >> 18);
        put(n >> 12);
        put(n >> 6);
        put(n);
    }
    if (i + 1 == in.size()) {
        unsigned n = static_cast<unsigned char>(in[i]) << 16;
        put(n >> 18);
        put(n >> 12);
        if (!url) out += "==";
    } else if (i + 2 == in.size()) {
        unsigned n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        put(n >> 18);
        put(n >> 12);
        put(n >> 6);
        if (!url) out += "=";
    }
    return out;
}

std::string b64decode(const std::string &in) {  // accepts both alphabets, padding optional (like Node)
    std::string out;
    unsigned buf = 0;
    int bits = 0;
    for (char c : in) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=') break;
        else continue;  // Node skips whitespace / junk
        buf = (buf << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buf >> bits) & 0xFF);
        }
    }
    return out;
}

std::string hexencode(const std::string &in) {
    static const char *h = "0123456789abcdef";
    std::string out;
    out.reserve(in.size() * 2);
    for (unsigned char c : in) {
        out += h[c >> 4];
        out += h[c & 15];
    }
    return out;
}

std::string hexdecode(const std::string &in) {
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i + 1 < in.size(); i += 2) {  // like Node: stops at the first non-hex pair
        int a = val(in[i]), b = val(in[i + 1]);
        if (a < 0 || b < 0) break;
        out += static_cast<char>((a << 4) | b);
    }
    return out;
}

std::string encode(const std::string &bytes, const std::string &enc) {
    std::string e = lower(enc);
    if (e == "hex") return hexencode(bytes);
    if (e == "base64") return b64encode(bytes, false);
    if (e == "base64url") return b64encode(bytes, true);
    if (e == "utf8" || e == "utf-8" || e == "binary" || e == "latin1" || e == "buffer" || e.empty()) return bytes;
    throw Error("express::crypto: unknown encoding \"" + enc + "\"");
}

std::string decode(const std::string &text, const std::string &enc) {
    std::string e = lower(enc);
    if (e == "hex") return hexdecode(text);
    if (e == "base64" || e == "base64url") return b64decode(text);
    if (e == "utf8" || e == "utf-8" || e == "binary" || e == "latin1" || e == "buffer" || e.empty()) return text;
    throw Error("express::crypto: unknown encoding \"" + enc + "\"");
}

const EVP_MD *md(const std::string &name) {
    const EVP_MD *m = EVP_get_digestbyname(lower(name).c_str());
    if (!m) throw Error("express::crypto: digest method not supported: " + name);
    return m;
}

}  // namespace

// ---------- Buffer ----------

Buffer Buffer::from(const std::string &text, const std::string &encoding) { return Buffer(decode(text, encoding)); }
std::string Buffer::toString(const std::string &encoding) const { return encode(bytes_, encoding); }

// ---------- random ----------

Buffer randomBytes(size_t n) {
    std::string b(n, '\0');
    if (n && RAND_bytes(reinterpret_cast<unsigned char *>(b.data()), static_cast<int>(n)) != 1)
        throw Error(opensslError("RAND_bytes failed"));
    return Buffer(std::move(b));
}

std::string randomUUID() {
    std::string b = randomBytes(16).bytes();
    b[6] = static_cast<char>((b[6] & 0x0F) | 0x40);  // version 4
    b[8] = static_cast<char>((b[8] & 0x3F) | 0x80);  // variant 10
    std::string h = hexencode(b);
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20);
}

long long randomInt(long long min, long long max) {
    if (max <= min) throw Error("express::crypto: randomInt needs min < max");
    unsigned long long range = static_cast<unsigned long long>(max - min);
    // rejection sampling: no modulo bias
    unsigned long long limit = UINT64_MAX - (UINT64_MAX % range);
    for (;;) {
        unsigned long long v;
        if (RAND_bytes(reinterpret_cast<unsigned char *>(&v), sizeof v) != 1) throw Error(opensslError("RAND_bytes failed"));
        if (v < limit) return min + static_cast<long long>(v % range);
    }
}
long long randomInt(long long max) { return randomInt(0, max); }

// ---------- Hash ----------

struct Hash::Impl {
    EVP_MD_CTX *ctx = nullptr;
    bool done = false;
    ~Impl() {
        if (ctx) EVP_MD_CTX_free(ctx);
    }
};

Hash createHash(const std::string &algorithm) {
    auto i = std::make_shared<Hash::Impl>();
    i->ctx = EVP_MD_CTX_new();
    if (!i->ctx || EVP_DigestInit_ex(i->ctx, md(algorithm), nullptr) != 1) throw Error(opensslError("hash init failed"));
    return Hash(i);
}

Hash &Hash::update(const std::string &data, const std::string &enc) {
    if (i_->done) throw Error("express::crypto: Digest already called");
    std::string b = decode(data, enc);
    EVP_DigestUpdate(i_->ctx, b.data(), b.size());
    return *this;
}

Buffer Hash::digest() {
    if (i_->done) throw Error("express::crypto: Digest already called");
    i_->done = true;
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_DigestFinal_ex(i_->ctx, out, &len) != 1) throw Error(opensslError("digest failed"));
    return Buffer(std::string(reinterpret_cast<char *>(out), len));
}
std::string Hash::digest(const std::string &encoding) { return digest().toString(encoding); }

std::vector<std::string> getHashes() {
    return {"md5", "sha1", "sha224", "sha256", "sha384", "sha512", "sha512-256", "sha3-224", "sha3-256", "sha3-384", "sha3-512",
            "blake2b512", "blake2s256", "ripemd160"};
}

// ---------- Hmac ----------

struct Hmac::Impl {
    EVP_MAC *mac = nullptr;
    EVP_MAC_CTX *ctx = nullptr;
    bool done = false;
    ~Impl() {
        if (ctx) EVP_MAC_CTX_free(ctx);
        if (mac) EVP_MAC_free(mac);
    }
};

Hmac createHmac(const std::string &algorithm, const std::string &key) {
    std::string digest = EVP_MD_get0_name(md(algorithm));  // validates + canonical name
    auto i = std::make_shared<Hmac::Impl>();
    i->mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    i->ctx = i->mac ? EVP_MAC_CTX_new(i->mac) : nullptr;
    OSSL_PARAM params[] = {OSSL_PARAM_construct_utf8_string("digest", digest.data(), 0), OSSL_PARAM_construct_end()};
    // an empty key is legal for HMAC; OpenSSL wants a non-null pointer
    static const unsigned char zero = 0;
    const unsigned char *k = key.empty() ? &zero : reinterpret_cast<const unsigned char *>(key.data());
    if (!i->ctx || EVP_MAC_init(i->ctx, k, key.size(), params) != 1) throw Error(opensslError("hmac init failed"));
    return Hmac(i);
}
Hmac createHmac(const std::string &algorithm, const Buffer &key) { return createHmac(algorithm, key.bytes()); }

Hmac &Hmac::update(const std::string &data, const std::string &enc) {
    if (i_->done) throw Error("express::crypto: Digest already called");
    std::string b = decode(data, enc);
    EVP_MAC_update(i_->ctx, reinterpret_cast<const unsigned char *>(b.data()), b.size());
    return *this;
}

Buffer Hmac::digest() {
    if (i_->done) throw Error("express::crypto: Digest already called");
    i_->done = true;
    unsigned char out[EVP_MAX_MD_SIZE];
    size_t len = 0;
    if (EVP_MAC_final(i_->ctx, out, &len, sizeof out) != 1) throw Error(opensslError("hmac failed"));
    return Buffer(std::string(reinterpret_cast<char *>(out), len));
}
std::string Hmac::digest(const std::string &encoding) { return digest().toString(encoding); }

// ---------- compare / KDF ----------

bool timingSafeEqual(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}
bool timingSafeEqual(const Buffer &a, const Buffer &b) { return timingSafeEqual(a.bytes(), b.bytes()); }

Buffer pbkdf2Sync(const std::string &password, const std::string &salt, int iterations, size_t keylen, const std::string &digest) {
    if (iterations < 1) throw Error("express::crypto: iterations must be >= 1");
    std::string out(keylen, '\0');
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), reinterpret_cast<const unsigned char *>(salt.data()),
                          static_cast<int>(salt.size()), iterations, md(digest), static_cast<int>(keylen),
                          reinterpret_cast<unsigned char *>(out.data())) != 1)
        throw Error(opensslError("pbkdf2 failed"));
    return Buffer(std::move(out));
}
Buffer pbkdf2Sync(const std::string &password, const Buffer &salt, int iterations, size_t keylen, const std::string &digest) {
    return pbkdf2Sync(password, salt.bytes(), iterations, keylen, digest);
}

Buffer scryptSync(const std::string &password, const std::string &salt, size_t keylen, ScryptOptions o) {
    std::string out(keylen, '\0');
    if (EVP_PBE_scrypt(password.data(), password.size(), reinterpret_cast<const unsigned char *>(salt.data()), salt.size(), o.N, o.r,
                       o.p, 0, reinterpret_cast<unsigned char *>(out.data()), keylen) != 1)
        throw Error(opensslError("scrypt failed (check N/r/p)"));
    return Buffer(std::move(out));
}
Buffer scryptSync(const std::string &password, const Buffer &salt, size_t keylen, ScryptOptions o) {
    return scryptSync(password, salt.bytes(), keylen, o);
}

// ---------- Cipher ----------

struct Cipher::Impl {
    EVP_CIPHER_CTX *ctx = nullptr;
    bool encrypt = true, aead = false;
    bool finalRequested = false, finalDone = false;
    std::string tag, finalOut;
    std::string b64carry;  // base64 output: bytes not yet encoded (keeps chunks joinable, like Node)
    ~Impl() {
        if (ctx) EVP_CIPHER_CTX_free(ctx);
    }
};

static Cipher makeCipher(const std::string &algorithm, const Buffer &key, const Buffer &iv, bool encrypt) {
    const EVP_CIPHER *c = EVP_get_cipherbyname(lower(algorithm).c_str());
    if (!c) throw Error("express::crypto: unknown cipher " + algorithm);
    if (static_cast<int>(key.size()) != EVP_CIPHER_key_length(c))
        throw Error("express::crypto: invalid key length (" + algorithm + " needs " + std::to_string(EVP_CIPHER_key_length(c)) +
                    " bytes)");
    auto i = std::make_shared<Cipher::Impl>();
    i->encrypt = encrypt;
    i->aead = (EVP_CIPHER_flags(c) & EVP_CIPH_FLAG_AEAD_CIPHER) != 0;
    i->ctx = EVP_CIPHER_CTX_new();
    if (!i->ctx || EVP_CipherInit_ex(i->ctx, c, nullptr, nullptr, nullptr, encrypt ? 1 : 0) != 1)
        throw Error(opensslError("cipher init failed"));
    if (i->aead) {
        if (EVP_CIPHER_CTX_ctrl(i->ctx, EVP_CTRL_AEAD_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1)
            throw Error("express::crypto: invalid IV length");
    } else if (static_cast<int>(iv.size()) != EVP_CIPHER_iv_length(c)) {
        throw Error("express::crypto: invalid IV length (" + algorithm + " needs " + std::to_string(EVP_CIPHER_iv_length(c)) + " bytes)");
    }
    if (EVP_CipherInit_ex(i->ctx, nullptr, nullptr, reinterpret_cast<const unsigned char *>(key.bytes().data()),
                          reinterpret_cast<const unsigned char *>(iv.bytes().data()), encrypt ? 1 : 0) != 1)
        throw Error(opensslError("cipher key/iv setup failed"));
    return Cipher(i);
}

Cipher createCipheriv(const std::string &algorithm, const Buffer &key, const Buffer &iv) { return makeCipher(algorithm, key, iv, true); }
Decipher createDecipheriv(const std::string &algorithm, const Buffer &key, const Buffer &iv) { return makeCipher(algorithm, key, iv, false); }

Buffer Cipher::update(const Buffer &data) {
    if (i_->finalDone) throw Error("express::crypto: update after final");
    std::string out(data.size() + EVP_MAX_BLOCK_LENGTH, '\0');
    int len = 0;
    if (EVP_CipherUpdate(i_->ctx, reinterpret_cast<unsigned char *>(out.data()), &len,
                         reinterpret_cast<const unsigned char *>(data.bytes().data()), static_cast<int>(data.size())) != 1)
        throw Error(opensslError("cipher update failed"));
    out.resize(static_cast<size_t>(len));
    return Buffer(std::move(out));
}

static bool isB64(const std::string &enc) {
    std::string e = lower(enc);
    return e == "base64" || e == "base64url";
}

std::string Cipher::update(const std::string &data, const std::string &inEnc, const std::string &outEnc) {
    std::string out = update(Buffer::from(data, inEnc)).bytes();
    if (!isB64(outEnc)) return encode(out, outEnc);
    std::string all = i_->b64carry + out;
    size_t keep = all.size() % 3;
    i_->b64carry = all.substr(all.size() - keep);
    return encode(all.substr(0, all.size() - keep), outEnc);
}

static Buffer finishCipher(Cipher::Impl &c) {
    if (c.finalDone) return Buffer(c.finalOut);
    c.finalDone = true;
    if (!c.encrypt && c.aead) {
        if (c.tag.empty()) throw Error("express::crypto: setAuthTag() is required before final() for AEAD ciphers");
        EVP_CIPHER_CTX_ctrl(c.ctx, EVP_CTRL_AEAD_SET_TAG, static_cast<int>(c.tag.size()), c.tag.data());
    }
    std::string out(EVP_MAX_BLOCK_LENGTH, '\0');
    int len = 0;
    if (EVP_CipherFinal_ex(c.ctx, reinterpret_cast<unsigned char *>(out.data()), &len) != 1) {
        ERR_clear_error();
        throw Error(c.aead ? "express::crypto: Unsupported state or unable to authenticate data"
                           : "express::crypto: bad decrypt (wrong key/iv or corrupted data)");
    }
    out.resize(static_cast<size_t>(len));
    if (c.encrypt && c.aead) {
        c.tag.assign(16, '\0');
        EVP_CIPHER_CTX_ctrl(c.ctx, EVP_CTRL_AEAD_GET_TAG, 16, c.tag.data());
    }
    c.finalOut = out;
    return Buffer(std::move(out));
}

FinalResult Cipher::final(const std::string &outEnc) {
    if (i_->finalRequested) throw Error("express::crypto: final already called");
    i_->finalRequested = true;
    return FinalResult(std::static_pointer_cast<void>(i_), outEnc);
}

Buffer FinalResult::buffer() const { return finishCipher(*std::static_pointer_cast<Cipher::Impl>(c_)); }

std::string FinalResult::str() const {
    auto &c = *std::static_pointer_cast<Cipher::Impl>(c_);
    std::string out = finishCipher(c).bytes();
    if (isB64(enc_)) out = c.b64carry + out;
    return encode(out, enc_);
}

Cipher &Cipher::setAAD(const std::string &aad) {
    int len = 0;
    if (EVP_CipherUpdate(i_->ctx, nullptr, &len, reinterpret_cast<const unsigned char *>(aad.data()), static_cast<int>(aad.size())) != 1)
        throw Error(opensslError("setAAD failed"));
    return *this;
}

Buffer Cipher::getAuthTag() const {
    if (!i_->encrypt || !i_->finalRequested || !i_->aead)
        throw Error("express::crypto: getAuthTag() only after final() of an AEAD encryption");
    finishCipher(*i_);
    return Buffer(i_->tag);
}

Cipher &Cipher::setAuthTag(const Buffer &tag) {
    if (i_->encrypt || !i_->aead) throw Error("express::crypto: setAuthTag() is for AEAD decryption");
    i_->tag = tag.bytes();
    return *this;
}

Cipher &Cipher::setAutoPadding(bool on) {
    EVP_CIPHER_CTX_set_padding(i_->ctx, on ? 1 : 0);
    return *this;
}

}  // namespace express::crypto
