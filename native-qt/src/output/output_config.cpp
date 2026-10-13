#include "versus/output/output_session.h"
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QUrlQuery>
#include <algorithm>
#include <stdexcept>

namespace versus::output {
const char *protocolName(Protocol protocol) {
    switch (protocol) {
        case Protocol::Recording: return "Recording";
        case Protocol::Whip: return "WHIP";
        case Protocol::Srt: return "SRT";
        case Protocol::Rtmp: return "RTMP";
        default: return "VDO.Ninja";
    }
}
Protocol parseProtocol(const std::string &name) {
    if (name == "file") return Protocol::Recording;
    if (name == "vdo" || name == "vdoninja") return Protocol::VdoNinja;
    if (name == "whip") return Protocol::Whip;
    if (name == "srt") return Protocol::Srt;
    if (name == "rtmp" || name == "rtmps") return Protocol::Rtmp;
    throw std::invalid_argument("Output must be vdo, whip, srt, rtmp or file");
}

std::string validateConfig(const Config &config) {
    if (config.protocol == Protocol::VdoNinja) return {};
    for (const auto *value : {&config.url, &config.bearerToken, &config.streamKey, &config.streamId, &config.passphrase}) {
        if (value->size() > 16384 || std::any_of(value->begin(), value->end(), [](unsigned char c) { return c < 32 || c == 127; }))
            return "Output settings contain invalid control characters or are too long.";
    }
    if (config.protocol == Protocol::Recording) {
        const QFileInfo file(QString::fromStdString(config.url));
        if (!file.isAbsolute() || file.suffix().toLower() != "mkv" || !file.dir().exists())
            return "Choose an absolute .mkv file path in an existing folder.";
        if (file.exists()) return "This recording already exists. Choose a new file name.";
        return {};
    }
    const QUrl url(QString::fromStdString(config.url), QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || url.hasFragment() || config.url.size() > 16384)
        return "Enter a complete output URL without a fragment.";
    const auto scheme = url.scheme().toLower();
    if (config.protocol == Protocol::Whip) {
        if (config.whipPlayoutMs < 0 || config.whipPlayoutMs > 1000) return "WHIP playback smoothing must be 0 to 1000 ms.";
        if (config.whipHistoryMs < 250 || config.whipHistoryMs > 3000) return "WHIP retransmission history must be 250 to 3000 ms.";
        if (scheme != "https" && scheme != "http") return "WHIP needs an https:// or http:// endpoint.";
        if (!url.userInfo().isEmpty()) return "Use the WHIP bearer token field for authentication.";
        if (config.bearerToken.find_first_of("\r\n") != std::string::npos || config.bearerToken.size() > 16384)
            return "The WHIP token contains invalid characters or is too long.";
    } else if (config.protocol == Protocol::Rtmp) {
        if (scheme != "rtmp" && scheme != "rtmps") return "RTMP needs an rtmp:// or rtmps:// server URL.";
        if (url.path().isEmpty() || url.path() == "/") return "Include the RTMP application path in the server URL.";
    } else if (config.protocol == Protocol::Srt) {
        if (scheme != "srt" || url.port() <= 0) return "SRT needs an srt://host:port URL.";
        if (!url.userInfo().isEmpty() || (!url.path().isEmpty() && url.path() != "/"))
            return "Use the SRT stream ID and passphrase fields instead of a URL path or user name.";
        const QUrlQuery query(url);
        const auto mode = query.queryItemValue("mode");
        if (mode == "rendezvous") return "SRT rendezvous is not available with this output backend. Use caller or listener.";
        if (!mode.isEmpty() && mode != "caller" && mode != "listener") return "SRT mode must be caller or listener.";
        if (query.allQueryItemValues("mode").size() > 1) return "Specify one SRT connection mode.";
        const auto passphrase = config.passphrase.empty()
            ? query.queryItemValue("passphrase", QUrl::FullyDecoded).toUtf8() : QByteArray::fromStdString(config.passphrase);
        if (!passphrase.isEmpty() && (passphrase.size() < 10 || passphrase.size() > 79))
            return "An SRT passphrase must contain 10 to 79 bytes.";
        const auto streamId = config.streamId.empty()
            ? query.queryItemValue("streamid", QUrl::FullyDecoded).toUtf8() : QByteArray::fromStdString(config.streamId);
        if (streamId.size() > 512) return "An SRT stream ID must contain at most 512 bytes.";
        if (config.latencyMs < 20 || config.latencyMs > 8000) return "SRT latency must be 20 to 8000 ms.";
    }
    if ((config.protocol == Protocol::Rtmp || config.protocol == Protocol::Srt) &&
        (config.aacBitrateKbps < 32 || config.aacBitrateKbps > 320))
        return "AAC bitrate must be 32 to 320 kbps.";
    return {};
}

std::string destinationLabel(const Config &config) {
    if (config.protocol == Protocol::VdoNinja) return protocolName(config.protocol);
    const QUrl url(QString::fromStdString(config.url));
    QString name = config.protocol == Protocol::Rtmp && url.scheme() == "rtmps"
        ? QStringLiteral("RTMPS") : QString::fromLatin1(protocolName(config.protocol));
    QString host = url.host();
    if (host.contains(':')) host = '[' + host + ']';
    if (url.port() > 0) host += ':' + QString::number(url.port());
    return (host.isEmpty() ? name : name + " · " + host).toStdString();
}

std::string destinationUrl(const Config &config) {
    if (config.protocol == Protocol::Recording) return config.url;
    QUrl url(QString::fromStdString(config.url));
    if (config.protocol == Protocol::Rtmp && !config.streamKey.empty()) {
        QString path = url.path();
        if (!path.endsWith('/')) path += '/';
        url.setPath(path + QString::fromStdString(config.streamKey));
    } else if (config.protocol == Protocol::Srt) {
        const auto encode = [](const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); };
        QUrlQuery query;
        // QUrlQuery setters expect encoded input. Preserve literal percent
        // sequences and plus signs in credentials and stream IDs; FFmpeg
        // decodes query values before handing them to libsrt.
        for (const auto &item : QUrlQuery(url).queryItems(QUrl::FullyDecoded))
            query.addQueryItem(encode(item.first), encode(item.second));
        const auto set = [&](const QString &name, const QString &value) {
            query.removeAllQueryItems(name); query.addQueryItem(name, encode(value));
        };
        const auto mode = query.queryItemValue("mode");
        set("mode", mode.isEmpty() ? "caller" : mode); set("transtype", "live");
        set("latency", QString::number(config.latencyMs * 1000));
        set("pkt_size", "1316");
        if (!config.streamId.empty()) set("streamid", QString::fromStdString(config.streamId));
        if (!config.passphrase.empty()) set("passphrase", QString::fromStdString(config.passphrase));
        url.setQuery(query);
    }
    return url.toString(QUrl::FullyEncoded).toStdString();
}
} // namespace versus::output
