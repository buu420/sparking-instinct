#include "speech_mailbox.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace std::chrono_literals;
using sparking::speech::Message;
using sparking::speech::SpeechMailbox;

namespace {
void require(bool result, const char* description)
{
    if (!result) {
        std::cerr << description << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

int main()
{
    SpeechMailbox mailbox;
    Message message;
    require(!mailbox.say("background", true), "accepted speech before foreground");
    mailbox.set_foreground(true);
    require(mailbox.say("first", true), "rejected an interrupt");
    require(mailbox.say("queued", false), "rejected a queued line");
    require(mailbox.next(message, 0ms) && message.text == "first" && message.interrupt,
            "changed the first line or its interrupt flag");
    require(mailbox.can_deliver(message), "rejected a current speech message");
    require(mailbox.next(message, 0ms) && message.text == "queued" && !message.interrupt,
            "changed queue order or flag");

    require(mailbox.say("pending", false), "rejected a pending line");
    require(mailbox.say("replacement", true), "rejected replacement focus speech");
    require(!mailbox.can_deliver(message), "old in-flight speech survived an interrupt");
    require(mailbox.next(message, 0ms) && message.text == "replacement" && message.interrupt,
            "stale text preceded the replacement");
    require(mailbox.next(message, 0ms) && message.kind == Message::Kind::idle,
            "stale text remained queued");

    require(mailbox.say("cleared by stop", false), "rejected speech before stop");
    mailbox.stop();
    require(mailbox.next(message, 0ms) && message.kind == Message::Kind::stop,
            "stop did not supersede queued speech");
    require(!mailbox.has_speech(), "stop counted as speech");

    require(mailbox.say("taken before Alt+Tab", true), "rejected speech before Alt+Tab");
    require(mailbox.next(message, 0ms), "could not take foreground speech");
    require(mailbox.say("queued before Alt+Tab", false), "rejected queued foreground speech");
    mailbox.set_foreground(false);
    mailbox.set_foreground(true);
    require(!mailbox.can_deliver(message), "in-flight background speech survived focus round trip");
    require(!mailbox.has_speech(), "queued background speech survived focus round trip");

    require(mailbox.next(message, 0ms) && message.kind == Message::Kind::stop &&
                mailbox.can_deliver(message),
            "foreground loss did not stop the game's active speech");
    require(mailbox.say("before stop and new screen", false), "rejected speech before a screen transition");
    mailbox.stop();
    require(mailbox.say("new screen", true), "rejected speech after a stop");
    require(mailbox.next(message, 0ms) && message.kind == Message::Kind::stop,
            "new speech removed the pending stop");
    require(mailbox.next(message, 0ms) && message.text == "new screen",
            "pending stop lost the new screen");

    for (std::size_t i = 0; i < SpeechMailbox::maximum_messages; ++i) {
        require(mailbox.say("pending", false), "capacity was too small");
    }
    require(!mailbox.say("overflow", false), "mailbox exceeded its capacity");
    require(mailbox.say("latest", true), "full mailbox blocked fresh focus speech");
    require(mailbox.next(message, 0ms) && message.text == "latest", "stale full queue survived interruption");
    require(!mailbox.say(std::string_view("unsafe\0tail", 11), false), "accepted truncated C string");
    require(!mailbox.say(std::string(SpeechMailbox::maximum_text_bytes + 1, 'x'), false),
            "accepted unbounded text");
    require(mailbox.say("日本語 한글 中文", false), "rejected UTF-8 speech");
    require(mailbox.next(message, 0ms) && message.text == "日本語 한글 中文", "changed UTF-8 bytes");

    mailbox.close();
    require(!mailbox.can_deliver(message), "speech remained deliverable after close");
    require(!mailbox.next(message, 0ms), "closed mailbox appeared alive");
    require(!mailbox.say("after close", true), "closed mailbox accepted speech");
    std::cout << "Foreground, FIFO, interrupt, stop, limits, UTF-8 and close passed\n";
}
