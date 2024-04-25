#include <fstream>
#include <iostream>

#include <cxxopts.hpp>
#include <ctranslate2/profiler.h>
#include <ctranslate2/utils.h>

#include "ctranslate2/models/whisper.h"

#include "wav_util.h"
#include "faster-whisper.h"

#include <windows.h>


int main(int argc, char* argv[])
{
	cxxopts::Options cmd_options("whisperWindows", "CTranslate2 whisperWindows client");
	cmd_options.custom_help("--model <directory> [OPTIONS]");

	cmd_options.add_options("General")
		("h,help", "Display available options.")
		("log_profiling", "Log execution profiling.", cxxopts::value<bool>()->default_value("false"))
		("audio", "Path to the 16bit mono wav file to transcribe.", cxxopts::value<std::string>())
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
		("max_queued_batches", "Maximum number of batches to load in advance (set -1 for unlimited, 0 for an automatic value).",
			cxxopts::value<long>()->default_value("0"))
		;

	auto args = cmd_options.parse(argc, argv);

	if (args.count("help")) {
		std::cerr << cmd_options.help() << std::endl;
		return 0;
	}

	if (!args.count("model")) {
		throw std::invalid_argument("Option --model is required to run whisperPOC");
	}

	if (!args.count("audio")) {
		throw std::invalid_argument("Option --audio is required to run whisperPOC");
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
	
	//std::cout << "whisper replicas" << whisper_pool.num_replicas() << std::endl;

	auto log_profiling = args["log_profiling"].as<bool>();
	if (log_profiling)
		ctranslate2::init_profiling(device, whisper_pool.num_replicas());


	/////////////// Load filters and vocab data ///////////////
	bool isMultilingual = (args["model"].as<std::string>() == "en");

	if (!load_filterbank_and_vocab(isMultilingual)) {
		return -1;
	}

	std::string audio_path = args["audio"].as<std::string>();

	std::vector<float> samples = readWAVFile(audio_path.c_str());

	const auto processor_count = std::thread::hardware_concurrency();

	for (auto i = 0; i < WHISPER_SAMPLE_SIZE; i++) samples.push_back(0);
	if (!log_mel_spectrogram(samples.data(), samples.size(), WHISPER_SAMPLE_RATE, WHISPER_N_FFT,
		WHISPER_HOP_LENGTH, WHISPER_N_MEL, processor_count, g_filters, g_mel)) {
		std::cerr << "Failed to compute mel spectrogram" << std::endl;
		return 0;
	}

	pad_or_trim(g_mel);

	ctranslate2::models::WhisperOptions whisper_options;
	ctranslate2::Shape shape{ 1, g_mel.n_mel, g_mel.n_len };
	ctranslate2::StorageView features(shape, g_mel.data, device);


	std::vector<size_t> sot_prompt = { (size_t)g_vocab.token_sot };
	std::vector<std::vector<size_t>> prompts;
	prompts.push_back(sot_prompt);

	std::vector<std::future<ctranslate2::models::WhisperGenerationResult>> results;
	results = whisper_pool.generate(features, prompts, whisper_options);
	for (auto& result : results) {
		ctranslate2::models::WhisperGenerationResult output = result.get();
		for (auto sequence : output.sequences_ids) {
			for (auto id : sequence) {
				if (id == g_vocab.token_eot) break;
				if (id < g_vocab.token_eot)
					printf("%s", whisper_token_to_str(id));
			}
			puts("\n");
			break;
		}
		break;
	}

	if (log_profiling)
		ctranslate2::dump_profiling(std::cerr);


	return 0;
}

