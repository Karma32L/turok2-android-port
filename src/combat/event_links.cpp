#include "event_links.hpp"

#include <algorithm>
#include <cmath>

namespace combat {

std::vector<EventLink> linksFromMap(const std::vector<render3d::MapActor>& actors) {
    std::vector<EventLink> out;
    for (const render3d::MapActor& a : actors) {
        if (!a.eventLink || out.size() >= EventBus::kMaxLinks) continue;
        const auto src = a.props.number("sourceTID");
        const auto dst = a.props.number("targetTID");
        if (!src || !dst || *src <= 0.0f || *dst <= 0.0f || *src > 1.0e7f || *dst > 1.0e7f) continue;
        EventLink l;
        l.source = static_cast<std::int32_t>(*src);
        l.target = static_cast<std::int32_t>(*dst);
        const float type = a.props.number("triggerType", static_cast<float>(kMsgTrigger));
        l.message = type >= 0.0f && type <= 255.0f ? static_cast<std::uint32_t>(type) : kMsgTrigger;
        l.delay = std::clamp(a.props.number("userValue", 0.0f), 0.0f, EventBus::kMaxDelay);
        out.push_back(l);
    }
    return out;
}

EventBus::EventBus(std::vector<EventLink> links) : links_(std::move(links)) {
    if (links_.size() > kMaxLinks) links_.resize(kMaxLinks);
    for (EventLink& l : links_) {
        if (!std::isfinite(l.delay)) l.delay = 0.0f;
        l.delay = std::clamp(l.delay, 0.0f, kMaxDelay);
    }
}

std::size_t EventBus::signal(std::int32_t source) {
    if (source <= 0) return 0;
    std::size_t n = 0;
    for (const EventLink& l : links_) {
        if (l.source != source) continue;
        if (pending_.size() >= kMaxPending) {
            ++dropped_;
            continue;
        }
        pending_.push_back({now_ + static_cast<double>(l.delay), serial_++, {l.target, l.message}});
        ++n;
    }
    return n;
}

void EventBus::update(float dt, std::vector<Message>& out) {
    if (std::isfinite(dt) && dt > 0.0f) now_ += static_cast<double>(std::min(dt, 1.0f));
    // Solo lo que ya estaba en la cola: lo que encolen las reacciones a estos mensajes va al siguiente update
    std::vector<Pending> due;
    auto it = std::stable_partition(pending_.begin(), pending_.end(), [this](const Pending& p) { return p.due > now_; });
    due.assign(it, pending_.end());
    pending_.erase(it, pending_.end());
    std::sort(due.begin(), due.end(), [](const Pending& a, const Pending& b) {
        return a.due != b.due ? a.due < b.due : a.order < b.order;
    });
    for (const Pending& p : due) out.push_back(p.msg);
}

}  // namespace combat
