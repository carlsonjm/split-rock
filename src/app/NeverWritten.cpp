// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/NeverWritten.h"

#include "app/Product.h"

#include <QRegularExpression>

namespace splitrock {

namespace {

// A card number passes the Luhn check; most other long numbers do not.
bool luhn(const QString &digits)
{
    int sum = 0;
    bool twice = false;
    for (qsizetype i = digits.size() - 1; i >= 0; --i) {
        int d = digits.at(i).digitValue();
        if (twice) {
            d *= 2;
            if (d > 9)
                d -= 9;
        }
        sum += d;
        twice = !twice;
    }
    return sum % 10 == 0;
}

bool hasCardNumber(const QString &text)
{
    static const QRegularExpression run(QStringLiteral("(?<!\\d)(?:\\d[ -]?){12,18}\\d(?!\\d)"));
    auto it = run.globalMatch(text);
    while (it.hasNext()) {
        QString digits = it.next().captured(0);
        digits.remove(QLatin1Char(' ')).remove(QLatin1Char('-'));
        if (digits.size() >= 13 && digits.size() <= 19 && luhn(digits))
            return true;
    }
    return false;
}

// An IBAN passes its own check: moved and turned into digits, it leaves 1
// when divided by 97.
bool hasIban(const QString &text)
{
    static const QRegularExpression iban(QStringLiteral("\\b[A-Z]{2}\\d{2}(?: ?[A-Z0-9]{4}){2,7}(?: ?[A-Z0-9]{1,3})?\\b"));
    auto it = iban.globalMatch(text);
    while (it.hasNext()) {
        QString code = it.next().captured(0);
        code.remove(QLatin1Char(' '));
        if (code.size() < 15 || code.size() > 34)
            continue;
        const QString moved = code.mid(4) + code.left(4);
        int remainder = 0;
        for (const QChar c : moved) {
            const QString part = c.isDigit() ? QString(c) : QString::number(c.unicode() - 'A' + 10);
            for (const QChar d : part)
                remainder = (remainder * 10 + d.digitValue()) % 97;
        }
        if (remainder == 1)
            return true;
    }
    return false;
}

} // namespace

QString neverWrittenReason(const QString &text)
{
    // "is" counts only before something that looks like a secret: quoted, or
    // with a digit or a sign in it; "the password is stored in KWallet" does
    // not. A colon or an equals sign counts before anything.
    static const QRegularExpression password(QStringLiteral(
        "\\b(password|passwd|passcode|passphrase|pin|pin code|wi-?fi key|network key)\\b"
        "\\s*(?:[:=]\\s*\\S{3,}|(?:is|was)\\s+(?:[\"'`\\x{201C}][^\"'`\\x{201D}]{3,}|\\S*[\\d!@#$%^&*_+=?]\\S*))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression code(QStringLiteral(
        "\\b(?:(?:otp|one-time code|verification code|security code|login code|sign-in code|cvv2?|cvc)\\b\\s*(?:is|was|:|=|-)?"
        "|code\\s*(?:is|was|:))\\s*\\d{3,8}\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bank(QStringLiteral(
        "\\b(account number|account no\\.?|acct|routing number|sort code|bsb)\\b\\s*(is|was|:|=|#|-)?\\s*\\d[\\d -]{5,}"),
        QRegularExpression::CaseInsensitiveOption);

    QString what;
    if (hasCardNumber(text))
        what = QStringLiteral("a card number");
    else if (hasIban(text) || bank.match(text).hasMatch())
        what = QStringLiteral("bank details");
    else if (password.match(text).hasMatch())
        what = QStringLiteral("a password");
    else if (code.match(text).hasMatch())
        what = QStringLiteral("a code");
    if (what.isEmpty())
        return {};
    return QStringLiteral("%1 never writes down passwords, card numbers, bank details or codes, and this has %2 in it, "
                          "so it was not kept.")
        .arg(productName(), what);
}

} // namespace splitrock
