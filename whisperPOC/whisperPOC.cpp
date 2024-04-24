#include <fstream>
#include <iostream>

#include <cxxopts.hpp>
#include <ctranslate2/profiler.h>
#include <ctranslate2/utils.h>

#include "ctranslate2/models/whisper.h"

#include "wav_util.h"
#include "audio_util.h"
#include "filters_vocab_en.h"
#include "filters_vocab_multilingual.h"

#include <windows.h>


int main(int argc, char* argv[])
{
	cxxopts::Options cmd_options("whisperWindows", "CTranslate2 whisperWindows client");
	cmd_options.custom_help("--model <directory> [OPTIONS]");

	cmd_options.add_options("General")
		("h,help", "Display available options.")
		("log_profiling", "Log execution profiling.", cxxopts::value<bool>()->default_value("false"))
	;


	cmd_options.add_options("Device")
		("inter_threads", "Maximum number of CPU asr to run in parallel.",
			cxxopts::value<size_t>()->default_value("1"))
		("intra_threads", "Number of computation threads (set to 0 to use the default value).",
			cxxopts::value<size_t>()->default_value("0"))
		("device", "Device to use (can be cpu, cuda, auto).",
			cxxopts::value<std::string>()->default_value("cpu"))
		("device_index", "Comma-separated list of device IDs to use.",
			cxxopts::value<std::vector<int>>()->default_value("0"))
		("cpu_core_offset", "Pin worker threads to CPU cores starting from this offset.",
			cxxopts::value<int>()->default_value("-1"))
		;

	cmd_options.add_options("Model")
		("model", "Path to the CTranslate2 model directory.", cxxopts::value<std::string>())
		("lang", "Language to transcribe audio", cxxopts::value<std::string>()->default_value("en"))
		("vad", "Boolean for vad. True if use vad.", cxxopts::value<bool>()->default_value("false"))
		("compute_type", "The type used for computation: default, auto, float32, float16, bfloat16, int16, int8, int8_float32, int8_float16, or int8_bfloat16",
			cxxopts::value<std::string>()->default_value("default"))
		("cuda_compute_type", "Computation type on CUDA devices (overrides compute_type)",
			cxxopts::value<std::string>())
		("cpu_compute_type", "Computation type on CPU devices (overrides compute_type)",
			cxxopts::value<std::string>())
		;
	cmd_options.add_options("Data")
		("src", "Path to the source file.",
			cxxopts::value<std::string>())
		("tgt", "Path to the target file to write transcription.",
			cxxopts::value<std::string>())
		("batch_size", "Size of the batch to forward into the model at once.",
			cxxopts::value<size_t>()->default_value("32"))
		("read_batch_size", "Size of the batch to read at once (defaults to batch_size).",
			cxxopts::value<size_t>()->default_value("0"))
		("max_queued_batches", "Maximum number of batches to load in advance (set -1 for unlimited, 0 for an automatic value).",
			cxxopts::value<long>()->default_value("0"))
		("batch_type", "Batch type (can be examples, tokens).",
			cxxopts::value<std::string>()->default_value("examples"))
		("max_input_length", "Truncate inputs after this many tokens (set 0 to disable).",
			cxxopts::value<size_t>()->default_value("1024"))
	;
	auto args = cmd_options.parse(argc, argv);

	if (args.count("help")) {
		std::cerr << cmd_options.help() << std::endl;
		return 0;
	}

	if (!args.count("model")) {
		throw std::invalid_argument("Option --model is required to run whisperPOC");
	}

	size_t inter_threads = args["inter_threads"].as<size_t>();
	size_t intra_threads = args["intra_threads"].as<size_t>();

	const auto device = ctranslate2::str_to_device(args["device"].as<std::string>());
	auto compute_type = ctranslate2::str_to_compute_type(args["compute_type"].as<std::string>());
	switch (device) {
	case ctranslate2::Device::CPU:
		if (args.count("cpu_compute_type"))
			compute_type = ctranslate2::str_to_compute_type(args["cpu_compute_type"].as<std::string>());
		break;
	case ctranslate2::Device::CUDA:
		if (args.count("cuda_compute_type"))
			compute_type = ctranslate2::str_to_compute_type(args["cuda_compute_type"].as<std::string>());
		break;
	};

	ctranslate2::ReplicaPoolConfig pool_config;
	pool_config.num_threads_per_replica = intra_threads;
	pool_config.max_queued_batches = args["max_queued_batches"].as<long>();
	pool_config.cpu_core_offset = args["cpu_core_offset"].as<int>();

	ctranslate2::models::ModelLoader model_loader(args["model"].as<std::string>());
	model_loader.device = device;
	model_loader.device_indices = args["device_index"].as<std::vector<int>>();
	model_loader.compute_type = compute_type;
	model_loader.num_replicas_per_device = inter_threads;

	ctranslate2::models::Whisper whisper_pool(model_loader, pool_config);

	/////////////// Load filters and vocab data ///////////////

	const char* vocabData = nullptr;
	bool isMultilingual = false;

	if (isMultilingual)
		vocabData = reinterpret_cast<const char*>(filters_vocab_multilingual);
	else
		vocabData = reinterpret_cast<const char*>(filters_vocab_en);

	// Read the magic number
	int magic = 0;
	std::memcpy(&magic, vocabData, sizeof(magic));
	vocabData += sizeof(magic);

	// Check the magic number
	if (magic != 0x57535052) { // 'WSPR'
		std::cerr << "Invalid vocab data (bad magic)" << std::endl;
		return -1;
	}

	// Load mel filters
	std::memcpy(&filters.n_mel, vocabData, sizeof(filters.n_mel));
	vocabData += sizeof(filters.n_mel);

	std::memcpy(&filters.n_fft, vocabData, sizeof(filters.n_fft));
	vocabData += sizeof(filters.n_fft);

	std::cout << "n_mel:" << filters.n_mel << " n_fft:" << filters.n_fft << std::endl;

	filters.data.resize(filters.n_mel * filters.n_fft);
	std::memcpy(filters.data.data(), vocabData, filters.data.size() * sizeof(float));
	vocabData += filters.data.size() * sizeof(float);



	auto log_profiling = args["log_profiling"].as<bool>();
	if (log_profiling)
		ctranslate2::init_profiling(device, whisper_pool.num_replicas());

	std::string audio_path = "jfk.wav";
	if (args.count("src")) {
		auto audio_path = args["src"].as<std::string>();
	}

	std::vector<float> samples = readWAVFile(audio_path.c_str());

	size_t originalSize = samples.size();

	const auto processor_count = std::thread::hardware_concurrency();


	if (!log_mel_spectrogram(samples.data(), samples.size(), WHISPER_SAMPLE_RATE, WHISPER_N_FFT,
		WHISPER_HOP_LENGTH, WHISPER_N_MEL, processor_count, filters, mel)) {
		std::cerr << "Failed to compute mel spectrogram" << std::endl;
		return 0;
	}

	ctranslate2::models::WhisperOptions whisper_options;
	ctranslate2::Shape shape{ 1, mel.n_mel, mel.n_len };
	ctranslate2::StorageView features(shape, mel.data, device);
	std::vector<std::vector<std::string>> prompts;

	std::vector<std::future<ctranslate2::models::WhisperGenerationResult>> results;
	results = whisper_pool.generate(features, prompts, whisper_options);

	std::vector<ctranslate2::models::WhisperGenerationResult> outputs;

	Sleep(100000);

	for (auto & result : results) {
		outputs.push_back(result.get());
	}

	for (auto output : outputs) {
		for (auto sequence : output.sequences) {
			for (auto word : sequence) {
				printf("%s", word.c_str());
			}
			puts("");
		}
	}

	puts("---------------");
	if (log_profiling)
		ctranslate2::dump_profiling(std::cerr);


	return 0;
}

