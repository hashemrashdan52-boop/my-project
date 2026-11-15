// A stand-in for "ffmpeg" used by the tests of the speech tool: it understands only what Transcriber asks for
// (-filters, and "-af whisper=...") and writes a canned SRT to the filter's `destination`.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static std::string option(const std::string& filter, const std::string& key) {
    const std::string k = key + "=";
    size_t p = filter.find(k);
    if (p == std::string::npos) return {};
    p += k.size();
    const size_t e = filter.find(':', p);
    return filter.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

int main(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);
    std::string af;
    for (size_t i = 0; i + 1 < a.size(); ++i)
        if (a[i] == "-af") af = a[i + 1];
    for (auto& x : a)
        if (x == "-filters") {
            std::puts(" T.. whisper          A->A       Run Automatic Speech Recognition using OpenAI's Whisper.");
            return 0;
        }
    const size_t w = af.find("whisper=");
    if (w == std::string::npos) return 3;
    if (const char* log = std::getenv("FAKE_FFMPEG_LOG")) {       // tests read back what the filter chain was
        std::ofstream l(log, std::ios::binary | std::ios::trunc);
        l << af;
    }
    af = af.substr(w);                                            // options of the whisper filter itself
    if (!std::ifstream(option(af, "model"))) {
        std::fprintf(stderr, "[whisper] could not load model\n");
        return 1;
    }
    if (std::getenv("FAKE_FFMPEG_FAIL_VAD") && af.find("vad_model") != std::string::npos) {
        std::fprintf(stderr, "[whisper] cannot use this VAD model\n");
        return 1;
    }
    if (std::getenv("FAKE_FFMPEG_FAIL")) {
        std::fprintf(stderr, "[whisper] simulated failure\n");
        return 1;
    }
    std::printf("out_time_us=3000000\nprogress=continue\n");
    std::fflush(stdout);
    std::ofstream out(option(af, "destination"), std::ios::binary);
    out << "1\n00:00:00,200 --> 00:00:01,200\nHello and welcome to the channel.\n\n"
           "2\n00:00:01,400 --> 00:00:01,900\n[BLANK_AUDIO]\n\n"
           "3\n00:00:02,100 --> 00:00:03,000\nToday we are going to talk about editing.\n\n"
           "4\n00:00:03,100 --> 00:00:03,900\n\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xD8\xA8\xD9\x83\xD9\x85 \xD9\x81\xD9\x8A \xD8\xA7\xD9\x84\xD9\x82\xD9\x86\xD8\xA7\xD8\xA9\n\n";
    out.close();
    std::printf("out_time_us=4000000\nprogress=end\n");
    return 0;
}
