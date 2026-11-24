#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace glimpse {
using Version = std::array<int,3>;
// Accepts "1.2.3" or "v1.2.3" (a release tag); anything else is rejected.
inline std::optional<Version> parseVersion(std::string_view text) {
    if(!text.empty() && (text[0]=='v' || text[0]=='V')) text.remove_prefix(1);
    Version v{};
    size_t part=0, digits=0;
    for(char c:text) {
        if(c>='0' && c<='9') {
            if(++digits>6) return std::nullopt;
            v[part]=v[part]*10+(c-'0');
        } else if(c=='.' && digits && part<2) { ++part; digits=0; }
        else return std::nullopt;
    }
    if(part!=2 || !digits) return std::nullopt;
    return v;
}
// The "sha256:<hex>" digest GitHub publishes for the release asset called `name`,
// read from the latest-release JSON. Only the JSON object holding that asset's
// "name" is searched, whatever order its fields come in, so another asset's digest
// is never used. Returns 64 lowercase hex digits.
inline std::optional<std::string> assetSha256(std::string_view json,std::string_view name) {
    const std::string quoted="\""+std::string(name)+"\"";
    const auto at=json.find(quoted);
    if(at==std::string_view::npos) return std::nullopt;
    // Find the object enclosing `at`, skipping braces inside strings.
    std::array<size_t,64> starts{};
    size_t depth=0, begin=std::string_view::npos, end=std::string_view::npos;
    for(size_t i=0;i<json.size() && end==std::string_view::npos;++i) {
        const char c=json[i];
        if(c=='"') {
            if(i==at && depth) begin=starts[depth-1];
            for(++i;i<json.size() && json[i]!='"';++i) if(json[i]=='\\') ++i;
        } else if(c=='{') {
            if(depth==starts.size()) return std::nullopt;
            starts[depth++]=i;
        } else if(c=='}') {
            if(!depth) return std::nullopt;
            if(begin==starts[--depth]) end=i;
        }
    }
    if(end==std::string_view::npos) return std::nullopt;
    const std::string_view object=json.substr(begin,end-begin);
    constexpr std::string_view prefix="\"sha256:";
    const auto d=object.find(prefix);
    if(d==std::string_view::npos || d+prefix.size()+64>=object.size()) return std::nullopt;
    std::string hex(object.substr(d+prefix.size(),64));
    for(char& c:hex) {
        if(c>='A' && c<='F') c=char(c-'A'+'a');
        if(!((c>='0' && c<='9') || (c>='a' && c<='f'))) return std::nullopt;
    }
    if(object[d+prefix.size()+64]!='"') return std::nullopt;
    return hex;
}
inline bool isNewer(const Version& candidate,const Version& current) { return candidate>current; }
inline std::string versionText(const Version& v) {
    return std::to_string(v[0])+"."+std::to_string(v[1])+"."+std::to_string(v[2]);
}
inline std::string installerName(const Version& v) { return "Glimpse-Setup-"+versionText(v)+".exe"; }
// Pulls the top-level "tag_name" string out of GitHub's latest-release JSON.
// Only the tag is trusted; the download URL is built from the parsed version.
inline std::optional<std::string> releaseTag(std::string_view json) {
    constexpr std::string_view key="\"tag_name\"";
    auto at=json.find(key);
    if(at==std::string_view::npos) return std::nullopt;
    at+=key.size();
    while(at<json.size() && (json[at]==' ' || json[at]=='\t' || json[at]=='\r' || json[at]=='\n')) ++at;
    if(at>=json.size() || json[at]!=':') return std::nullopt;
    ++at;
    while(at<json.size() && (json[at]==' ' || json[at]=='\t' || json[at]=='\r' || json[at]=='\n')) ++at;
    if(at>=json.size() || json[at]!='"') return std::nullopt;
    auto end=json.find('"',++at);
    if(end==std::string_view::npos || end-at>64) return std::nullopt;
    return std::string(json.substr(at,end-at));
}
}
