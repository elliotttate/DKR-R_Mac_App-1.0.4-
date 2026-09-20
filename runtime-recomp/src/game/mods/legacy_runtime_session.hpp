#pragma once
#include "legacy_track_artifact.hpp"
#include "legacy_resident_assets.hpp"
#include "legacy_character_materialize.hpp"
#include <optional>

namespace dkr::mods {
// Owns all immutable asset-address generations for one running guest session.
// Only the emulated scene-loading thread may publish a scene; UI preparation
// happens before admission and does not write RDRAM.
class RuntimeSession {
public:
    explicit RuntimeSession(std::shared_ptr<const AssetBank> stock,
        std::shared_ptr<const CharacterNamespace> characters=nullptr,
        std::vector<Bytes> shared_textures={});
    void admit(PreparedTrack track);
    // Explicit identity is required even if the carrier matches a loaded scene.
    // An empty identity means the ORIGINAL course, never "keep the last mod".
    void request(std::string content_id,unsigned carrier);
    void begin_scene(std::span<std::uint8_t> guest,unsigned carrier,bool external_course=false);
    ResidentAssetState::Lease acquire();
    const AssetBus& bus() const {return bus_;}
    std::string current_content() const;
    std::uint64_t published_scenes() const;
    std::vector<Root> tracks() const;
    const std::shared_ptr<const CharacterNamespace>& characters() const {return characters_;}
    std::uint32_t character_sample_address(unsigned index)const;
    std::uint32_t character_race_sample_address(unsigned index)const;
private:
    std::shared_ptr<const AssetBank> stock_;
    std::shared_ptr<const CharacterNamespace> characters_;
    std::vector<Bytes> shared_textures_;
    std::shared_ptr<const AssetBank> boot_;
    AssetBus bus_;
    std::vector<std::shared_ptr<const AssetBus::Mount>> character_audio_mounts_;
    std::vector<std::shared_ptr<const AssetBus::Mount>> character_race_audio_mounts_;
    std::shared_ptr<const ResidentBank> original_;
    ResidentAssetState resident_;
    struct Entry {PreparedTrack track;std::shared_ptr<const ResidentBank> resident;};
    std::map<std::string,Entry> admitted_;
    struct Request {std::string id;unsigned carrier;};
    mutable std::mutex mutex_;
    std::optional<Request> requested_;
    std::string current_;
    std::uint64_t scenes_=0;
};
} // namespace dkr::mods
