#pragma once
/* Built-in template engine (Mustache syntax), used by res.render().

     {{name}}            value, HTML-escaped          {{{html}}} / {{& html}}   value, not escaped
     {{user.name}}       dotted lookup                {{.}}                     current item
     {{#items}}..{{/items}}   loop over an array, or show if truthy (object = enter it)
     {{^items}}..{{/items}}   show if missing / false / empty
     {{! comment }}      {{> header}}  partial (views/header.html)

   express::renderTemplate("Hi {{name}}", express::obj("name", "Asha"))  ->  "Hi Asha"           */

#include <functional>
#include <string>

#include "json.hpp"

namespace express {

// partial(name) returns the partial's template text ("" if missing)
std::string renderTemplate(const std::string &tpl, const Json &data,
                           const std::function<std::string(const std::string &)> &partial = {});

std::string escapeHtml(const std::string &s);

}  // namespace express
