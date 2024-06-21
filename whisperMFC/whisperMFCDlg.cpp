
// whisperMFCDlg.cpp : implementation file
//


#include "framework.h"
#include "whisperMFC.h"
#include "whisperMFCDlg.h"
#include "afxdialogex.h"

#include "faster-whisper.h"

#include "sndfile.h"
#include <chrono>

#include "libresample.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define TARGET_SAMPLE_RATE 16000
#define BUFFER_SIZE 1024
#define WHISPER_MODEL_DIR "model"

// CAboutDlg dialog used for App About

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

// Implementation
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CwhisperMFCDlg dialog



CwhisperMFCDlg::CwhisperMFCDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_WHISPERMFC_DIALOG, pParent)
	, m_strPath(_T(""))
	, m_strResult(_T(""))
	, m_strStatus(_T(""))
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CwhisperMFCDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_STATIC_PATH, m_strPath);
	DDX_Text(pDX, IDC_STATIC_RESULT, m_strResult);
	DDX_Text(pDX, IDC_STATIC_STATUS, m_strStatus);
}

BEGIN_MESSAGE_MAP(CwhisperMFCDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BTN_EXIT, &CwhisperMFCDlg::OnBnClickedBtnExit)
	ON_BN_CLICKED(IDC_BTN_OPEN, &CwhisperMFCDlg::OnBnClickedBtnOpen)
END_MESSAGE_MAP()


// CwhisperMFCDlg message handlers

BOOL CwhisperMFCDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	// TODO: Add extra initialization here

	if (initModel() == false) {
		exit(-1);
	}

	m_strStatus = "Whisper model was initialized to transcribe audios...";
	UpdateData(false);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CwhisperMFCDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CwhisperMFCDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CwhisperMFCDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}



void CwhisperMFCDlg::OnBnClickedBtnExit()
{
	// TODO: Add your control notification handler code here
	CDialog::OnOK();
}

void readAudioFile(const char* filename, std::vector<float>& float_samples) {
	SF_INFO sfinfo;
	SNDFILE* sndfile;

	// Open the sound file
	sndfile = sf_open(filename, SFM_READ, &sfinfo);
	if (!sndfile) {
		std::cerr << "Error opening file: " << filename << std::endl;
		return;
	}
	if (sfinfo.channels > 1) {
		std::cerr << "Error opening file: " << filename << std::endl;
		return;
	}

	double srcrate = sfinfo.samplerate;
	double tgtrate = 16000;
	double ratio = tgtrate / srcrate;

	//// Check input file format
	//if (sfinfo.samplerate != TARGET_SAMPLE_RATE) {
	//	std::cerr << "Input file does not have the target sample rate of " << TARGET_SAMPLE_RATE << " Hz.\n";
	//	sf_close(sndfile);
	//	return;
	//}

	// Calculate the number of samples to read
	sf_count_t num_samples = sfinfo.frames;

	std::vector<float> input_samples(num_samples);

	// Read all samples into float_samples
	sf_read_float(sndfile, input_samples.data(), num_samples);

	// Close the sound file
	sf_close(sndfile);

	// Check for errors
	if (sf_error(sndfile) != SF_ERR_NO_ERROR) {
		std::cerr << "Error reading file: " << sf_strerror(sndfile) << std::endl;
	}


	void* resampler =resample_open(1, ratio, ratio);


	int out_sampes = num_samples * ratio;
	// Resize the vector to hold all samples
	float_samples.resize(out_sampes + 1000);

	int inUsed = 0;
	int out = resample_process(resampler, ratio, input_samples.data(), num_samples, true,
		&inUsed, float_samples.data(), out_sampes);

	float_samples.resize(out);

	resample_close(resampler);
}



