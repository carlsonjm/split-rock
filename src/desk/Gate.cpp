// SPDX-License-Identifier: GPL-2.0-or-later
#include "desk/Gate.h"

#include <QList>
#include <QRegularExpression>

namespace splitrock::desk {

namespace {

struct Kind {
    QString name;
    QRegularExpression pattern;
    QString answer;
};

QRegularExpression words(const QString &pattern)
{
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
}

// In order: the first kind that matches answers. A call from "Microsoft" that
// mentions a bank is a scam first; a stored password that mentions a bank is
// about the password.
const QList<Kind> &list()
{
    static const QList<Kind> kinds{
        {QStringLiteral("scam"),
         words(QStringLiteral(
             "microsoft (support|called|rang|phoned|technician|engineer|says|said)|"
             "(called|rang|phoned) me.{0,80}(virus|infected|hacked|hackers)|tech(nical)? support|"
             "support (number|line|called|rang)|\\binfected\\b|virus.{0,40}(call|phone|number)|"
             "(pop.?up|window|page|message) (says|said|saying).{0,60}(call|virus|hacked|infected|locked)|"
             "\\.(exe|scr|msi)\\b|(type|paste|enter) (in )?what (he|she|they) (says|tells|reads)|"
             "open the (black|typing|command)|(router|broadband|account|bank account|computer) (has been|was|is|got) hacked|"
             "(charged|renewal|subscription|refund).{0,80}(call|ring|phone)|call (them )?to cancel|geek squad|norton|mcafee")),
         QStringLiteral("Stop there: this is how scams work. Microsoft and other companies never call you, or show a "
                        "window asking you to phone them, and nobody honest needs to see or control your computer "
                        "to fix it. Don't call the number, don't install anything they ask for, and don't open files "
                        "like this one. Hang up or close the window; your computer is fine. If you'd like, ask me "
                        "for a health check and I'll look at it with you.")},
        {QStringLiteral("remote"),
         words(QStringLiteral(
             "teamviewer|anydesk|rustdesk|quick ?assist|ultraviewer|supremo|"
             "(see|control|access|take over|get into) (my|the) (screen|computer|pc|tablet|laptop|desktop)|"
             "remote(ly)? (access|control|desktop|support)|support program|"
             "(read|give|tell|send) (him|her|them|it to (him|her|them)) the (\\d+.digit |\\d+ |nine |six )?(code|numbers?|digits|id)|"
             "(share|show) (them |him |her )?my screen.{0,80}(bank|prize|winning|refund|caller|stranger)|"
             "(prize|winnings|won|refund).{0,120}(share|show).{0,20}screen")),
         QStringLiteral("Be careful with this one. AnyDesk, TeamViewer and apps like them, and the code they show, give "
                        "someone full control of this computer, and that's how most scams begin. If someone asked "
                        "you to, check it's really them by calling them back on a number you already have. Either "
                        "way, tell me what they were going to fix, and I'll help with it here, so nobody needs to "
                        "take control.")},
        {QStringLiteral("forgotten"),
         words(QStringLiteral(
             "(what is|what's|whats|tell me|find|show me|look up|remind me of|give me) (my|the|our) (?!(own )?(wi-?fi|network|router|hotspot))[^.?!]{0,30}"
             "(password|passcode|pin)\\b|(forgot|forgotten|lost|can'?t remember) (my|the|our) (?!(own )?(wi-?fi|network|router|hotspot))[^.?!]{0,30}(password|passcode)|"
             "\\b(bank|email|e-mail|paypal|amazon|facebook|google|apple|netflix)( account)? (password|passcode|pin)\\b")),
         QStringLiteral("I can't see or keep passwords, so I can't tell you that one, and nobody else should ask you for "
                        "it. For a bank or another website, use \"Forgot password\" on its own sign-in page, or call "
                        "your bank on the number printed on your card. If you let your web browser save passwords, its "
                        "settings list them once you sign in to it. If it's the password for this computer, whoever "
                        "set the computer up can set a new one for you.")},
        {QStringLiteral("password"),
         words(QStringLiteral(
             "password.{0,200}(make it stop|stop asking|never ask|turn (it|that|this) off|get rid of|without asking)|"
             "stop asking.{0,40}password|nopasswd|sudo.{0,40}(never|stop|without).{0,20}(ask|password)|"
             "(save|store|remember|keep|hold|write down) (all )?(of )?(my|the|both|our)\\b.{0,60}(password|passcode|login)|"
             "i.?ll tell you (them|my password|the password)|"
             "get (past|around|into) (the|her|his|its|their) (lock|pin|password|passcode)|"
             "(pin|passcode|lock ?screen).{0,60}(get past|bypass|get around|break into)|"
             "(password|passcode) (is|was)\\s+\\S|my (password|passcode)s? (is|was|are)\\b|"
             "(reset|bypass|crack).{0,30}(password|passcode)|"
             "(lock ?screen|log ?in|sign.?in|login screen).{0,80}(password|passcode)|"
             "(password|passcode).{0,80}(lock ?screen|won'?t (take|work|accept)|doesn'?t work|not working)")),
         QStringLiteral("I never ask for, keep or type passwords, so please don't share one with me; if you've written "
                        "one out somewhere, it's worth changing it. When this computer asks for your password before "
                        "a change, that's what stops a bad program from changing things behind your back, so I won't "
                        "switch it off. If the lock screen won't take a password you know is right, it's most likely "
                        "a short safety pause after a few wrong tries: wait ten minutes, then type it slowly once.")},
        {QStringLiteral("money"),
         words(QStringLiteral(
             "gift ?cards?|google play cards?|itunes cards?|steam cards?|"
             "\\b(code|pin|otp)\\b.{0,60}(bank|texted|\\bsms\\b|sent to (me|my phone))|"
             "(bank|texted|\\bsms\\b).{0,60}\\b(code|pin|otp)\\b|"
             "seed (words|phrase)|recovery phrase|secret (words|phrase)|\\b(12|24|twelve) words\\b|"
             "recovery (agent|service|expert|company)|(get|recover) (my|the) (money|bitcoin|crypto|funds) back|"
             "unlock(ing)? fee|fee to unlock|card number|\\bcvv\\b|security code on (my|the) card|"
             "wire (the )?money|western union|bitcoin atm|"
             "pay .{0,40}(fee|fine|to unlock)|bank (details|login|account number)")),
         QStringLiteral("Stop there. No real company or bank asks for gift cards, a code sent to your phone, your card "
                        "number or a crypto wallet's secret words, and anyone offering to win back lost money for a "
                        "fee is running a second scam. Don't send or type any of them, and don't pass on a code "
                        "you've been sent. If money has already gone, call your bank on the number printed on your "
                        "card, and report it to the police.")},
        {QStringLiteral("destructive"),
         words(QStringLiteral(
             "rm\\s+-\\w*[rf]|\\bsudo\\s+rm\\b|delete (everything|all of it|it all|all the (system|files))|"
             "wipe (everything|it all|all|the (whole|entire)|my (whole |entire )?(computer|disk|drive|home)|the main folders)|"
             "erase (everything|it all|the (disk|drive|whole))|format (the|my) (disk|drive|computer|hard ?drive)|"
             "(curl|wget)[^|]*\\|\\s*(sudo\\s+)?(ba|z)?sh|\\|\\s*sudo\\s+(ba)?sh|paste (this|that|it) (in|into)|"
             "run this script|(delete|remove|clear) .{0,30}(/usr|/etc|/boot|/bin|/lib|system (files|folders))|"
             "chmod\\s+(-R\\s+)?777|\\bdd\\s+if=|mkfs|"
             "(wipe|format|erase).{0,40}(main|big|whole|internal|system) (drive|disk|one)|format it .{0,40}while it.?s running|"
             "(get rid of|delete|remove|wipe) everything in (my|the) (home|main)|everything in my home folder")),
         QStringLiteral("I won't do that one: it would delete parts of the system or your files, or run a stranger's "
                        "program with full control, and the computer may not start afterwards. Tell me what you're "
                        "trying to fix and I'll find a safe way. If it's about space, ask me to free up space and "
                        "I'll show what's using it first.")},
        {QStringLiteral("piracy"),
         words(QStringLiteral(
             "cracked (version|copy|copies|software|app|apps|game|games)|\\bcrack (for|to activate)|\\bkeygen\\b|"
             "\\bwarez\\b|key ?generator|trial (last |run )?forever|reset the trial|"
             "(photoshop|office|windows|adobe|premiere|lightroom|autocad|illustrator).{0,100}"
             "(don.?t have to pay|without paying|patch|for nothing)|pirat(ed|e|ing)\\b|"
             "torrent.{0,40}(movie|film|game|photoshop|office|windows|adobe|series|show)|activator|kms ?pico|"
             "(for free|free version|free copy|unlocked|already (activated|unlocked)|full versions?).{0,60}"
             "(photoshop|office|windows|adobe|premiere|lightroom|autocad|illustrator|final cut)|"
             "(photoshop|office|windows|adobe|premiere|lightroom|autocad|illustrator).{0,60}"
             "(for free|free version|unlocked|already (activated|unlocked)|without paying)")),
         QStringLiteral("I can't help get or run cracked copies. They're one of the most common ways bad software gets "
                        "onto a computer. There are good free apps for most jobs: LibreOffice for documents and "
                        "spreadsheets, GIMP or Krita for photos, all in Discover, and the official apps when you need "
                        "the real thing.")},
        {QStringLiteral("spying"),
         words(QStringLiteral(
             "without (her|him|them|my \\w+|anyone|anybody) (knowing|noticing|seeing|finding out)|spy(ing)? on|"
             "secretly (watch|read|track|record|see|check)|keylogger|stalkerware|"
             "track (my )?(wife|husband|partner|girlfriend|boyfriend|spouse|ex)\\b|"
             "(read|see|watch|monitor|check) (her|his|their|my (wife|husband|partner|girlfriend|boyfriend|spouse)'?s?) "
             "(messages|texts|searches|chats|emails|screen|history|whatsapp|facebook)|"
             "what (she|he)\\W?s? (been )?(searching|messaging|looking at|typing|doing online)")),
         QStringLiteral("I won't set up watching someone without them knowing. What I can do is give each person their "
                        "own account on this computer, so everyone's things stay separate and private. That's in "
                        "System Settings, under Users, and I can walk you through it.")},
        {QStringLiteral("updates"),
         words(QStringLiteral(
             "never updates? (again|ever)|(turn off|switch off|disable|stop|block) (all )?(the )?(automatic )?updates?"
             "( for good| forever| permanently| ever again| completely)|so (it|the computer|this) never updates")),
         QStringLiteral("I won't switch updates off for good: they're what keeps this computer safe from new threats "
                        "and fixes what breaks. If the reminders are the bother, ask me to update the computer and "
                        "I'll do it in one go, at a time that suits you.")},
        {QStringLiteral("scareware"),
         words(QStringLiteral(
             "driver ?(booster|updater|fixer|easy)|pc (cleaner|optimi[sz]er|booster|speed ?up)|registry (cleaner|fix)|"
             "speed (it |your pc |it up )?(up )?\\d+ ?%|drivers? (are|is) .{0,30}out of date|cleaner pro|"
             "(advert|ad|pop.?up) (said|says|saying).{0,80}(slow|virus|drivers?|speed|clean)")),
         QStringLiteral("That's an advert, not your computer talking. Driver boosters and PC cleaners are made for "
                        "Windows, don't work here, and are a common way to sneak in bad software. Linux gets its "
                        "drivers with its own updates, so there's nothing to download. If the computer feels slow, "
                        "ask me for a health check.")},
        {QStringLiteral("protection"),
         words(QStringLiteral(
             "(turn|switch|shut) (all |every )?(the |my )?(protection|firewall|antivirus|anti.?virus|security|secure boot)"
             "( thing| stuff)? off|(turn|switch|shut) off (all |every )?(the |my )?(protection|firewall|antivirus|"
             "anti.?virus|security|secure boot)|disable (all |the |my )?(firewall|antivirus|anti.?virus|protection|"
             "security|secure boot|selinux|apparmor)|(firewall|antivirus|protection).{0,40}(off for good|off forever|"
             "permanently off)")),
         QStringLiteral("I won't switch the protection off completely: it would stay off for everything, not just the "
                        "thing it's blocking. Tell me what it's stopping, such as the game's name, and I'll help find "
                        "what's in the way and let just that through.")},
    };
    return kinds;
}

} // namespace

QStringList Gate::kinds()
{
    QStringList names;
    for (const Kind &kind : list())
        names.append(kind.name);
    return names;
}

bool Gate::harmful(const QString &ask)
{
    const std::optional<QString> kind = check(ask);
    return kind && *kind != QLatin1String("forgotten") && *kind != QLatin1String("password");
}

std::optional<QString> Gate::check(const QString &ask)
{
    for (const Kind &kind : list()) {
        if (kind.pattern.match(ask).hasMatch())
            return kind.name;
    }
    return std::nullopt;
}

QString Gate::answer(const QString &kind)
{
    for (const Kind &k : list()) {
        if (k.name == kind)
            return k.answer;
    }
    return {};
}

} // namespace splitrock::desk
