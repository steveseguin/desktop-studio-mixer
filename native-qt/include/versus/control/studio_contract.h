#pragma once
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace versus::control {
inline QJsonObject studioSchema() {
    static const QJsonObject schema = [] { QFile f(":/mixer/studio-control.json");
        if (!f.open(QIODevice::ReadOnly)) return QJsonObject{};
        return QJsonDocument::fromJson(f.readAll()).object(); }();
    return schema;
}
// A small, closed JSON-schema subset. Validate the complete request before any
// side effects, including rejecting misspelled fields and wrong JSON types.
inline QString validateStudioRequest(const QJsonObject &value, const QJsonObject &schema) {
    if (schema.isEmpty()) return "Unknown command";
    for (const auto required : schema["required"].toArray())
        if (!value.contains(required.toString())) return "Missing field: " + required.toString();
    const auto properties = schema["properties"].toObject();
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!properties.contains(it.key())) return "Unknown field: " + it.key();
        const auto rule = properties[it.key()].toObject(); const auto v = it.value();
        const auto type = rule["type"].toString();
        bool valid = true;
        if (rule.contains("const")) valid = v == rule["const"];
        if (type == "boolean") valid = v.isBool();
        if (type == "string") {
            valid = v.isString(); const auto s = v.toString();
            if (rule.contains("minLength")) valid &= s.trimmed().size() >= rule["minLength"].toInt();
            if (rule.contains("maxLength")) valid &= s.size() <= rule["maxLength"].toInt();
            valid &= !s.contains(QChar(0));
        }
        if (type == "number" || type == "integer") {
            const double n = v.toDouble(); valid = v.isDouble() && std::isfinite(n);
            if (type == "integer") valid &= std::floor(n) == n;
            valid &= n >= rule["minimum"].toDouble() && n <= rule["maximum"].toDouble();
        }
        if (rule.contains("enum")) valid &= rule["enum"].toArray().contains(v);
        if (!valid) return "Invalid field: " + it.key();
    }
    return {};
}
}
