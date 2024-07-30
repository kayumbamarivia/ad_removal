#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
 #include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/timestamp.h>


using namespace std;

void processSegment(AVFormatContext* inFormatContext, AVFormatContext* outFormatContext, int64_t startPTS, int64_t endPTS) {
    int videoStreamIndex = -1;
    for (unsigned int i = 0; i < inFormatContext->nb_streams; ++i) {
        if (inFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }
    }

    if (videoStreamIndex == -1) {
        cerr << "No video stream found." << endl;
        return;
    }

    AVStream* inStream = inFormatContext->streams[videoStreamIndex];
    AVStream* outStream = avformat_new_stream(outFormatContext, nullptr);
    if (!outStream) {
        cerr << "Could not allocate output stream." << endl;
        return;
    }
    avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);

    if (avformat_write_header(outFormatContext, nullptr) < 0) {
        cerr << "Error occurred while writing header." << endl;
        return;
    }

    AVPacket packet;
    av_init_packet(&packet);

    while (av_read_frame(inFormatContext, &packet) >= 0) {
        if (packet.stream_index == videoStreamIndex) {
            if (packet.pts < startPTS || packet.pts > endPTS) {
                av_packet_unref(&packet);
                continue;
            }

            packet.pts -= startPTS;
            packet.dts -= startPTS;

            if (av_interleaved_write_frame(outFormatContext, &packet) < 0) {
                cerr << "Error while writing packet." << endl;
                break;
            }
        }
        av_packet_unref(&packet);
    }

    av_write_trailer(outFormatContext);
}

void removeAds(const string& inputFile, const string& outputFile, const vector<pair<int, int>>& adBreaks) {
    avformat_network_init();

    AVFormatContext* inFormatContext = nullptr;
    if (avformat_open_input(&inFormatContext, inputFile.c_str(), nullptr, nullptr) < 0) {
        cerr << "Could not open input file." << endl;
        exit(1);
    }

    if (avformat_find_stream_info(inFormatContext, nullptr) < 0) {
        cerr << "Could not find stream information." << endl;
        exit(1);
    }

    AVFormatContext* outFormatContext = nullptr;
    avformat_alloc_output_context2(&outFormatContext, nullptr, nullptr, outputFile.c_str());
    if (!outFormatContext) {
        cerr << "Could not create output context." << endl;
        exit(1);
    }

    if (avio_open(&outFormatContext->pb, outputFile.c_str(), AVIO_FLAG_WRITE) < 0) {
        cerr << "Could not open output file." << endl;
        exit(1);
    }

    int64_t lastEndPTS = 0;
    for (size_t i = 0; i < adBreaks.size(); ++i) {
        int64_t adStart = adBreaks[i].first * AV_TIME_BASE;
        int64_t adEnd = adBreaks[i].second * AV_TIME_BASE;

        if (lastEndPTS < adStart) {
            processSegment(inFormatContext, outFormatContext, lastEndPTS, adStart);
        }
        lastEndPTS = adEnd;
    }

    if (lastEndPTS < inFormatContext->duration) {
        processSegment(inFormatContext, outFormatContext, lastEndPTS, inFormatContext->duration);
    }

    avformat_close_input(&inFormatContext);
    if (outFormatContext && !(outFormatContext->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&outFormatContext->pb);
    }
    avformat_free_context(outFormatContext);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <input file> <output file>" << endl;
        return 1;
    }

    string inputFile = argv[1];
    string outputFile = argv[2];

    vector<pair<int, int>> adBreaks = {
        {30, 60},
        {120, 150}
    };

    removeAds(inputFile, outputFile, adBreaks);

    cout << "Ad removal completed successfully." << endl;

    return 0;
}
