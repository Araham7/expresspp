#include "express/view.hpp"

#include <stdexcept>
#include <vector>

namespace express {

std::string escapeHtml(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c;
        }
    }
    return o;
}

namespace {

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

const Json *lookup(const std::vector<const Json *> &stack, const std::string &name) {
    static const Json null;
    if (name == ".") return stack.back();
    std::string first = name.substr(0, name.find('.'));
    const Json *found = nullptr;
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
        if ((*it)->isObject() && (*it)->isMember(first)) {
            found = &(**it)[first];
            break;
        }
    if (!found) return &null;
    size_t pos = first.size();
    while (pos < name.size()) {
        size_t next = name.find('.', pos + 1);
        std::string key = name.substr(pos + 1, next == std::string::npos ? std::string::npos : next - pos - 1);
        if (!found->isObject() || !found->isMember(key)) return &null;
        found = &(*found)[key];
        pos = next == std::string::npos ? name.size() : next;
    }
    return found;
}

std::string toText(const Json &v) {
    if (v.isNull()) return "";
    if (v.isString()) return v.asString();
    if (v.isBool()) return v.asBool() ? "true" : "false";
    if (v.isIntegral()) return v.isInt64() ? std::to_string(v.asInt64()) : std::to_string(v.asUInt64());
    if (v.isDouble()) {
        std::string s = std::to_string(v.asDouble());
        s.erase(s.find_last_not_of('0') + 1);
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }
    return stringify(v);
}

bool truthy(const Json &v) {
    if (v.isNull()) return false;
    if (v.isBool()) return v.asBool();
    if (v.isArray() || v.isObject()) return !v.empty();
    if (v.isString()) return !v.asString().empty();
    if (v.isNumeric()) return v.asDouble() != 0;
    return true;
}

struct Renderer {
    const std::function<std::string(const std::string &)> &partial;
    int depth = 0;

    // find the {{/name}} that closes a section opened just before `from`
    size_t findClose(const std::string &t, size_t from, const std::string &name, size_t &closeEnd) {
        int level = 1;
        size_t pos = from;
        while (true) {
            size_t open = t.find("{{", pos);
            if (open == std::string::npos) throw std::runtime_error("Express++ view: unclosed section {{#" + name + "}}");
            size_t close = t.find("}}", open + 2);
            if (close == std::string::npos) throw std::runtime_error("Express++ view: unclosed tag");
            std::string tag = trim(t.substr(open + 2, close - open - 2));
            if ((tag[0] == '#' || tag[0] == '^') && trim(tag.substr(1)) == name) level++;
            if (tag[0] == '/' && trim(tag.substr(1)) == name && --level == 0) {
                closeEnd = close + 2;
                return open;
            }
            pos = close + 2;
        }
    }

    std::string render(const std::string &t, std::vector<const Json *> &stack) {
        if (++depth > 64) throw std::runtime_error("Express++ view: template nesting too deep (partial loop?)");
        std::string out;
        size_t pos = 0;
        while (pos < t.size()) {
            size_t open = t.find("{{", pos);
            if (open == std::string::npos) {
                out += t.substr(pos);
                break;
            }
            out += t.substr(pos, open - pos);
            bool triple = t.compare(open, 3, "{{{") == 0;
            size_t close = t.find(triple ? "}}}" : "}}", open + (triple ? 3 : 2));
            if (close == std::string::npos) throw std::runtime_error("Express++ view: unclosed tag");
            std::string tag = trim(t.substr(open + (triple ? 3 : 2), close - open - (triple ? 3 : 2)));
            size_t after = close + (triple ? 3 : 2);

            if (triple) {
                out += toText(*lookup(stack, tag));
            } else if (tag.empty()) {
            } else if (tag[0] == '!') {
            } else if (tag[0] == '&') {
                out += toText(*lookup(stack, trim(tag.substr(1))));
            } else if (tag[0] == '>') {
                if (partial) out += render(partial(trim(tag.substr(1))), stack);
            } else if (tag[0] == '#' || tag[0] == '^') {
                std::string name = trim(tag.substr(1));
                size_t closeEnd = 0;
                size_t closeStart = findClose(t, after, name, closeEnd);
                std::string inner = t.substr(after, closeStart - after);
                const Json &v = *lookup(stack, name);
                if (tag[0] == '^') {
                    if (!truthy(v)) out += render(inner, stack);
                } else if (v.isArray()) {
                    for (const auto &item : v) {
                        stack.push_back(&item);
                        out += render(inner, stack);
                        stack.pop_back();
                    }
                } else if (truthy(v)) {
                    stack.push_back(&v);
                    out += render(inner, stack);
                    stack.pop_back();
                }
                after = closeEnd;
            } else if (tag[0] == '/') {
                throw std::runtime_error("Express++ view: unexpected {{" + tag + "}}");
            } else {
                out += escapeHtml(toText(*lookup(stack, tag)));
            }
            pos = after;
        }
        depth--;
        return out;
    }
};

}  // namespace

std::string renderTemplate(const std::string &tpl, const Json &data,
                           const std::function<std::string(const std::string &)> &partial) {
    Renderer r{partial};
    std::vector<const Json *> stack{&data};
    return r.render(tpl, stack);
}

}  // namespace express
