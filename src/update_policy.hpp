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
inline bool isNewer(const Version& candidate,const Version& current) { return candidate>current; }
inline std::string versionText(const Version& v) {
    return std::to_string(v[0])+"."+std::to_string(v[1])+"."+std::to_string(v[2]);
}
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
