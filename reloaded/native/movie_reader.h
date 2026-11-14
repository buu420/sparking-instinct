#pragma once

#include "movie_audio.h"
#include "unreal.h"

namespace sparking {

class GameReader;

class MovieReader {
public:
    MovieReader(GameReader& game, std::filesystem::path narration, MovieReport report = {});
    bool sample(std::span<const ObjectRef> visible_roots);
    void reset();

private:
    ObjectRef find_image(ObjectRef root, std::string_view name);
    bool image_visible(ObjectRef image, ObjectRef root);
    ObjectRef find_player(ObjectRef root, ObjectRef image, ObjectRef material);
    void clear_surface();
    bool sample_player(ObjectRef player);
    void mute();
    void update(const MovieSchedule* movie, int frame);

    GameReader& game_;
    MovieAudio audio_;
    MovieTimeline timeline_;
    ObjectRef root_;
    ObjectRef image_;
    ObjectRef player_;
    ObjectRef component_;
    ObjectRef source_;
    const MovieSchedule* schedule_{};
    MovieClock::time_point next_lookup_{};
};

}
