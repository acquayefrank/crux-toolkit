#include "CruxLFQApplication.h"

#include <cmath>
#include <exception>
#include <sstream>

#include "IndexedMassSpectralPeak.h"
#include "crux-lfq/IntensityNormalizationEngine.h"
#include "crux-lfq/Results.h"
#include "crux-lfq/Utils.h"
#include "indexed_mass_spectral_peak.pb.h"
#include "io/carp.h"
#include "util/FileUtils.h"
#include "util/Params.h"
#include "util/crux-utils.h"

using std::make_pair;
using std::pair;
using std::string;
using std::unordered_map;
using std::vector;

using pwiz::cv::MS_ms_level;
using pwiz::cv::MS_scan_start_time;
using pwiz::msdata::BinaryDataArrayPtr;
using pwiz::msdata::MSDataFile;
using pwiz::msdata::SpectrumListSimple;
using pwiz::msdata::SpectrumListSimplePtr;
using pwiz::msdata::SpectrumPtr;

typedef pwiz::msdata::SpectrumListPtr SpectrumListPtr;

int CruxLFQ::NUM_ISOTOPES_REQUIRED = 2;                       // Default value is 2
double CruxLFQ::PEAK_FINDING_PPM_TOLERANCE = 20.0;            // Default value is 20.0
double CruxLFQ::PPM_TOLERANCE = 10.0;                         // Default value is 10.0
bool CruxLFQ::ID_SPECIFIC_CHARGE_STATE = false;               // Default value is false
int CruxLFQ::MISSED_SCANS_ALLOWED = 1;                        // Default value is 1
double CruxLFQ::ISOTOPE_TOLERANCE_PPM = 5.0;                  // Default value is 5.0
bool CruxLFQ::INTEGRATE = false;                              // Default value is false
double CruxLFQ::DISCRIMINATION_FACTOR_TO_CUT_PEAK = 0.6;      // Default value is 0.6
bool CruxLFQ::QUANTIFY_AMBIGUOUS_PEPTIDES = false;            // Default value is false
bool CruxLFQ::USE_SHARED_PEPTIDES_FOR_PROTEIN_QUANT = false;  // Default value is false
bool CruxLFQ::NORMALIZE = false;                              // Default value is false
// MBR settings
// bool CruxLFQ::MATCH_BETWEEN_RUNS = false;                 // Default value is false
// double CruxLFQ::MATCH_BETWEEN_RUNS_PPM_TOLERANCE = 10.0;  // Default value is 10.0
// double CruxLFQ::MAX_MBR_WINDOW = 2.5;                     // Default value is 2.5
// bool CruxLFQ::REQUIRE_MSMS_ID_IN_CONDITION = false;       // Default value is false

CruxLFQApplication::CruxLFQApplication() {}

CruxLFQApplication::~CruxLFQApplication() {}

int CruxLFQApplication::main(int argc, char** argv) {
    string psm_file = Params::GetString("lfq-peptide-spectrum matches");
    vector<string> spec_files = Params::GetStrings("spectrum files");
    return main(psm_file, spec_files);
}

