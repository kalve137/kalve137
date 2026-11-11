// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "log/occt_printer.h"

#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <TCollection_AsciiString.hxx>

namespace kalve137::log {

namespace {

Level levelOf(Message_Gravity gravity)
{
    switch (gravity) {
    case Message_Trace:
        return Level::trace;
    case Message_Info:
        return Level::info;
    case Message_Warning:
        return Level::warn;
    case Message_Alarm:
        return Level::err;
    case Message_Fail:
        return Level::critical;
    }
    return Level::info;
}

class LogPrinter final : public Message_Printer {
public:
    explicit LogPrinter(Channel channel) : m_channel(channel) { SetTraceLevel(Message_Trace); }

protected:
    void send(const TCollection_AsciiString& text, const Message_Gravity gravity) const override
    {
        channel(m_channel).log(levelOf(gravity), "OCCT: {}", text.ToCString());
    }

private:
    Channel m_channel;
};

} // namespace

void routeOcctMessages(Channel channel)
{
    const occ::handle<Message_Messenger>& messenger = Message::DefaultMessenger();
    messenger->RemovePrinters(STANDARD_TYPE(Message_Printer));
    messenger->AddPrinter(new LogPrinter(channel));
}

} // namespace kalve137::log
