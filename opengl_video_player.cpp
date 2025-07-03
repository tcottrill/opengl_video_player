// opengl_video_player.cpp — July 2025 (modular)

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <thread>
#include <chrono>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <xaudio2.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// ---------- link libs (MSVC) ------------------------------------------------
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"swscale.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"glew32.lib")
#pragma comment(lib,"glfw3dll.lib")
#pragma comment(lib,"opengl32.lib")
#pragma comment(lib,"xaudio2.lib")

/* --------helpers (alignedAlloc, VoiceCallback, makeWave, sleepMillis) -------- */
static void* alignedAlloc(size_t s, size_t a) {
#ifdef _WIN32
	return _aligned_malloc(s, a);
#else return aligned_alloc(a, s);
#endif
}
static void alignedFree(void* p)
{
#ifdef _WIN32
	_aligned_free(p);
#else free(p);
#endif
}

struct VoiceCallback :IXAudio2VoiceCallback {
	void STDMETHODCALLTYPE OnBufferEnd(void* ctx)override { delete[](uint8_t*)ctx; }
	void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32)override {}
	void STDMETHODCALLTYPE OnVoiceProcessingPassEnd()override {}
	void STDMETHODCALLTYPE OnStreamEnd()override {}
	void STDMETHODCALLTYPE OnBufferStart(void*)override {}
	void STDMETHODCALLTYPE OnLoopEnd(void*)override {}
	void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT)override {}
};

static WAVEFORMATEX makeWave(int Hz) {
	WAVEFORMATEX f{};
	f.wFormatTag = WAVE_FORMAT_PCM;
	f.nChannels = 2; 
	f.nSamplesPerSec = Hz;
	f.wBitsPerSample = 16; 
	f.nBlockAlign = 4; 
	f.nAvgBytesPerSec = Hz * 4; 
	return f;
}

static void sleepMs(int ms) { 
	std::this_thread::sleep_for(std::chrono::milliseconds(ms)); 
}

/* ---------- grouped state -----------------------*/
struct VideoState {
	AVCodecContext* dec = nullptr;
	SwsContext* sws = nullptr;
	int             W = 0, H = 0;
	GLFWwindow* win = nullptr;
	GLuint          tex = 0;
};
struct AudioState {
	AVCodecContext* dec = nullptr;
	SwrContext* swr = nullptr;
	IXAudio2* xa = nullptr;
	IXAudio2MasteringVoice* master = nullptr;
	IXAudio2SourceVoice* src = nullptr;
	int deviceRate = 0;
	std::vector<uint8_t> scratch;
};

/* -----------------------------------------------------------------
   VIDEO INITIALISATION (opens stream + OpenGL window)
   ----------------------------------------------------------------*/
bool initVideoStream(AVFormatContext* fmt, int streamIdx, VideoState& vs)
{
	auto* par = fmt->streams[streamIdx]->codecpar;
	auto* dec = avcodec_find_decoder(par->codec_id);
	vs.dec = avcodec_alloc_context3(dec);
	avcodec_parameters_to_context(vs.dec, par);
	if (avcodec_open2(vs.dec, dec, nullptr) < 0) return false;

	vs.W = vs.dec->width; vs.H = vs.dec->height;
	vs.sws = sws_getContext(vs.W, vs.H, vs.dec->pix_fmt,
		vs.W, vs.H, AV_PIX_FMT_RGBA,
		SWS_BILINEAR, nullptr, nullptr, nullptr);

	/* OpenGL window + texture */
	glfwInit(); glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	vs.win = glfwCreateWindow(vs.W, vs.H, "player", nullptr, nullptr);
	if (!vs.win) { fprintf(stderr, "window fail\n"); return false; }
	glfwMakeContextCurrent(vs.win); glewInit(); glfwSwapInterval(1);
	glGenTextures(1, &vs.tex); glBindTexture(GL_TEXTURE_2D, vs.tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vs.W, vs.H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glDisable(GL_DEPTH_TEST);
	return true;
}

/* ------- Present a converted RGBA frame ---------------------------------- */
void presentVideoFrame(VideoState& vs, const uint8_t* rgba)
{
	glBindTexture(GL_TEXTURE_2D, vs.tex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vs.W, vs.H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glViewport(0, 0, vs.W, vs.H);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_TEXTURE_2D);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 1); glVertex2f(-1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, -1);
	glTexCoord2f(1, 0); glVertex2f(1, 1);
	glTexCoord2f(0, 0); glVertex2f(-1, 1);
	glEnd();
	glfwSwapBuffers(vs.win);
}

/* -----------------------------------------------------------------
   AUDIO INITIALISATION
   ----------------------------------------------------------------*/