int CruxLFQApplication::main(const string& psm_file, const vector<string>& spec_files) {
    carp(CARP_INFO, "Running crux-lfq...");

    CruxLFQ::NUM_ISOTOPES_REQUIRED = Params::GetInt("num-isotopes-required");                                   // Default value is 2
    CruxLFQ::PEAK_FINDING_PPM_TOLERANCE = Params::GetDouble("peak-finding-ppm-tolerance");                      // Default value is 20.0
    CruxLFQ::PPM_TOLERANCE = Params::GetDouble("ppm-tolerance");                                                // Default value is 10.0
    CruxLFQ::ID_SPECIFIC_CHARGE_STATE = Params::GetBool("id-specific-charge-state");                            // Default value is false
    CruxLFQ::MISSED_SCANS_ALLOWED = Params::GetInt("missed-scans-allowed");                                     // Default value is 1
    CruxLFQ::ISOTOPE_TOLERANCE_PPM = Params::GetDouble("isotope-tolerance-ppm");                                // Default value is 5.0
    CruxLFQ::INTEGRATE = Params::GetBool("integrate");                                                          // Default value is false
    CruxLFQ::DISCRIMINATION_FACTOR_TO_CUT_PEAK = Params::GetDouble("discrimination-factor-to-cut-peak");        // Default value is 0.6
    CruxLFQ::QUANTIFY_AMBIGUOUS_PEPTIDES = Params::GetBool("quantify-ambiguous-peptides");                      // Default value is false
    CruxLFQ::USE_SHARED_PEPTIDES_FOR_PROTEIN_QUANT = Params::GetBool("use-shared-peptides-for-protein-quant");  // Default value is false
    CruxLFQ::NORMALIZE = Params::GetBool("normalize");                                                          // Default value is false
    // MBR settings --- I may remove all code concerning MBR
    // CruxLFQ::MATCH_BETWEEN_RUNS = Params::GetBool("match-between-runs");                                // Default value is false
    // CruxLFQ::MATCH_BETWEEN_RUNS_PPM_TOLERANCE = Params::GetDouble("match-between-runs-ppm-tolerance");  // Default value is 10.0
    // CruxLFQ::MAX_MBR_WINDOW = Params::GetDouble("max-mbr-window");                                      // Default value is 2.5
    // CruxLFQ::REQUIRE_MSMS_ID_IN_CONDITION = Params::GetBool("require-msms-id-in-condition");            // Default value is false

    string output_dir = Params::GetString("output-dir");
    string psm_file_format = Params::GetString("psm-file-format");

    if (!FileUtils::Exists(psm_file)) {
        carp(CARP_FATAL, "PSM file %s not found", psm_file.c_str());
    }
    map<int, CruxLFQ::PSM> psm_datum = CruxLFQ::create_psm_map(psm_file, psm_file_format);
    CruxLFQ::CruxLFQResults lfqResults(spec_files);

    vector<CruxLFQ::Identification> allIdentifications;
    std::unordered_set<CruxLFQ::Identification> uniqueIdentifications;
    for (const string& spectra_file : spec_files) {
        SpectrumListPtr spectra_ms2 = loadSpectra(spectra_file, 2);
        carp(CARP_INFO, "Read %d spectra. for MS2 from %s", spectra_ms2->size(), spectra_file.c_str());
        vector<CruxLFQ::Identification> tempIdentifications = createIdentifications(psm_datum, spectra_file, spectra_ms2);
        for (auto& id : tempIdentifications) {
            uniqueIdentifications.insert(id);
        }
    }
    std::copy(uniqueIdentifications.begin(), uniqueIdentifications.end(), std::back_inserter(allIdentifications));

    lfqResults.setPeptideModifiedSequencesAndProteinGroups(allIdentifications);

    for (const string& spectra_file : spec_files) {
        SpectrumListPtr spectra_ms1 = loadSpectra(spectra_file, 1);
        carp(CARP_INFO, "Read %d spectra. for MS1. from %s", spectra_ms1->size(), spectra_file.c_str());

        CruxLFQ::IndexedSpectralResults indexResults = indexedMassSpectralPeaks(spectra_ms1, spectra_file);

        unordered_map<string, vector<pair<double, double>>> modifiedSequenceToIsotopicDistribution = CruxLFQ::calculateTheoreticalIsotopeDistributions(allIdentifications);

        CruxLFQ::setPeakFindingMass(allIdentifications, modifiedSequenceToIsotopicDistribution);
        vector<double> chargeStates = CruxLFQ::createChargeStates(allIdentifications);

        CruxLFQ::quantifyMs2IdentifiedPeptides(
            spectra_file,
            allIdentifications,
            chargeStates,
            indexResults._ms1Scans,
            indexResults._indexedPeaks,
            modifiedSequenceToIsotopicDistribution,
            lfqResults);

        // if (CruxLFQ::MATCH_BETWEEN_RUNS) {
        //     crux_lfq::IndexedSpectralResults proto_data;

        //     // Populate indexed_peaks
        //     for (const auto& outer_pair : indexResults._indexedPeaks) {
        //         int outer_key = outer_pair.first;
        //         const auto& inner_map = outer_pair.second;

        //         // Get a mutable pointer to the InnerMap object associated with outer_key
        //         crux_lfq::InnerMap* proto_inner_map = &(*proto_data.mutable_indexed_peaks())[outer_key];

        //         for (const auto& inner_pair : inner_map) {
        //             int inner_key = inner_pair.first;
        //             const CruxLFQ::IndexedMassSpectralPeak& peak = inner_pair.second;

        //             // Get a mutable pointer to the IndexedMassSpectralPeak object associated with inner_key
        //             crux_lfq::IndexedMassSpectralPeak* proto_peak = &(*proto_inner_map->mutable_inner_map())[inner_key];

        //             // Populate the fields
        //             proto_peak->set_mz(peak.mz);
        //             proto_peak->set_intensity(peak.intensity);
        //             proto_peak->set_zero_based_ms1_scan_index(peak.zeroBasedMs1ScanIndex);
        //             proto_peak->set_retention_time(peak.retentionTime);
        //         }
        //     }

        //     // Populate ms1_scans
        //     for (const auto& entry : indexResults._ms1Scans) {
        //         const std::string& key = entry.first;
        //         const std::vector<CruxLFQ::Ms1ScanInfo>& scan_info_vector = entry.second;

        //         // Get a mutable pointer to the Ms1ScanInfoList object associated with key
        //         crux_lfq::Ms1ScanInfoList* proto_scan_info_list = &(*proto_data.mutable_ms1_scans())[key];

        //         for (const CruxLFQ::Ms1ScanInfo& scan_info : scan_info_vector) {
        //             // Add a new Ms1ScanInfo to the ms1_scan_info repeated field
        //             crux_lfq::Ms1ScanInfo* proto_scan_info = proto_scan_info_list->add_ms1_scan_info();

        //             // Populate the fields
        //             proto_scan_info->set_one_based_scan_number(scan_info.oneBasedScanNumber);
        //             proto_scan_info->set_zero_based_ms1_scan_index(scan_info.zeroBasedMs1ScanIndex);
        //             proto_scan_info->set_retention_time(scan_info.retentionTime);
        //         }
        //     }

        //     // Serialize to binary format
        //     std::string serialized_data;
        //     proto_data.SerializeToString(&serialized_data);
        //     string file_name = spectra_file.substr(spectra_file.find_last_of("/\\") + 1);
        //     string mbr_file = FileUtils::Join(output_dir, file_name + ".pb");
        //     // Write the serialized data to a file
        //     std::ofstream output_file(mbr_file, std::ios::binary);
        //     if (output_file.is_open()) {
        //         output_file.write(serialized_data.c_str(), serialized_data.size());
        //         output_file.close();
        //     } else {
        //         // Handle error opening the file
        //         carp(CARP_FATAL, "Error opening file %s", mbr_file.c_str());
        //     }
        // }

        CruxLFQ::runErrorChecking(spectra_file, lfqResults);

        carp(CARP_INFO, "Finished processing %s", spectra_file.c_str());
    }

    // if (CruxLFQ::MATCH_BETWEEN_RUNS) {
    //     carp(CARP_INFO, "Running match-between-runs...");
    //     for (const string& spectra_file : spec_files) {
    //         carp(CARP_INFO, "Doing match-between-runs for %s", spectra_file.c_str());
    //         CruxLFQ::quantifyMatchBetweenRunsPeaks(spectraFile);
    //         carp(CARP_INFO, "Finished MBR for %s", spectra_file.c_str());
    //     }
    // }

    if (CruxLFQ::NORMALIZE) {
        CruxLFQ::IntensityNormalizationEngine intensityNormalizationEngine(
            lfqResults,
            CruxLFQ::INTEGRATE,
            CruxLFQ::QUANTIFY_AMBIGUOUS_PEPTIDES);
        intensityNormalizationEngine.NormalizeResults();
    }

    lfqResults.calculatePeptideResults(CruxLFQ::QUANTIFY_AMBIGUOUS_PEPTIDES);
    lfqResults.calculateProteinResultsMedianPolish(CruxLFQ::USE_SHARED_PEPTIDES_FOR_PROTEIN_QUANT);
    const std::string results_file = make_file_path("crux-lfq.txt");
    lfqResults.writeResults(results_file, spec_files);

    return 0;
}

