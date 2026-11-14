#include "movie_reader.h"
#include "game_reader.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sparking {
namespace {
using namespace std::chrono_literals;
constexpr std::string_view tutorial_menu = "WBP_TEST_Tutorial_MoviePlayer_C";
constexpr std::string_view fullscreen_movie = "WBP_Sd2FullScreenMovie_C";
constexpr std::size_t widget_limit = 64;
constexpr std::size_t parent_limit = 16;
constexpr std::size_t player_limit = 32;
constexpr std::size_t component_limit = 8;
constexpr auto lookup_interval = 240ms;
}

MovieReader::MovieReader(GameReader& game, std::filesystem::path narration, MovieReport report)
    : game_(game), audio_(std::move(narration), std::move(report)) {}

ObjectRef MovieReader::find_image(ObjectRef root, std::string_view name)
{
    const auto reflected = game_.object(root, name);
    if (reflected) return reflected;

    const auto tree = game_.object(root, "WidgetTree");
    if (!tree) return {};
    std::vector<ObjectRef> pending{root};
    ObjectRef result;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto widget = pending[i];
        if (!game_.visible(widget)) continue;
        if (game_.engine().outer(widget) == tree && game_.engine().name_of(widget) == name) {
            if (result) return {};
            result = widget;
            continue;
        }
        for (const auto child : game_.widget_children(widget)) {
            if (pending.size() == widget_limit) break;
            if (std::find(pending.begin(), pending.end(), child) == pending.end()) pending.push_back(child);
        }
    }
    return result;
}

bool MovieReader::image_visible(ObjectRef image, ObjectRef root)
{
    auto& engine = game_.engine();
    if (!image || !engine.is_a(image, engine.find_class("Image")) ||
        game_.number(image, "ColorAndOpacity.A").value_or(0.0) <= 0.001 ||
        !game_.geometry(image)) return false;

    const auto tree_root = game_.object(root, "WidgetTree.RootWidget");
    const auto switcher = engine.find_class("WidgetSwitcher");
    for (std::size_t depth = 0; depth < parent_limit; ++depth) {
        if (!game_.visible(image)) return false;
        if (image == root) return true;
        // The root of a widget tree has no panel parent.
        if (image == tree_root) return game_.visible(root);
        const auto parent = game_.call_object(image, "GetParent");
        if (!parent || (engine.is_a(parent, switcher) && game_.call_object(parent, "GetActiveWidget") != image))
            return false;
        image = parent;
    }
    return false;
}

ObjectRef MovieReader::find_player(ObjectRef root, ObjectRef image, ObjectRef material)
{
    auto& engine = game_.engine();
    // Setup binds an image to its CRI player and puts that player's dynamic
    // material in the image brush. Both identities must match the live view.
    if (engine.class_name(root) == tutorial_menu) {
        ObjectRef result;
        for (const auto actor : engine.objects("SSTutorialMoviePlayer", {}, player_limit, 0, 0, false)) {
            if (game_.object(actor, "TutorialMovieMenu") != root) continue;
            const auto candidate = game_.object(actor, "CriMediaPlayer");
            if (!candidate || game_.object(candidate, "Image") != image) continue;
            if (result && result != candidate) return {};
            result = candidate;
        }
        if (result) return result;
    }
    auto binding_matches = [&](ObjectRef candidate) {
        return candidate && game_.object(candidate, "Image") == image &&
               game_.object(candidate, "DynamicManaMaterial") == material;
    };
    const auto owner = engine.outer(material);
    if (engine.is_a(owner, engine.find_class("SSCriMediaPlayer")) && binding_matches(owner)) return owner;

    ObjectRef result;
    for (const auto candidate : engine.objects("SSCriMediaPlayer", {}, player_limit, 0, 0, false)) {
        if (!binding_matches(candidate)) continue;
        if (result) return {};
        result = candidate;
    }
    return result;
}

void MovieReader::clear_surface()
{
    root_ = {};
    image_ = {};
    player_ = {};
    next_lookup_ = {};
    mute();
}

