// Standalone diagnostic adapter. Emits the subset of ffprobe JSON used by aMule.
#include <iostream>
#include <iomanip>
#include <string>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

static void quoted(const char* text)
{
    std::cout << '"';
    for (const unsigned char c : std::string(text ? text : "")) {
        if (c == '"' || c == '\\') std::cout << '\\' << c;
        else if (c < 32) std::cout << "\\u00" << std::hex << std::setw(2)
                                 << std::setfill('0') << unsigned(c) << std::dec;
        else std::cout << c;
    }
    std::cout << '"';
}

static void tags(const AVDictionary* dict)
{
    std::cout << '{';
    const AVDictionaryEntry* entry = nullptr;
    bool first = true;
    while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX))) {
        if (!first) std::cout << ',';
        first = false;
        quoted(entry->key); std::cout << ':'; quoted(entry->value);
    }
    std::cout << '}';
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << av_version_info() << '\n'; return 0;
    }
    if (argc != 2) { std::cerr << "usage: libav-probe FILE\n"; return 2; }
    av_log_set_level(AV_LOG_ERROR);
    AVFormatContext* ctx = nullptr;
    int rc = avformat_open_input(&ctx, argv[1], nullptr, nullptr);
    if (rc >= 0) rc = avformat_find_stream_info(ctx, nullptr);
    if (rc < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(rc, error, sizeof(error)); std::cerr << error << '\n';
        avformat_close_input(&ctx); return 1;
    }
    std::cout << "{\"format\":{\"format_name\":";
    quoted(ctx->iformat->name);
    std::cout << ",\"duration\":";
    if (ctx->duration == AV_NOPTS_VALUE) std::cout << "null";
    else std::cout << std::setprecision(17) << double(ctx->duration) / AV_TIME_BASE;
    std::cout << ",\"bit_rate\":" << ctx->bit_rate << ",\"tags\":";
    tags(ctx->metadata);
    std::cout << "},\"streams\":[";
    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        if (i) std::cout << ',';
        const AVStream* stream = ctx->streams[i];
        std::cout << "{\"codec_name\":";
        if (stream->codecpar->codec_id == AV_CODEC_ID_NONE) quoted("");
        else quoted(avcodec_get_name(stream->codecpar->codec_id));
        std::cout << ",\"codec_type\":";
        quoted(av_get_media_type_string(stream->codecpar->codec_type));
        std::cout << ",\"disposition\":{\"attached_pic\":"
                  << !!(stream->disposition & AV_DISPOSITION_ATTACHED_PIC)
                  << "},\"tags\":";
        tags(stream->metadata); std::cout << '}';
    }
    std::cout << "]}\n";
    avformat_close_input(&ctx);
}