string CruxLFQApplication::getName() const {
    return "crux-lfq";
}

string CruxLFQApplication::getDescription() const {
    return "[[nohtml:This command reads a set of PSMs and a corresponding set of spectrum files"
           "and carries out label-free quantification (LFQ) for each detected peptide.]]"
           "[[html:<p>This command reads a set of PSMs and a corresponding set of spectrum files "
           "and carries out label-free quantification (LFQ) for each detected peptide."
           "The algorithm follows that of FlashLFQ: "
           "Millikin RJ, Solntsev SK, Shortreed MR, Smith LM. &quot;<a href=\""
           "https://pubmed.ncbi.nlm.nih.gov/29083185/\">Ultrafast Peptide Label-Free Quantification with FlashLFQ.</a>&quot;"
           "<em>Journal of Proteome Research</em>. 17(1):386-391, 2018.</blockquote><p>]]";
}

vector<string> CruxLFQApplication::getArgs() const {
    string arr[] = {
        "lfq-peptide-spectrum matches",
        "spectrum files+"};
    return vector<string>(arr, arr + sizeof(arr) / sizeof(string));
}

vector<string> CruxLFQApplication::getOptions() const {
    string arr[] = {
        "score",
        "threshold",
        "smaller-is-better",
        "fileroot",
        "output-dir",
        "overwrite",
        "parameter-file",
        "verbosity",
        "num-isotopes-required",
        "peak-finding-ppm-tolerance",
        "ppm-tolerance",
        "id-specific-charge-state",
        "isotope-tolerance-ppm",
        "integrate",
        "discrimination-factor-to-cut-peak",
        "quantify-ambiguous-peptides",
        "use-shared-peptides-for-protein-quant",
        "normalize",
        // MBR settings
        "match-between-runs",
        "match-between-runs-ppm-tolerance",
        "max-mbr-window",
        "require-msms-id-in-condition",
    };
    return vector<string>(arr, arr + sizeof(arr) / sizeof(string));
}