bool MovieReader::sample(std::span<const ObjectRef> visible_roots)
{
    ObjectRef root;
    ObjectRef image;
    ObjectRef material;
    for (const auto candidate : visible_roots) {
        const auto type = game_.engine().class_name(candidate);
        if (type != tutorial_menu && type != fullscreen_movie) continue;
        if (!game_.visible(candidate)) continue;
        const auto widget = candidate == root_ && game_.engine().live(image_) ? image_ :
            find_image(candidate, type == tutorial_menu ? "IMG_Movie_Area" : "MovieImage");
        const auto brush = game_.object(widget, "Brush.ResourceObject");
        if (!brush || !image_visible(widget, candidate)) continue;
        if (root) {
            clear_surface();
            return false;
        }
        root = candidate;
        image = widget;
        material = brush;
    }
    if (!root) {
        if (root_) clear_surface();
        return false;
    }
    if (root_ != root || image_ != image) {
        clear_surface();
        root_ = root;
        image_ = image;
    }

    if (!game_.engine().live(player_) || game_.object(player_, "Image") != image ||
        game_.object(player_, "DynamicManaMaterial") != material) player_ = {};
    const auto now = MovieClock::now();
    if (!player_ && now >= next_lookup_) {
        const auto candidate = find_player(root, image, material);
        if (candidate && game_.object(candidate, "DynamicManaMaterial") == material) player_ = candidate;
        next_lookup_ = now + lookup_interval;
    }
    if (!player_) {
        mute();
        return false;
    }
    const auto playing = game_.call_bool(player_, "IsPlaying");
    const auto current = game_.integer(player_, "ManaPlayerNo");
    const auto components = game_.objects(player_, "ManaComponents", component_limit);
    if (!playing || !current || *current < 0 || static_cast<std::size_t>(*current) >= components.size()) {
        mute();
        return false;
    }
    const auto paused = game_.call_bool(player_, "IsPaused");
    const auto fading = game_.call_bool(player_, "IsCrossFading");
    if (!playing.value_or(false) || paused.value_or(true) || fading.value_or(true) ||
        !sample_player(components[static_cast<std::size_t>(*current)])) mute();
    return *playing || paused.value_or(false) || fading.value_or(false);
}

bool MovieReader::sample_player(ObjectRef component)
{
    if (!game_.call_bool(component, "IsPlaying").value_or(false) ||
        game_.call_bool(component, "IsPaused").value_or(true) ||
        game_.call_number(component, "GetStatus") != 5.0) return false;
    const auto source = game_.call_object(component, "GetSource");
    if (!source) return false;

    if (source_ != source || component_ != component) {
        audio_.stop();
        timeline_.reset();
        component_ = component;
        source_ = source;
        auto url = game_.text(source, "FilePath");
        if (url.empty()) url = game_.call_text(source, "GetUrl");
        schedule_ = match_movie(url);
    }
    if (!schedule_) return false;
    const auto fps = game_.array_number(source, "VideoTracks", 0, "FrameRate");
    const auto frame_count = game_.array_number(source, "VideoTracks", 0, "TotalFrames");
    if (!fps || std::abs(*fps - schedule_->fps) > 0.001 || frame_count != schedule_->frame_count) return false;
    const auto frame = game_.call_number(component, "GetFrameNumber");
    if (!frame || *frame < 0 || *frame >= schedule_->frame_count || std::trunc(*frame) != *frame) return false;
    update(schedule_, static_cast<int>(*frame));
    return true;
}

void MovieReader::mute()
{
    update(nullptr, -1);
}

void MovieReader::update(const MovieSchedule* movie, int frame)
{
    const auto now = MovieClock::now();
    const auto action = timeline_.advance(movie, frame, now);
    if (action.stop) audio_.stop();
    if (action.advancing) audio_.clock_advanced(now);
    if (action.clip) audio_.play(*action.clip, now);
}

void MovieReader::reset()
{
    clear_surface();
    component_ = {};
    source_ = {};
    schedule_ = nullptr;
    timeline_.reset();
    audio_.stop();
}

}
