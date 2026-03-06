#include "core/TokenEstimator.h"
#include "core/TraceSessionManager.h"

#include <algorithm>
#include <string>

namespace
{
// 当前先采用“字符数近似 token”策略，避免引入第三方 tokenizer 依赖。
constexpr size_t kCharsPerToken = 4;
// 这个常量用于覆盖 span JSON 结构字段名/符号等固定成本。
// 这里不追求精确 tokenizer 对齐，只要口径稳定即可支撑阈值分发。
constexpr size_t kFixedJsonOverhead = 96;

size_t DigitsOfSizeT(size_t value)
{
    // 统一按十进制序列化估算数字位数。
    size_t digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

size_t DigitsOfInt64(int64_t value)
{
    // 负数额外算上 '-' 符号位，保证估算口径一致。
    size_t digits = 1;
    if (value < 0) {
        // 转无符号前先处理 INT64_MIN，避免溢出。
        const uint64_t abs_value = static_cast<uint64_t>(-(value + 1)) + 1;
        digits = 1; // '-'
        uint64_t tmp = abs_value;
        do {
            ++digits;
            tmp /= 10;
        } while (tmp > 0);
        return digits;
    }
    uint64_t tmp = static_cast<uint64_t>(value);
    while (tmp >= 10) {
        tmp /= 10;
        ++digits;
    }
    return digits;
}
} // namespace

size_t TokenEstimator::Estimate(const SpanEvent& span) const
{
    size_t chars = kFixedJsonOverhead;

    // 数字字段近似：trace/span/start 是固定会出现的，parent/end 视 optional 决定。
    chars += DigitsOfSizeT(span.trace_key);
    chars += DigitsOfSizeT(span.span_id);
    chars += DigitsOfInt64(span.start_time_ms);
    if (span.parent_span_id.has_value()) {
        chars += DigitsOfSizeT(span.parent_span_id.value());
    }
    if (span.end_time.has_value()) {
        chars += DigitsOfInt64(span.end_time.value());
    }

    // 核心文本字段。
    chars += span.name.size();
    chars += span.service_name.size();

    // 可选枚举字段按最终序列化文本长度估算。
    if (span.status.has_value()) {
        switch (span.status.value()) {
            case SpanEvent::Status::Unset:
                chars += 5; // UNSET
                break;
            case SpanEvent::Status::Ok:
                chars += 2; // OK
                break;
            case SpanEvent::Status::Error:
                chars += 5; // ERROR
                break;
        }
    }
    if (span.kind.has_value()) {
        switch (span.kind.value()) {
            case SpanEvent::Kind::Internal:
                chars += 8; // INTERNAL
                break;
            case SpanEvent::Kind::Server:
                chars += 6; // SERVER
                break;
            case SpanEvent::Kind::Client:
                chars += 6; // CLIENT
                break;
            case SpanEvent::Kind::Producer:
                chars += 8; // PRODUCER
                break;
            case SpanEvent::Kind::Consumer:
                chars += 8; // CONSUMER
                break;
        }
    }

    // attributes 按 key/value 文本长度累加，并加上每项 JSON 符号的固定成本。
    for (const auto& [key, value] : span.attributes) {
        chars += key.size();
        chars += value.size();
        chars += 6; // 引号/冒号/逗号等近似结构开销
    }

    const size_t tokens = (chars + (kCharsPerToken - 1)) / kCharsPerToken;
    return std::max<size_t>(tokens, 1);
}