void CwhisperMFCDlg::OnBnClickedBtnOpen()
{
	// TODO: Add your control notification handler code here
	CFileDialog dlg(TRUE);  // TRUE for "Open" dialog, FALSE for "Save As" dialog
	if (dlg.DoModal() == IDOK) {
		CString filePath = dlg.GetPathName();

		CStringA filePathA(filePath);  // Convert CString to CStringA
		const char* filename =
			filePathA.GetString();  // Get pointer to internal character buffer
		m_strPath = filePath;
		UpdateData(false);


		std::vector<float> float_samples;

		readAudioFile(filename, float_samples);

		m_strStatus = "Transcribing audio. Please wait...";
		UpdateData(false);


		// Start time measurement
		auto start = std::chrono::high_resolution_clock::now();
		std::string res = transcribeAudio(float_samples);

		// End time measurement
		auto end = std::chrono::high_resolution_clock::now();

		// Calculate duration and convert it to milliseconds
		std::chrono::duration<double, std::milli> duration_ms = end - start;

		m_strResult = res.c_str();
		m_strStatus.Format(L"It took %.3lfs to transcribe audio of length %.3lfs.", duration_ms.count()/1000, (double)float_samples.size()/TARGET_SAMPLE_RATE);

		UpdateData(false);
	}
}

std::string CwhisperMFCDlg::transcribeAudio(std::vector<float> samples)
{
	const auto processor_count = std::thread::hardware_concurrency();

	for (auto i = 0; i < WHISPER_SAMPLE_SIZE; i++) samples.push_back(0);
	if (!log_mel_spectrogram(samples.data(), samples.size(), WHISPER_SAMPLE_RATE, WHISPER_N_FFT,
		WHISPER_HOP_LENGTH, WHISPER_N_MEL, processor_count, g_filters, g_mel)) {
		std::cerr << "Failed to compute mel spectrogram" << std::endl;
		return "";
	}

	pad_or_trim(g_mel);


	//ctranslate2::Device m_device;
	//ctranslate2::models::Whisper *m_whisper_pool;

	ctranslate2::models::WhisperOptions whisper_options;
	ctranslate2::Shape shape{ 1, g_mel.n_mel, g_mel.n_len };
	ctranslate2::StorageView features(shape, g_mel.data, m_device);


	std::vector<size_t> sot_prompt = { (size_t)g_vocab.token_sot };
	std::vector<std::vector<size_t>> prompts;
	prompts.push_back(sot_prompt);

	std::vector<std::future<ctranslate2::models::WhisperGenerationResult>> results;
	ctranslate2::models::WhisperGenerationResult output;

	results = m_whisper_pool->generate(features, prompts, whisper_options);

	std::string resultStr = "";
	for (auto& result : results) {
		try {
			output = result.get();
		}
		catch (std::exception& e) {
			std::cout << "Exception " << e.what() << std::endl;
		}
		for (auto sequence : output.sequences_ids) {
			for (auto id : sequence) {
				if (id == g_vocab.token_eot) break;
				if (id < g_vocab.token_eot) {
					printf("%s", whisper_token_to_str(id));
					resultStr += whisper_token_to_str(id);
				}
			}
			puts("\n");
			resultStr += "\n";
			break;
		}
		break;
	}

	if (m_log_profiling)
		ctranslate2::dump_profiling(std::cerr);

	return resultStr;
}

bool CwhisperMFCDlg::initModel()
{

	//ctranslate2::Device m_device;
	//ctranslate2::models::Whisper *m_whisper_pool;


	ctranslate2::ReplicaPoolConfig pool_config;
	pool_config.num_threads_per_replica = 1;
	pool_config.max_queued_batches = 0;
	pool_config.cpu_core_offset = -1;

	ctranslate2::models::ModelLoader model_loader(WHISPER_MODEL_DIR);

	m_device = ctranslate2::str_to_device("cpu");

	model_loader.device = m_device;
	model_loader.device_indices = { 0 };
	model_loader.compute_type = ctranslate2::ComputeType::DEFAULT;
	model_loader.num_replicas_per_device = 1;


	m_whisper_pool = new ctranslate2::models::Whisper(model_loader, pool_config);

	//std::cout << "whisper replicas" << whisper_pool.num_replicas() << std::endl;

	m_log_profiling = false;
	if (m_log_profiling)
		ctranslate2::init_profiling(m_device, m_whisper_pool->num_replicas());


	/////////////// Load filters and vocab data ///////////////
	bool isMultilingual = false;

	if (!load_filterbank_and_vocab(isMultilingual)) {
		return false;
	}


	return true;
}
