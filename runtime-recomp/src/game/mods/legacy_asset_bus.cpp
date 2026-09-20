#include "legacy_asset_bus.hpp"

namespace dkr::mods {
std::uint32_t AssetBus::Mount::address(unsigned section,std::size_t offset) const {
    if(section>=bases_.size() || offset>directory_->section_size(section))
        throw Error("Virtual asset address is outside its mounted section.");
    return bases_[section]+static_cast<std::uint32_t>(offset);
}
Bytes AssetBus::Read::bytes() const {
    if(!mount) throw Error("Virtual asset read has no owner.");
    return mount->directory()->read(section,offset,length);
}
void AssetBus::Read::copy_to_guest(std::span<std::uint8_t> rdram,std::uint32_t address) const {
    if(!mount) throw Error("Virtual asset read has no owner.");
    mount->directory()->copy_to_guest(section,offset,rdram,address,length);
}
std::shared_ptr<const AssetBus::Mount> AssetBus::mount(std::shared_ptr<const AssetDirectory> directory) {
    if(!directory) throw Error("Cannot mount an absent immutable directory.");
    std::lock_guard lock(mutex_);
    const auto fingerprint=directory->fingerprint();
    if(const auto found=banks_.find(fingerprint);found!=banks_.end()) {
        if(auto live=found->second.owner.lock()) return live;
        auto remount=std::make_shared<Mount>();
        remount->directory_=std::move(directory);remount->bases_=found->second.bases;
        // Binding is to actual base+override bytes, not just a package title.
        for(unsigned s=0;s<50;++s)
            if(remount->directory_->section_size(s)!=found->second.sizes[s])
                throw Error("An existing bank fingerprint changed section sizes.");
        found->second.owner=remount;return remount;
    }
    if(banks_.size()>=MaxBanks) throw Error("This game session has reached its mounted-content limit.");
    Bank bank;
    std::uint64_t next=next_;
    for(unsigned s=0;s<50;++s) {
        bank.sizes[s]=directory->section_size(s);
        bank.bases[s]=static_cast<std::uint32_t>(next);
        // A guard page makes a one-past-section pointer distinguishable from
        // the next section, including for empty sections and zero-byte reads.
        next+=((bank.sizes[s]+4095U)&~std::uint64_t(4095U))+4096U;
        if(next>End) throw Error("This game session has exhausted its virtual asset address space.");
    }
    auto mounted=std::make_shared<Mount>();
    mounted->directory_=std::move(directory);mounted->bases_=bank.bases;bank.owner=mounted;
    // Stage the small indexes so allocation failure cannot publish half a bank.
    auto banks=banks_;auto ranges=ranges_;
    banks.emplace(fingerprint,bank);
    for(unsigned s=0;s<50;++s) ranges.emplace(bank.bases[s],Range{fingerprint,s});
    banks_.swap(banks);ranges_.swap(ranges);next_=static_cast<std::uint32_t>(next);
    return mounted;
}
std::optional<AssetBus::Read> AssetBus::resolve(std::uint32_t address,std::size_t length) const {
    if(!is_virtual(address)) return std::nullopt;
    std::lock_guard lock(mutex_);
    auto range=ranges_.upper_bound(address);
    if(range==ranges_.begin()) throw Error("Virtual DMA references an unknown bank.");
    --range;
    const auto& bank=banks_.at(range->second.fingerprint);
    const auto section=range->second.section;
    const auto offset=std::size_t(address-range->first),size=bank.sizes[section];
    if(offset>size || length>size-offset) throw Error("Virtual DMA crosses its section or guard range.");
    auto owner=bank.owner.lock();
    if(!owner) throw Error("Virtual DMA references a retired asset bank.");
    return Read{std::move(owner),section,offset,length};
}
} // namespace dkr::mods