vector<pair<string, string>> CruxLFQApplication::getOutputs() const {
    vector<pair<string, string>> outputs;
    outputs.push_back(make_pair("crux-lfq.txt",
                                "A tab-delimited text file in which rows are peptides, "
                                "columns correspond to the different spectrum files, "
                                "and values are peptide quantifications.  "
                                "If a peptide is not detected in a given run, "
                                "then its corresponding quantification value is NaN."));
    outputs.push_back(make_pair("crux-lfq.params.txt",
                                "A file containing the name and value of all parameters/options"
                                " for the current operation. Not all parameters in the file may have"
                                " been used in the operation. The resulting file can be used with the "
                                "--parameter-file option for other Crux programs."));
    outputs.push_back(make_pair("crux-lfq.log.txt",
                                "A log file containing a copy of all messages that were printed to the screen during execution."));
    return outputs;
}

COMMAND_T CruxLFQApplication::getCommand() const {
    return CRUX_LFQ_COMMAND;
}

bool CruxLFQApplication::needsOutputDirectory() const {
    return true;
}

// TODO: Add parameter processing
void CruxLFQApplication::processParams() {
}

SpectrumListPtr CruxLFQApplication::loadSpectra(const string& file, int msLevel) {
    try {
        MSDataFile msd(file);
        SpectrumListPtr originalSpectrumList = msd.run.spectrumListPtr;
        if (!originalSpectrumList) {
            carp(CARP_FATAL, "Error reading spectrum file %s", file.c_str());
        }
        SpectrumListSimplePtr filteredSpectrumList(new SpectrumListSimple);

        for (size_t i = 0; i < originalSpectrumList->size(); ++i) {
            SpectrumPtr spectrum = originalSpectrumList->spectrum(i);

            int spectrumMSLevel = spectrum->cvParam(MS_ms_level).valueAs<int>();
            if (spectrumMSLevel == msLevel) {
                // Add the spectrum to the filtered list
                filteredSpectrumList->spectra.push_back(spectrum);
            }
        }

        return filteredSpectrumList;
    } catch (const std::exception& e) {
        carp(CARP_INFO, "Error:  %s", e.what());
        return nullptr;
    }
}

