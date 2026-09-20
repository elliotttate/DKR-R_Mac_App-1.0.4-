#pragma once
#include "legacy_track_catalog.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <cctype>

namespace dkr::mods::browser {
struct Card {
    TrackCatalogItem item;
    unsigned revisions=0;
    std::set<std::string> sources;
};
struct Filters {
    std::string query,source;
    int sort=0,state=0,compatibility=0,visibility=0;
    unsigned revision=0; // 1=v1.0, 2=v1.1; zero means no verified selection.
};
inline std::string lower(std::string text) {
    for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
inline std::vector<Card> cards(const TrackCatalogView& view,bool characters) {
    std::map<std::string,Card> unique;
    for(const auto& item:view.tracks) {
        auto [it,added]=unique.try_emplace(item.id);
        auto& card=it->second;
        if(added) {card.item=item;card.item.managed_bytes=0;}
        card.item.managed_bytes+=item.managed_bytes;
        card.item.enabled|=item.enabled;
        card.sources.insert(item.source_name);
        card.revisions|=characters?3U:item.revision=="us.v77"?1U:2U;
    }
    std::vector<Card> result;for(auto& [id,card]:unique)result.push_back(std::move(card));return result;
}
inline bool compatible(const Card& card,unsigned revision) {return revision!=0 && (card.revisions&revision)!=0;}
inline std::vector<Card> select(const std::vector<Card>& all,const Filters& filters) {
    std::vector<Card> result;const auto query=lower(filters.query);
    for(const auto& card:all) {
        const auto& item=card.item;
        std::string searchable=item.name;for(const auto& source:card.sources)searchable+=' '+source;
        if(!query.empty() && lower(searchable).find(query)==std::string::npos)continue;
        if(!filters.source.empty() && !card.sources.contains(filters.source))continue;
        if((filters.state==1&&!item.enabled)||(filters.state==2&&item.enabled))continue;
        if((filters.visibility==0&&item.hidden)||(filters.visibility==2&&!item.hidden))continue;
        if((filters.compatibility==1&&!compatible(card,filters.revision)) ||
           (filters.compatibility==2&&compatible(card,filters.revision)))continue;
        result.push_back(card);
    }
    std::sort(result.begin(),result.end(),[&](const auto& a,const auto& b) {
        const auto& x=a.item;const auto& y=b.item;
        if(filters.sort==2&&x.managed_bytes!=y.managed_bytes)return x.managed_bytes>y.managed_bytes;
        if(filters.sort==3&&x.managed_bytes!=y.managed_bytes)return x.managed_bytes<y.managed_bytes;
        if((filters.sort==4||filters.sort==5)&&x.imported_at!=y.imported_at) {
            // Unknown historical dates sort last in either direction.
            if(!x.imported_at||!y.imported_at)return x.imported_at!=0;
            return filters.sort==4?x.imported_at>y.imported_at:x.imported_at<y.imported_at;
        }
        const auto left=lower(x.name),right=lower(y.name);
        if(left!=right)return filters.sort==1?left>right:left<right;
        return x.id<y.id;
    });
    return result;
}
inline unsigned active_count(const std::vector<Card>& all) {
    return static_cast<unsigned>(std::count_if(all.begin(),all.end(),[](const auto& card){return card.item.enabled;}));
}
inline bool can_activate(const Card& card,bool characters,unsigned active,unsigned revision,bool locked) {
    return !locked&&!card.item.hidden&&compatible(card,revision)&&(!characters||active<MaxActiveStageCharacters);
}
}
