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
// read from the latest-release JSON. Searches only inside that asset's object
// (its fields end with browser_download_url). Returns 64 lowercase hex digits.
inline std::optional<std::string> assetSha256(std::string_view json,std::string_view name) {
    const std::string quoted="\""+std::string(name)+"\"";
    auto at=json.find(quoted);
    if(at==std::string_view::npos) return std::nullopt;
    auto end=json.find("\"browser_download_url\"",at);
    if(end==std::string_view::npos) return std::nullopt;
    constexpr std::string_view prefix="\"sha256:";
    auto d=json.find(prefix,at);
    if(d==std::string_view::npos || d>end || d+prefix.size()+64>json.size()) return std::nullopt;
    std::string hex(json.substr(d+prefix.size(),64));
    for(char& c:hex) {
        if(c>='A' && c<='F') c=char(c-'A'+'a');
        if(!((c>='0' && c<='9') || (c>='a' && c<='f'))) return std::nullopt;
    }
    if(json[d+prefix.size()+64]!='"') return std::nullopt;
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