IndexedSpectralResults CruxLFQApplication::indexedMassSpectralPeaks(SpectrumListPtr spectrum_collection, const string& spectra_file) {
    string _spectra_file(spectra_file);

    map<int, map<int, IndexedMassSpectralPeak>> _indexedPeaks;
    unordered_map<string, vector<Ms1ScanInfo>> _ms1Scans;

    _ms1Scans[_spectra_file] = vector<Ms1ScanInfo>();

    IndexedSpectralResults index_results{_indexedPeaks, _ms1Scans};

    if (!spectrum_collection) {
        return index_results;
    }

    int _scanIndex = 0;
    int _oneBasedScanNumber = 1;

    for (size_t i = 0; i < spectrum_collection->size(); ++i) {
        SpectrumPtr spectrum = spectrum_collection->spectrum(i);

        if (spectrum) {
            BinaryDataArrayPtr mzs = spectrum->getMZArray();
            BinaryDataArrayPtr intensities = spectrum->getIntensityArray();

            int scanIndex;
            int oneBasedScanNumber;
            std::string scanId = getScanID(spectrum->id);
            if (scanId.empty()) {
                scanIndex = _scanIndex;
                oneBasedScanNumber = _oneBasedScanNumber;
            } else {
                scanIndex = std::stoi(scanId);
                oneBasedScanNumber = scanIndex + 1;
            }

            double retentionTime = spectrum->scanList.scans[0].cvParam(MS_scan_start_time).timeInSeconds();

            if (mzs && intensities) {
                const std::vector<double>& mzArray = mzs->data;
                const std::vector<double>& intensityArray = intensities->data;
                for (size_t j = 0; j < mzArray.size(); ++j) {
                    FLOAT_T mz = mzArray[j];
                    int roundedMz = static_cast<int>(std::round(mz * BINS_PER_DALTON));
                    IndexedMassSpectralPeak spec_data(
                        mz,                 // mz value
                        intensityArray[j],  // intensity
                        scanIndex,          // zeroBasedMs1ScanIndex
                        retentionTime);

                    auto& indexedPeaks = index_results._indexedPeaks;
                    auto it = indexedPeaks.find(roundedMz);
                    if (it == indexedPeaks.end()) {
                        map<int, IndexedMassSpectralPeak> tmp;
                        tmp.insert({scanIndex, spec_data});
                        indexedPeaks[roundedMz] = tmp;
                    } else {
                        it->second.insert({scanIndex, spec_data});
                    }

                    Ms1ScanInfo scan = {oneBasedScanNumber, scanIndex, retentionTime};
                    index_results._ms1Scans[spectra_file].push_back(scan);
                }
            }

            _scanIndex++;
            _oneBasedScanNumber++;
        }
    }
    return index_results;
}

// Make this a multithreaded process
vector<Identification> CruxLFQApplication::createIdentifications(const map<int, PSM>& psm_datum, const string& spectra_file, SpectrumListPtr spectrum_collection) {
    carp(CARP_INFO, "Creating indentifications, this may take a bit of time, do not terminate the process...");

    vector<Identification> allIdentifications;
    string _spectra_file(spectra_file);

    for (size_t i = 0; i < spectrum_collection->size(); ++i) {
        SpectrumPtr spectrum = spectrum_collection->spectrum(i);
        if (spectrum) {
            int scanIndex;
            std::string scanId = getScanID(spectrum->id);
            if (scanId.empty()) {
                continue;
            } else {
                scanIndex = std::stoi(scanId);
            }

            auto it = psm_datum.find(scanIndex);

            if (it != psm_datum.end()) {
                double retentionTimeInSeconds = spectrum->scanList.scans[0].cvParam(MS_scan_start_time).timeInSeconds();

                FLOAT_T retentionTimeInMinutes = retentionTimeInSeconds / 60.0;

                Identification identification;

                identification.sequence = it->second.sequence_col;
                identification.monoIsotopicMass = it->second.peptide_mass_col;
                identification.charge = it->second.charge_col;
                identification.peptideMass = it->second.peptide_mass_col;
                identification.precursorCharge = it->second.spectrum_precursor_mz_col;
                identification.spectralFile = _spectra_file;
                identification.ms2RetentionTimeInMinutes = retentionTimeInMinutes;
                identification.scanId = it->second.scan_col;
                identification.modifications = it->second.modifications;
                allIdentifications.push_back(identification);
            }
        }
    }

    return allIdentifications;
}