bool initAudio(AVFormatContext* fmt, int streamIdx, AudioState& as)
{
	auto* par = fmt->streams[streamIdx]->codecpar;
	auto* dec = avcodec_find_decoder(par->codec_id);
	as.dec = avcodec_alloc_context3(dec);
	avcodec_parameters_to_context(as.dec, par);
	if (avcodec_open2(as.dec, dec, nullptr) < 0) return false;

	as.swr = swr_alloc();
	av_opt_set_chlayout(as.swr, "in_chlayout", &as.dec->ch_layout, 0);
	AVChannelLayout stereo; av_channel_layout_default(&stereo, 2);
	av_opt_set_chlayout(as.swr, "out_chlayout", &stereo, 0);
	av_opt_set_sample_fmt(as.swr, "in_sample_fmt", as.dec->sample_fmt, 0);
	av_opt_set_sample_fmt(as.swr, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
	av_opt_set_int(as.swr, "in_sample_rate", as.dec->sample_rate, 0);

	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	XAudio2Create(&as.xa, 0);
	as.xa->CreateMasteringVoice(&as.master);
	XAUDIO2_VOICE_DETAILS det{}; as.master->GetVoiceDetails(&det);
	as.deviceRate = det.InputSampleRate;
	av_opt_set_int(as.swr, "out_sample_rate", as.deviceRate, 0);
	swr_init(as.swr);

	WAVEFORMATEX wf = makeWave(as.deviceRate);
	static VoiceCallback cb;
	as.xa->CreateSourceVoice(&as.src, &wf, 0, 2.0f, &cb);
	as.src->Start();
	return true;
}

/* --------------- queue a decoded AAC audio frame --------------------------- */
void queueAudioFrame(AudioState& as, AVFrame* fA)
{
	int inS = fA->nb_samples;
	int maxS = swr_get_out_samples(as.swr, inS);
	int maxB = av_samples_get_buffer_size(nullptr, 2, maxS, AV_SAMPLE_FMT_S16, 0);
	if ((int)as.scratch.size() < maxB) as.scratch.resize(maxB);
	uint8_t* dst[1] = { as.scratch.data() };
	int outS = swr_convert(as.swr, dst, maxS, (const uint8_t**)fA->extended_data, inS);
	if (outS <= 0) return;
	int outB = outS * 2 * 2;
	XAUDIO2_VOICE_STATE st{}; as.src->GetState(&st);
	while (st.BuffersQueued >= 48) { sleepMs(2); as.src->GetState(&st); }
	uint8_t* pcm = new uint8_t[outB]; memcpy(pcm, as.scratch.data(), outB);
	XAUDIO2_BUFFER xb{}; xb.AudioBytes = outB; xb.pAudioData = pcm; xb.pContext = pcm;
	as.src->SubmitSourceBuffer(&xb);
}

/* ----------------------------------------------------------------- */
int main(int argc, char** argv)
{
	if (argc < 2) { printf("Usage: %s video\n", argv[0]); return 0; }
	const char* file = argv[1];

	avformat_network_init();
	AVFormatContext* fmt = nullptr;
	if (avformat_open_input(&fmt, file, nullptr, nullptr) != 0) { fprintf(stderr, "open fail\n"); return 1; }
	avformat_find_stream_info(fmt, nullptr);

	int vIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	int aIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (vIdx < 0) { fprintf(stderr, "no video stream\n"); return 1; }

	VideoState vs;
	if (!initVideoStream(fmt, vIdx, vs)) return 1;
	AudioState as;
	bool haveAudio = (aIdx >= 0) && initAudio(fmt, aIdx, as);

	/* timing vars */
	using Clock = std::chrono::steady_clock;
	auto playStart = Clock::now();
	double firstPts = -1;
	AVRational tb = fmt->streams[vIdx]->time_base;
	uint8_t* rgba = (uint8_t*)alignedAlloc(vs.W * vs.H * 4, 32);

	AVPacket* pkt = av_packet_alloc();
	AVFrame* fV = av_frame_alloc(), * fA = av_frame_alloc();
	bool quit = false;

	while (!quit && !glfwWindowShouldClose(vs.win)) {
		if (av_read_frame(fmt, pkt) < 0) break;

		if (pkt->stream_index == vIdx) {
			if (avcodec_send_packet(vs.dec, pkt) == 0)
				while (avcodec_receive_frame(vs.dec, fV) == 0) {
					double pts = fV->pts * av_q2d(tb);
					if (firstPts < 0) { firstPts = pts; playStart = Clock::now(); }
					double target = pts - firstPts;

					double wall = std::chrono::duration<double>(Clock::now() - playStart).count();
					double lead = target - wall;
					if (lead > 0.003) { if (lead > 0.3) lead = 0.3; sleepMs(int(lead * 1000)); }

					uint8_t* dst[4] = { rgba,nullptr,nullptr,nullptr };
					int ls[4] = { vs.W * 4,0,0,0 };
					sws_scale(vs.sws, fV->data, fV->linesize, 0, vs.H, dst, ls);
					presentVideoFrame(vs, rgba);

					glfwPollEvents();
					if (glfwGetKey(vs.win, GLFW_KEY_ESCAPE) == GLFW_PRESS) quit = true;
				}
		}
		else if (haveAudio && pkt->stream_index == aIdx) {
			if (avcodec_send_packet(as.dec, pkt) == 0)
				while (avcodec_receive_frame(as.dec, fA) == 0)
					queueAudioFrame(as, fA);
		}
		av_packet_unref(pkt);
	}

	/* cleanup */
	alignedFree(rgba); av_frame_free(&fV); av_frame_free(&fA); av_packet_free(&pkt);
	avcodec_free_context(&vs.dec); sws_freeContext(vs.sws);
	if (haveAudio) {
		avcodec_free_context(&as.dec); swr_free(&as.swr);
		as.src->Stop(); as.src->DestroyVoice(); as.master->DestroyVoice(); as.xa->Release();
	}
	glfwDestroyWindow(vs.win); glfwTerminate(); CoUninitialize();
	avformat_close_input(&fmt);
	return 0;
}