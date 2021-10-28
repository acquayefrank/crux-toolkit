#include <cstdio>
#include <numeric>
#include "app/tide/abspath.h"
#include "app/tide/records_to_vector-inl.h"

#include "io/carp.h"
#include "parameter.h"
#include "io/SpectrumRecordWriter.h"
#include "TideIndexApplication.h"
#include "TideSearchApplication.h"
#include "DIAmeterApplication.h"
#include "MakePinApplication.h"
#include "PercolatorApplication.h"
#include "tide/mass_constants.h"
#include "TideMatchSet.h"
#include "util/Params.h"
#include "util/FileUtils.h"
#include "util/StringUtils.h"
#include "util/MathUtil.h"

#include "io/DIAmeterFeatureScaler.h"
#include "io/DIAmeterPSMFilter.h"
// #include "io/DIAmeterCVSelector.h"

const double DIAmeterApplication::XCORR_SCALING = 100000000.0;
const double DIAmeterApplication::RESCALE_FACTOR = 20.0;

DIAmeterApplication::DIAmeterApplication():
  remove_index_(""), output_pin_(""), output_percolator_("") { /* do nothing */
}

DIAmeterApplication::~DIAmeterApplication() {
  if (!remove_index_.empty() && FileUtils::Exists(remove_index_) ) {
  carp(CARP_DEBUG, "Removing temp index '%s'", remove_index_.c_str());
  // FileUtils::Remove(remove_index_);
  }
}


int DIAmeterApplication::main(int argc, char** argv) {
  return main(Params::GetStrings("tide spectra file"));
}
int DIAmeterApplication::main(const vector<string>& input_files) {
  return main(input_files, Params::GetString("tide database"));
}

int DIAmeterApplication::main(const vector<string>& input_files, const string input_index) {
  carp(CARP_INFO, "Running diameter...");

  // DIAmeter supports spectrum centric match report only
  if (Params::GetBool("peptide-centric-search")) { carp(CARP_FATAL, "Spectrum-centric match only!"); }

  const string index = input_index;
  string peptides_file = FileUtils::Join(index, "pepix");
  string proteins_file = FileUtils::Join(index, "protix");
  string auxlocs_file = FileUtils::Join(index, "auxlocs");

  double bin_width_  = Params::GetDouble("mz-bin-width");
  double bin_offset_ = Params::GetDouble("mz-bin-offset");
  vector<int> negative_isotope_errors = TideSearchApplication::getNegativeIsotopeErrors();

  // Read proteins index file
  ProteinVec proteins;
  pb::Header protein_header;
  if (!ReadRecordsToVector<pb::Protein, const pb::Protein>(&proteins, proteins_file, &protein_header)) { carp(CARP_FATAL, "Error reading index (%s)", proteins_file.c_str()); }

  // Read auxlocs index file
  vector<const pb::AuxLocation*> locations;
  if (!ReadRecordsToVector<pb::AuxLocation>(&locations, auxlocs_file)) { carp(CARP_FATAL, "Error reading index (%s)", auxlocs_file.c_str()); }

  // Read peptides index file
  pb::Header peptides_header;
  HeadedRecordReader* peptide_reader = new HeadedRecordReader(peptides_file, &peptides_header);
  if ((peptides_header.file_type() != pb::Header::PEPTIDES) || !peptides_header.has_peptides_header()) { carp(CARP_FATAL, "Error reading index (%s)", peptides_file.c_str()); }

  // Some search setup adoped from TideSearch which I don't fully understand
  const pb::Header::PeptidesHeader& pepHeader = peptides_header.peptides_header();
  MassConstants::Init(&pepHeader.mods(), &pepHeader.nterm_mods(), &pepHeader.cterm_mods(), &pepHeader.nprotterm_mods(), &pepHeader.cprotterm_mods(), bin_width_, bin_offset_);
  ModificationDefinition::ClearAll();
  TideMatchSet::initModMap(pepHeader.mods(), ANY);
  TideMatchSet::initModMap(pepHeader.nterm_mods(), PEPTIDE_N);
  TideMatchSet::initModMap(pepHeader.cterm_mods(), PEPTIDE_C);
  TideMatchSet::initModMap(pepHeader.nprotterm_mods(), PROTEIN_N);
  TideMatchSet::initModMap(pepHeader.cprotterm_mods(), PROTEIN_C);

  // Output setup
  string output_file_name_unsorted_ = make_file_path("diameter.tmp.txt");
  string output_file_name_scaled_ = make_file_path("diameter.psm-features.txt");
  string output_file_name_filtered_ = make_file_path("diameter.psm-features.filtered.txt");

  /*
  if (Params::GetBool("psm-filter")) {
    stringstream param_ss;
    param_ss << "diameter-search.filtered." << getCoeffTag() << ".txt";
    output_file_name_filtered_ = make_file_path(param_ss.str().c_str());
  }
  */

  // Extract all edge features
  if (!FileUtils::Exists(output_file_name_unsorted_) /*|| Params::GetBool("overwrite")*/ ) {
    carp(CARP_DEBUG, "Either file exists or it needs to be overwritten: %s", output_file_name_unsorted_.c_str());

    ofstream* output_file = create_stream_in_path(output_file_name_unsorted_.c_str(), NULL, Params::GetBool("overwrite"));
    TideMatchSet::writeHeadersDIA(output_file, Params::GetBool("compute-sp"));

    map<string, double> peptide_predrt_map;
    getPeptidePredRTMapping(&peptide_predrt_map);

    vector<InputFile> ms1_spectra_files = getInputFiles(input_files, 1);
    vector<InputFile> ms2_spectra_files = getInputFiles(input_files, 2);

    // Loop through spectrum files
    for (pair<vector<InputFile>::const_iterator, vector<InputFile>::const_iterator> f(ms1_spectra_files.begin(), ms2_spectra_files.begin());
     f.first != ms1_spectra_files.end() && f.second != ms2_spectra_files.end(); ++f.first, ++f.second) {

     string ms1_spectra_file = (f.first)->SpectrumRecords;
     string ms2_spectra_file = (f.second)->SpectrumRecords;
     string origin_file = (f.second)->OriginalName;

     // load MS1 and MS2 spectra
     map<int, boost::tuple<double*, double*, double*, int>> ms1scan_mz_intensity_rank_map;
     map<int, boost::tuple<double, double>> ms1scan_slope_intercept_map;
     loadMS1Spectra(ms1_spectra_file, &ms1scan_mz_intensity_rank_map, &ms1scan_slope_intercept_map);
     carp(CARP_DEBUG, "new max_ms1scan:%d \t scan_gap:%d \t avg_noise_intensity_logrank:%f", max_ms1scan_, scan_gap_, avg_noise_intensity_logrank_);

     SpectrumCollection* spectra = loadSpectra(ms2_spectra_file);

     // insert the search code here and will split into a new function later
     double highest_ms2_mz = spectra->FindHighestMZ();
     MaxBin::SetGlobalMax(highest_ms2_mz);
     resetMods();
     carp(CARP_DEBUG, "Maximum observed MS2 m/z:%f", highest_ms2_mz);

     // Active queue to process the indexed peptides
     ActivePeptideQueue* active_peptide_queue = new ActivePeptideQueue(peptide_reader->Reader(), proteins);
     active_peptide_queue->setElutionWindow(0);
     active_peptide_queue->setPeptideCentric(false);
     active_peptide_queue->SetBinSize(bin_width_, bin_offset_);
     active_peptide_queue->SetOutputs(NULL, &locations, Params::GetInt("top-match"), true, output_file, NULL, highest_ms2_mz);

     // Some setup adoped from TideSearch
     const vector<SpectrumCollection::SpecCharge>* spec_charges = spectra->SpecCharges();
     int sc_index = -1;
     FLOAT_T sc_total = (FLOAT_T)spec_charges->size();
     int print_interval = Params::GetInt("print-search-progress");
     // Keep track of observed peaks that get filtered out in various ways.
     long int num_range_skipped = 0;
     long int num_precursors_skipped = 0;
     long int num_isotopes_skipped = 0;
     long int num_retained = 0;

     // Infer the isolation window size, which will be used if windowWideness is not provided in the input file
     double current_mz = 0; avg_isowin_width_ = 0;
     vector<double> precursor_mz_vec, precursor_gap_vec;
     for (vector<SpectrumCollection::SpecCharge>::const_iterator sc_chunk = spec_charges->begin();sc_chunk < spec_charges->begin() + (spec_charges->size()); sc_chunk++) {
         double precursor_mz = sc_chunk->spectrum->PrecursorMZ();
         if (precursor_mz > current_mz) {
             precursor_mz_vec.push_back(precursor_mz);
             current_mz = precursor_mz;
         }
         else if (precursor_mz < current_mz) { break; }
     }
     for (int precursor_mz_idx = 1; precursor_mz_idx < precursor_mz_vec.size(); ++precursor_mz_idx) { precursor_gap_vec.push_back(precursor_mz_vec.at(precursor_mz_idx) - precursor_mz_vec.at(precursor_mz_idx-1)); }
     if (precursor_gap_vec.size() > 0) { avg_isowin_width_ = MathUtil::Mean(precursor_gap_vec); }

     // This is the main search loop.
     ObservedPeakSet observed(bin_width_, bin_offset_, Params::GetBool("use-neutral-loss-peaks"), Params::GetBool("use-flanking-peaks") );

     // Note: We don't traverse the collection of SpecCharge, which is sorted by neutral mass and if the neutral mass is equal, sort by the MS2 scan.
     // Notice that in the DIA setting, each different neutral mass correspond to a (scan-win, charge) pair.
     // Therefore, we divide the collection of SpecCharge into different chunks, each of which contains spectra
     // corresponding to the same (scan-win, charge) pair. Within each chunk, the spectra should be sort by the MS2 scan.
     // The motivation here is to build per chunk (i.e. scan-win) map to extract chromatogram for precursor-fragment coelution.

     vector<SpectrumCollection::SpecCharge> spec_charge_chunk;
     int curr_precursor_mz = 0;

     for (vector<SpectrumCollection::SpecCharge>::const_iterator sc_chunk = spec_charges->begin();sc_chunk < spec_charges->begin() + (spec_charges->size()); sc_chunk++) {
      ++sc_index;
      if (print_interval > 0 && sc_index > 0 && sc_index % print_interval == 0) { carp(CARP_INFO, "%d spectrum-charge combinations searched, %.0f%% complete", sc_index, sc_index / sc_total * 100); }

      Spectrum* spectrum_chunk = sc_chunk->spectrum;
      int precursor_mz_chunk = int(spectrum_chunk->PrecursorMZ());

      // deal with a chunk if it's either the end of the same mz or it's the last element
      if (((precursor_mz_chunk != curr_precursor_mz) || (sc_chunk == (spec_charges->begin() + spec_charges->size()-1))) && (spec_charge_chunk.size() > 0) ) {

        // cache the MS2 peaks specific to the current isolation window
        map<int, boost::tuple<double*, double*, int>> ms2scan_mz_intensity_map;
        buildSpectraIndexFromIsoWindow(&spec_charge_chunk, &ms2scan_mz_intensity_map);

        // the TTOF-specific denoising should occur in the for loop below
        for (int chunk_idx = 0; chunk_idx < spec_charge_chunk.size(); ++chunk_idx)
        {
        	Spectrum* spectrum = spec_charge_chunk.at(chunk_idx).spectrum;
        	int charge = spec_charge_chunk.at(chunk_idx).charge;

          double precursor_mz = spectrum->PrecursorMZ();
          int scan_num = spectrum->SpectrumNumber();
          int ms1_scan_num = spectrum->MS1SpectrumNum();

          //denoising-related
          if (Params::GetBool("spectra-denoising")) {
          	int neighbor_cnt = 0;
          	vector<double> proceed_mzs, succeed_mzs;
          	if (chunk_idx > 0) {
          		++neighbor_cnt;
          		int neighbor_chunk_idx = chunk_idx - 1;
          		Spectrum* neighbor_spectrum = spec_charge_chunk.at(neighbor_chunk_idx).spectrum;
          		for (int neighbor_peak_idx=0; neighbor_peak_idx<neighbor_spectrum->Size(); ++neighbor_peak_idx) {
          			proceed_mzs.push_back(neighbor_spectrum->M_Z(neighbor_peak_idx));
          		}
          		std::sort(proceed_mzs.begin(), proceed_mzs.end());
          	}

          	if (chunk_idx < (spec_charge_chunk.size()-1)) {
          		++neighbor_cnt;
          		int neighbor_chunk_idx = chunk_idx + 1;
          		Spectrum* neighbor_spectrum = spec_charge_chunk.at(neighbor_chunk_idx).spectrum;
          		for (int neighbor_peak_idx=0; neighbor_peak_idx<neighbor_spectrum->Size(); ++neighbor_peak_idx) {
          		  succeed_mzs.push_back(neighbor_spectrum->M_Z(neighbor_peak_idx));
          		}
          		std::sort(succeed_mzs.begin(), succeed_mzs.end());
          	}

          	vector<bool> peak_supported;
          	for (int peak_idx=0; peak_idx<spectrum->Size(); ++peak_idx) {
          		double peak_mz = spectrum->M_Z(peak_idx);

          	  int supported_cnt = 0;
          	  int proceed_mz_idx = MathUtil::binarySearch(&proceed_mzs, peak_mz);
          	  if (proceed_mz_idx >= 0) {
          	    double matched_mz = proceed_mzs.at(proceed_mz_idx);
          	    double ppm = fabs(peak_mz - matched_mz) * 1000000 / max(peak_mz, matched_mz);
          	    if (ppm <= Params::GetInt("frag-ppm")) { ++supported_cnt; }
          	  }

          	  int succeed_mz_idx = MathUtil::binarySearch(&succeed_mzs, peak_mz);
          	  if (succeed_mz_idx >= 0) {
          	    double matched_mz = succeed_mzs.at(succeed_mz_idx);
          	    double ppm = fabs(peak_mz - matched_mz) * 1000000 / max(peak_mz, matched_mz);
          	    if (ppm <= Params::GetInt("frag-ppm")) { ++supported_cnt; }
          	  }

          	  if (supported_cnt >= neighbor_cnt) { peak_supported.push_back(true); }
          	  else { peak_supported.push_back(false); }
          	}
          	spectrum->UpdatePeakSupport(&peak_supported);
          }

          // The active peptide queue holds the candidate peptides for spectrum.
          // Calculate and set the window, depending on the window type.
          vector<double>* min_mass = new vector<double>();
          vector<double>* max_mass = new vector<double>();
          vector<bool>* candidatePeptideStatus = new vector<bool>();
          double min_range, max_range;

          carp(CARP_DETAILED_DEBUG, "MS1Scan:%d \t MS2Scan:%d \t precursor_mz:%f \t charge:%d", ms1_scan_num, scan_num, precursor_mz, charge);
          computeWindowDIA(spec_charge_chunk.at(chunk_idx), &negative_isotope_errors, min_mass, max_mass, &min_range, &max_range);

          // Normalize the observed spectrum and compute the cache of frequently-needed
          // values for taking dot products with theoretical spectra.
          // TODO: Note that here each specturm might be preprocessed multiple times, one for each charge, potentially can be improved!
          observed.PreprocessSpectrum(*spectrum, charge, &num_range_skipped, &num_precursors_skipped, &num_isotopes_skipped, &num_retained);
          int nCandPeptide = active_peptide_queue->SetActiveRange(min_mass, max_mass, min_range, max_range, candidatePeptideStatus);
          int candidatePeptideStatusSize = candidatePeptideStatus->size();
          if (nCandPeptide == 0) { continue; }

          TideMatchSet::Arr2 match_arr2(candidatePeptideStatusSize); // Scored peptides will go here.
          // Programs for taking the dot-product with the observed spectrum are laid
          // out in memory managed by the active_peptide_queue, one program for each
          // candidate peptide. The programs will store the results directly into
          // match_arr. We now pass control to those programs.
          TideSearchApplication::collectScoresCompiled(active_peptide_queue, spectrum, observed, &match_arr2, candidatePeptideStatusSize, charge);

          // The denominator used in the Tailor score calibration method
          double quantile_score = getTailorQuantile(&match_arr2);

          TideMatchSet::Arr match_arr(nCandPeptide);
          for (TideMatchSet::Arr2::iterator it = match_arr2.begin(); it != match_arr2.end(); ++it) {
             /// The code below which is adopted from Tide-search
             int peptide_idx = candidatePeptideStatusSize - (it->second);
             if ((*candidatePeptideStatus)[peptide_idx]) {
              TideMatchSet::Scores curScore;
               curScore.xcorr_score = (double)(it->first / XCORR_SCALING);
               curScore.rank = it->second;
               curScore.tailor = ((double)(it->first / XCORR_SCALING) + 5.0) / quantile_score;
               match_arr.push_back(curScore);
             }
          }

          TideMatchSet matches(&match_arr, highest_ms2_mz);
          if (!match_arr.empty()) {
            reportDIA(output_file, origin_file, spec_charge_chunk.at(chunk_idx), active_peptide_queue, proteins, locations,
                &matches,
                &observed,
                &ms1scan_mz_intensity_rank_map,
                &ms1scan_slope_intercept_map,
                &ms2scan_mz_intensity_map,
                &peptide_predrt_map);
          }

          delete min_mass;
          delete max_mass;
          delete candidatePeptideStatus;
        }

        // clear up for next chunk
        for (map<int, boost::tuple<double*, double*, int>>::const_iterator i = ms2scan_mz_intensity_map.begin(); i != ms2scan_mz_intensity_map.end(); i++) { delete[] (i->second).get<0>(); delete[] (i->second).get<1>(); }
        ms2scan_mz_intensity_map.clear();

        spec_charge_chunk.clear();
      }

      curr_precursor_mz = precursor_mz_chunk;
      spec_charge_chunk.push_back(*sc_chunk);
     }

     // clean up
     delete spectra;

     for (map<int, boost::tuple<double*, double*, double*, int>>::const_iterator i = ms1scan_mz_intensity_rank_map.begin(); i != ms1scan_mz_intensity_rank_map.end(); i++) {
       delete[] (i->second).get<0>();
       delete[] (i->second).get<1>();
       delete[] (i->second).get<2>();
     }
     ms1scan_mz_intensity_rank_map.clear();
     ms1scan_slope_intercept_map.clear();

     delete active_peptide_queue;
    }

    // clean up
    if (output_file) { output_file->close(); delete output_file; }
  }
  delete peptide_reader;

  // standardize the features
  if (!FileUtils::Exists(output_file_name_scaled_) /*|| Params::GetBool("overwrite")*/ ) {
    DIAmeterFeatureScaler diameterScaler(output_file_name_unsorted_.c_str());
    diameterScaler.calcDataQuantile();
    diameterScaler.writeScaledFile(output_file_name_scaled_.c_str());
  }

  // filter the edges
  if (!FileUtils::Exists(output_file_name_filtered_) /*|| Params::GetBool("overwrite")*/ ) {
    DIAmeterPSMFilter diameterFilter(output_file_name_scaled_.c_str());
    diameterFilter.loadAndFilter(output_file_name_filtered_.c_str(), Params::GetBool("psm-filter") );
  }

  /*
  // filter the edges by using cross-validation
  if (!FileUtils::Exists(output_file_name_cv_) || Params::GetBool("overwrite") ) {
	  DIAmeterCVSelector diameterCVSelector(output_file_name_scaled_.c_str());
	  diameterCVSelector.loadData(output_file_name_cv_.c_str() );

	  double params[] = {0.2,0.4,0.8,1.6,3.2,6.4,12.8,25.6};
	  vector<double> paramRangeList (params, params + sizeof(params) / sizeof(params[0]));
	  diameterCVSelector.FoldFilter(output_file_name_cv_.c_str(), &paramRangeList);
  }
  */

  // generate .pin file by calling make-pin
  MakePinApplication pinApp;
  vector<string> paths_vec;
  paths_vec.push_back(output_file_name_filtered_);
  if (pinApp.main(paths_vec) != 0) { carp(CARP_FATAL, "Make-pin failed internally in DIAmeter."); }

  // calling percolator
  PercolatorApplication percolatorApp;
  if(!FileUtils::Exists(output_percolator_)) { FileUtils::Mkdir(output_percolator_); }
  if (percolatorApp.main(make_file_path(output_pin_), output_percolator_) != 0) { carp(CARP_FATAL, "Percolator failed internally in DIAmeter."); }

  /*
  FileUtils::Remove(output_file_name_filtered_);
  FileUtils::Remove(make_file_path(output_pin_));
  FileUtils::Remove(FileUtils::Join(output_percolator_, "percolator.decoy.peptides.txt"));
  FileUtils::Remove(FileUtils::Join(output_percolator_, "percolator.decoy.psms.txt"));
  FileUtils::Remove(FileUtils::Join(output_percolator_, "percolator.target.psms.txt"));
  */

  return 0;
}

void DIAmeterApplication::reportDIA(
   ofstream* output_file,  // output file to write to
   const string& spectrum_filename, // name of spectrum file
   const SpectrumCollection::SpecCharge& sc, // spectrum and charge for matches
   const ActivePeptideQueue* peptides, // peptide queue
   const ProteinVec& proteins, // proteins corresponding with peptides
   const vector<const pb::AuxLocation*>& locations,  // auxiliary locations
   TideMatchSet* matches, // object to manage PSMs
   ObservedPeakSet* observed,
   map<int, boost::tuple<double*, double*, double*, int>>* ms1scan_mz_intensity_rank_map,
   map<int, boost::tuple<double, double>>* ms1scan_slope_intercept_map,
   map<int, boost::tuple<double*, double*, int>>* ms2scan_mz_intensity_map,
   map<string, double>* peptide_predrt_map
) {
   Spectrum* spectrum = sc.spectrum;
   int charge = sc.charge;
   int ms1_scan_num = spectrum->MS1SpectrumNum();
   int ms2_scan_num = spectrum->SpectrumNumber();

   // get top-n targets and decoys by the heap
   vector<TideMatchSet::Arr::iterator> targets, decoys;
   matches->gatherTargetsAndDecoys(peptides, proteins, targets, decoys, Params::GetInt("top-match"), 1, true);

   // calculate precursor intensity logrank (ppm-based)
   int peak_num_new = -1; double *mz_arr_new = NULL, *intensity_arr_new = NULL, *intensity_rank_arr_new = NULL;
   map<int, boost::tuple<double*, double*, double*, int>>::iterator intensityIter_new = ms1scan_mz_intensity_rank_map->find(ms1_scan_num);
   if (intensityIter_new == ms1scan_mz_intensity_rank_map->end()) { carp(CARP_DETAILED_DEBUG, "No intensity found in MS1 scan:%d !!!", ms1_scan_num); }
   else {
     mz_arr_new = (intensityIter_new->second).get<0>();
     intensity_arr_new = (intensityIter_new->second).get<1>();
     intensity_rank_arr_new = (intensityIter_new->second).get<2>();
     peak_num_new = (intensityIter_new->second).get<3>();
   }

   double slope_new = 0, intercept_new = avg_ms1_intercept_;
   map<int, boost::tuple<double, double>>::iterator rankIter_new = ms1scan_slope_intercept_map->find(ms1_scan_num);
   if (rankIter_new == ms1scan_slope_intercept_map->end()) { carp(CARP_DETAILED_DEBUG, "No slope and intercept found in MS1 scan:%d !!!", ms1_scan_num); }
   else {
     slope_new = (rankIter_new->second).get<0>();
     intercept_new = (rankIter_new->second).get<1>();
   }
   boost::tuple<double, double> slope_intercept_tp = boost::make_tuple(slope_new, intercept_new);

   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>> intensity_map;
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>> logrank_map;
   computePrecIntRank(targets, peptides, mz_arr_new, intensity_arr_new, intensity_rank_arr_new, slope_intercept_tp, peak_num_new, &intensity_map, &logrank_map, charge);
   computePrecIntRank(decoys, peptides, mz_arr_new, intensity_arr_new, intensity_rank_arr_new, slope_intercept_tp, peak_num_new, &intensity_map, &logrank_map, charge);

   // calculate precursor fragment co-elution
   carp(CARP_DETAILED_DEBUG, "scan_gap:%d \t coelution-oneside-scans:%d", scan_gap_, Params::GetInt("coelution-oneside-scans") );
   // extract the MS1 and MS2 scan numbers which constitute the local chromatogram
   vector<int> valid_ms1scans, valid_ms2scans;
   for (int offset=-Params::GetInt("coelution-oneside-scans"); offset<=Params::GetInt("coelution-oneside-scans"); ++offset) {
     int candidate_ms1scan = ms1_scan_num + offset*scan_gap_;
     int candidate_ms2scan = ms2_scan_num + offset*scan_gap_;
     if (candidate_ms1scan < 1 || candidate_ms1scan > max_ms1scan_) { continue; }

     valid_ms1scans.push_back(candidate_ms1scan);
     valid_ms2scans.push_back(candidate_ms2scan);
   }

   // Loop through each corresponding ms1scan and ms2scan pair (ppm-based)
   vector<boost::tuple<double*, double*, int, double*, double*, int>> mz_intensity_arrs_vector;
   for (pair<vector<int>::const_iterator, vector<int>::const_iterator> f(valid_ms1scans.begin(), valid_ms2scans.begin());
     f.first != valid_ms1scans.end() && f.second != valid_ms2scans.end(); ++f.first, ++f.second) {

     int curr_ms1scan = *(f.first);
     int curr_ms2scan = *(f.second);

     int ms1_peak_num = -1; double *ms1_mz_arr = NULL, *ms1_intensity_arr = NULL;
     int ms2_peak_num = -1; double *ms2_mz_arr = NULL, *ms2_intensity_arr = NULL;

     map<int, boost::tuple<double*, double*, double*, int>>::iterator ms1_intensityIter = ms1scan_mz_intensity_rank_map->find(curr_ms1scan);
     if (ms1_intensityIter == ms1scan_mz_intensity_rank_map->end()) { carp(CARP_DETAILED_DEBUG, "No intensity found in MS1 scan:%d !!!", curr_ms1scan); }
     else {
       ms1_mz_arr = (ms1_intensityIter->second).get<0>();
       ms1_intensity_arr = (ms1_intensityIter->second).get<1>();
       ms1_peak_num = (ms1_intensityIter->second).get<3>();
     }

     map<int, boost::tuple<double*, double*, int>>::iterator ms2_intensityIter = ms2scan_mz_intensity_map->find(curr_ms2scan);
     if (ms2_intensityIter == ms2scan_mz_intensity_map->end()) { carp(CARP_DETAILED_DEBUG, "No intensity found in MS2 scan:%d !!!", curr_ms2scan); }
     else {
        ms2_mz_arr = (ms2_intensityIter->second).get<0>();
        ms2_intensity_arr = (ms2_intensityIter->second).get<1>();
        ms2_peak_num = (ms2_intensityIter->second).get<2>();
     }

     if (ms1_intensity_arr != NULL && ms2_intensity_arr != NULL) {
       mz_intensity_arrs_vector.push_back(boost::make_tuple(ms1_mz_arr, ms1_intensity_arr, ms1_peak_num, ms2_mz_arr, ms2_intensity_arr, ms2_peak_num));
     }
   }
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>> coelute_map;
   computePrecFragCoelute(targets, peptides, &mz_intensity_arrs_vector, &coelute_map, charge);
   computePrecFragCoelute(decoys, peptides, &mz_intensity_arrs_vector, &coelute_map, charge);

   // calculate MS2 p-value
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double>> ms2pval_map;
   computeMS2Pval(targets, peptides, observed, &ms2pval_map, true);
   computeMS2Pval(decoys, peptides, observed, &ms2pval_map, true);

   // calculate delta_cn and delta_lcn
   map<TideMatchSet::Arr::iterator, FLOAT_T> delta_cn_map;
   map<TideMatchSet::Arr::iterator, FLOAT_T> delta_lcn_map;
   TideMatchSet::computeDeltaCns(targets, &delta_cn_map, &delta_lcn_map);
   TideMatchSet::computeDeltaCns(decoys, &delta_cn_map, &delta_lcn_map);

   // calculate SpScore if necessary
   map<TideMatchSet::Arr::iterator, pair<const SpScorer::SpScoreData, int> > sp_map;
   if (Params::GetBool("compute-sp")) {
    SpScorer sp_scorer(proteins, *spectrum, charge, matches->max_mz_);
    TideMatchSet::computeSpData(targets, &sp_map, &sp_scorer, peptides);
    TideMatchSet::computeSpData(decoys, &sp_map, &sp_scorer, peptides);
   }

   matches->writeToFileDIA(output_file,
       Params::GetInt("top-match"),
       targets,
       spectrum_filename,
       spectrum,
       charge,
       peptides,
       proteins,
       locations,
       &delta_cn_map,
       &delta_lcn_map,
       Params::GetBool("compute-sp")? &sp_map : NULL,
       &intensity_map,
       &logrank_map,
       &coelute_map,
       &ms2pval_map,
       peptide_predrt_map);

   matches->writeToFileDIA(output_file,
       Params::GetInt("top-match"),
       decoys,
       spectrum_filename,
       spectrum,
       charge,
       peptides,
       proteins,
       locations,
       &delta_cn_map,
       &delta_lcn_map,
       Params::GetBool("compute-sp")? &sp_map : NULL,
       &intensity_map,
       &logrank_map,
       &coelute_map,
       &ms2pval_map,
       peptide_predrt_map);

}

void DIAmeterApplication::computePrecFragCoelute(
  const vector<TideMatchSet::Arr::iterator>& vec,
  const ActivePeptideQueue* peptides,
  vector<boost::tuple<double*, double*, int, double*, double*, int>>* mz_intensity_arrs_vector,
  map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>>* coelute_map,
  int charge
) {
  int coelute_size = mz_intensity_arrs_vector->size();
  vector<double> ms1_corrs, ms2_corrs, ms1_ms2_corrs;

  for (vector<TideMatchSet::Arr::iterator>::const_iterator i = vec.begin(); i != vec.end(); ++i) {
     Peptide& peptide = *(peptides->GetPeptide((*i)->rank));
     // Precursor signals
     double peptide_mz_m0 = Peptide::MassToMz(peptide.Mass(), charge);
     // Fragment signals
     vector<double> ion_mzs = peptide.IonMzs();
     // Precursor and fragment chromatograms
     vector<double*> ms1_chroms, ms2_chroms;

     // build Precursor chromatograms
     for (int prec_offset=0; prec_offset<3; ++prec_offset ) {
       double prec_mz = peptide_mz_m0 + 1.0*prec_offset/(charge * 1.0);

       double* intensity_arr = new double[coelute_size];
       fill_n(intensity_arr, coelute_size, 0);

       for (int coelute_idx=0; coelute_idx<coelute_size; ++coelute_idx ) {
         boost::tuple<double*, double*, int, double*, double*, int> mz_intensity_arrs = mz_intensity_arrs_vector->at(coelute_idx);
         double* ms1_mz_arr = mz_intensity_arrs.get<0>();
         double* ms1_intensity_arr = mz_intensity_arrs.get<1>();
         int ms1_peak_num = mz_intensity_arrs.get<2>();

         intensity_arr[coelute_idx] = closestPPMValue(ms1_mz_arr, ms1_intensity_arr, ms1_peak_num, prec_mz, Params::GetInt("prec-ppm"), 0, true);
       }
       ms1_chroms.push_back(intensity_arr);
     }

     // build Fragment chromatograms
     for (int frag_offset=0; frag_offset<ion_mzs.size(); ++frag_offset ) {
       double frag_mz = ion_mzs.at(frag_offset);

       double* intensity_arr = new double[coelute_size];
       fill_n(intensity_arr, coelute_size, 0);

       for (int coelute_idx=0; coelute_idx<coelute_size; ++coelute_idx ) {
         boost::tuple<double*, double*, int, double*, double*, int> mz_intensity_arrs = mz_intensity_arrs_vector->at(coelute_idx);
         double* ms2_mz_arr = mz_intensity_arrs.get<3>();
         double* ms2_intensity_arr = mz_intensity_arrs.get<4>();
         int ms2_peak_num = mz_intensity_arrs.get<5>();

         intensity_arr[coelute_idx] = closestPPMValue(ms2_mz_arr, ms2_intensity_arr, ms2_peak_num, frag_mz, Params::GetInt("frag-ppm"), 0, true);
       }
       ms2_chroms.push_back(intensity_arr);
     }

     // calculate correlation among MS1
     ms1_corrs.clear();
     for (int i=0; i<ms1_chroms.size(); ++i) {
       for (int j=i+1; j<ms1_chroms.size(); ++j) {
         ms1_corrs.push_back(MathUtil::NormalizedDotProduct(ms1_chroms.at(i), ms1_chroms.at(j), coelute_size));
       }
     }
     sort(ms1_corrs.begin(), ms1_corrs.end(), greater<double>());

     // calculate correlation among MS2
     ms2_corrs.clear();
     for (int i=0; i<ms2_chroms.size(); ++i) {
       for (int j=i+1; j<ms2_chroms.size(); ++j) {
         ms2_corrs.push_back(MathUtil::NormalizedDotProduct(ms2_chroms.at(i), ms2_chroms.at(j), coelute_size));
       }
     }
     sort(ms2_corrs.begin(), ms2_corrs.end(), greater<double>());

     // calculate correlation among MS1 and MS2
     ms1_ms2_corrs.clear();
     for (int i=0; i<1; ++i) {
       for (int j=0; j<ms2_chroms.size(); ++j) {
         ms1_ms2_corrs.push_back(MathUtil::NormalizedDotProduct(ms1_chroms.at(i), ms2_chroms.at(j), coelute_size));
       }
     }
     sort(ms1_ms2_corrs.begin(), ms1_ms2_corrs.end(), greater<double>());

     double ms1_mean=0, ms2_mean=0, ms1_ms2_mean=0;
     if (ms1_corrs.size() > 0) { ms1_corrs.resize(Params::GetInt("coelution-topk")); ms1_mean = std::accumulate(ms1_corrs.begin(), ms1_corrs.end(), 0.0) / ms1_corrs.size(); }
     if (ms2_corrs.size() > 0) { ms2_corrs.resize(Params::GetInt("coelution-topk")); ms2_mean = std::accumulate(ms2_corrs.begin(), ms2_corrs.end(), 0.0) / ms2_corrs.size(); }
     if (ms1_ms2_corrs.size() > 0) { ms1_ms2_corrs.resize(Params::GetInt("coelution-topk")); ms1_ms2_mean = std::accumulate(ms1_ms2_corrs.begin(), ms1_ms2_corrs.end(), 0.0) / ms1_ms2_corrs.size(); }
     coelute_map->insert(make_pair((*i), boost::make_tuple(ms1_mean, ms2_mean, ms1_ms2_mean)));

     // clean up
     for (int prec_offset=0; prec_offset<ms1_chroms.size(); ++prec_offset ) { delete[] ms1_chroms.at(prec_offset); }
     for (int frag_offset=0; frag_offset<ms2_chroms.size(); ++frag_offset ) { delete[] ms2_chroms.at(frag_offset); }
     ms1_chroms.clear();
     ms2_chroms.clear();
   }
}

void DIAmeterApplication::computeMS2Pval(
   const vector<TideMatchSet::Arr::iterator>& vec,
   const ActivePeptideQueue* peptides,
   ObservedPeakSet* observed,
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double>>* ms2pval_map,
   bool dynamic_filter
) {
   int smallest_mzbin = observed->SmallestMzbin();
   int largest_mzbin = observed->LargestMzbin();

   vector<pair<int, double>> filtered_peak_tuples = observed->StaticFilteredPeakTuples();
   if (dynamic_filter) { filtered_peak_tuples = observed->DynamicFilteredPeakTuples(); }

   double ms2_coverage = 1.0 * filtered_peak_tuples.size() / (largest_mzbin - smallest_mzbin + 1);
   double log_p = log(ms2_coverage);
   double log_1_min_p = log(1 - ms2_coverage);

   carp(CARP_DETAILED_DEBUG, "Mzbin range:[%d, %d] \t ms2_coverage: %f ", smallest_mzbin, largest_mzbin, ms2_coverage );
   // a sanity check if filtered_peak_tuples is sorted ascendingly w.r.t mzbin
   vector<int> filtered_peak_mzbins;
   vector<double> filtered_peak_intensities;
   for (int idx=0; idx<filtered_peak_tuples.size(); ++idx) {
     filtered_peak_mzbins.push_back(filtered_peak_tuples.at(idx).first);
     filtered_peak_intensities.push_back(filtered_peak_tuples.at(idx).second);
   }

   vector<int> intersect_mzbins;
   vector<double> pvalue_binomial_probs;

   for (vector<TideMatchSet::Arr::iterator>::const_iterator i = vec.begin(); i != vec.end(); ++i) {
    Peptide& peptide = *(peptides->GetPeptide((*i)->rank));
    vector<int> ion_mzbins = peptide.IonMzbins();

    intersect_mzbins.clear();
    // sort(ion_mzbins.begin(), ion_mzbins.end()); // sort the vector for calculating the intersection lateron

    set_intersection(filtered_peak_mzbins.begin(),filtered_peak_mzbins.end(), ion_mzbins.begin(), ion_mzbins.end(), back_inserter(intersect_mzbins));

    pvalue_binomial_probs.clear();
    for (int k=intersect_mzbins.size(); k <= ion_mzbins.size(); ++k ) {
      double binomial_prob = MathUtil::LogNChooseK(ion_mzbins.size(), k) + k * log_p + (ion_mzbins.size()-k) * log_1_min_p;
      pvalue_binomial_probs.push_back(binomial_prob);
    }
    double ms2pval1 = -MathUtil::LogSumExp(&pvalue_binomial_probs);
    if (std::isnan(ms2pval1) || std::isinf(ms2pval1)) { ms2pval1 = 0; }

    double ms2pval2 = 0.0, intensitysum = 0.0;

    // deal with another alternative
    vector<int> b_ion_mzbins = peptide.BIonMzbins();
    vector<int> y_ion_mzbins = peptide.YIonMzbins();

    intersect_mzbins.clear(); intensitysum = 0.0;
    set_intersection(filtered_peak_mzbins.begin(),filtered_peak_mzbins.end(), b_ion_mzbins.begin(), b_ion_mzbins.end(), back_inserter(intersect_mzbins));
    ms2pval2 += MathUtil::gammaln(1.0 + intersect_mzbins.size());
    for (int k=0; k <intersect_mzbins.size(); ++k ) {
      std::vector<int>::iterator itr = find(filtered_peak_mzbins.begin(), filtered_peak_mzbins.end(), intersect_mzbins.at(k));
      if (itr != filtered_peak_mzbins.cend()) {
        int hit_idx = distance(filtered_peak_mzbins.begin(), itr);
        intensitysum += (filtered_peak_intensities.at(hit_idx)*filtered_peak_intensities.at(hit_idx));
      }
    }
    ms2pval2 += log(1.0 + intensitysum);

    intersect_mzbins.clear(); intensitysum = 0.0;
    set_intersection(filtered_peak_mzbins.begin(),filtered_peak_mzbins.end(), y_ion_mzbins.begin(), y_ion_mzbins.end(), back_inserter(intersect_mzbins));
    ms2pval2 += MathUtil::gammaln(1.0 + intersect_mzbins.size());
    for (int k=0; k <intersect_mzbins.size(); ++k ) {
      std::vector<int>::iterator itr = find(filtered_peak_mzbins.begin(), filtered_peak_mzbins.end(), intersect_mzbins.at(k));
      if (itr != filtered_peak_mzbins.cend()) {
        int hit_idx = distance(filtered_peak_mzbins.begin(), itr);
        intensitysum += (filtered_peak_intensities.at(hit_idx)*filtered_peak_intensities.at(hit_idx));
      }
    }
    ms2pval2 += log(1.0 + intensitysum);
    ms2pval_map->insert(make_pair((*i), boost::make_tuple(ms2pval1, ms2pval2 )));
   }
}

void DIAmeterApplication::computePrecIntRank(
   const vector<TideMatchSet::Arr::iterator>& vec,
   const ActivePeptideQueue* peptides,
   const double* mz_arr,
   const double* intensity_arr,
   const double* intensity_rank_arr,
   boost::tuple<double, double> slope_intercept_tp,
   int peak_num,
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>>* intensity_map,
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>>* logrank_map,
   int charge
) {
  double noise_intensity_rank = avg_noise_intensity_logrank_;
  if (peak_num > 0) { noise_intensity_rank = log(1.0+peak_num); }
  double slope = slope_intercept_tp.get<0>();
  double intercept = slope_intercept_tp.get<1>();

  for (vector<TideMatchSet::Arr::iterator>::const_iterator i = vec.begin(); i != vec.end(); ++i) {
     Peptide& peptide = *(peptides->GetPeptide((*i)->rank));
     double peptide_mz_m0 = Peptide::MassToMz(peptide.Mass(), charge);

     double intensity_rank_m0 = closestPPMValue(mz_arr, intensity_rank_arr, peak_num, peptide_mz_m0, Params::GetInt("prec-ppm"), noise_intensity_rank, false);
     double intensity_rank_m1 = closestPPMValue(mz_arr, intensity_rank_arr, peak_num, peptide_mz_m0 + 1.0/(charge * 1.0), Params::GetInt("prec-ppm"), noise_intensity_rank, false);
     double intensity_rank_m2 = closestPPMValue(mz_arr, intensity_rank_arr, peak_num, peptide_mz_m0 + 2.0/(charge * 1.0), Params::GetInt("prec-ppm"), noise_intensity_rank, false);

     double intensity_m0 = closestPPMValue(mz_arr, intensity_arr, peak_num, peptide_mz_m0, Params::GetInt("prec-ppm"), 0, false);
     double intensity_m1 = closestPPMValue(mz_arr, intensity_arr, peak_num, peptide_mz_m0 + 1.0/(charge * 1.0), Params::GetInt("prec-ppm"), 0, false);
     double intensity_m2 = closestPPMValue(mz_arr, intensity_arr, peak_num, peptide_mz_m0 + 2.0/(charge * 1.0), Params::GetInt("prec-ppm"), 0, false);

     intensity_map->insert(make_pair((*i), boost::make_tuple(intensity_rank_m0, intensity_rank_m1, intensity_rank_m2)));
     logrank_map->insert(make_pair((*i), boost::make_tuple(slope*log(1.0+intensity_m0)+intercept, slope*log(1.0+intensity_m1)+intercept, slope*log(1.0+intensity_m2)+intercept)));
  }
}

vector<InputFile> DIAmeterApplication::getInputFiles(const vector<string>& filepaths, int ms_level) const {
   vector<InputFile> input_sr;

   if (Params::GetString("spectrum-parser") != "pwiz") { carp(CARP_FATAL, "spectrum-parser must be pwiz instead of %s", Params::GetString("spectrum-parser").c_str() ); }

   for (vector<string>::const_iterator f = filepaths.begin(); f != filepaths.end(); f++) {
    string spectrum_input_url = *f;
    string spectrumrecords_url = make_file_path(FileUtils::BaseName(spectrum_input_url) + ".spectrumrecords.ms" + to_string(ms_level));
    carp(CARP_INFO, "Converting %s to spectrumrecords %s", spectrum_input_url.c_str(), spectrumrecords_url.c_str());
    carp(CARP_DEBUG, "New MS%d spectrumrecords filename: %s", ms_level, spectrumrecords_url.c_str());

    if (!FileUtils::Exists(spectrumrecords_url)) {
     if (!SpectrumRecordWriter::convert(spectrum_input_url, spectrumrecords_url, ms_level, true)) {
      carp(CARP_FATAL, "Error converting MS2 spectrumrecords from %s", spectrumrecords_url.c_str());
     }
    }
    input_sr.push_back(InputFile(*f, spectrumrecords_url, true));
  }

  return input_sr;
}

void DIAmeterApplication::getPeptidePredRTMapping(map<string, double>* peptide_predrt_map, int percent_bins) {
   carp(CARP_INFO, "predrt-files: %s ", Params::GetString("predrt-files").c_str());

   map<string, double> tmp_map;
   vector<double> predrt_vec;

   // it's possible that multiple mapping files are provided and concatenated by comma
   vector<string> mapping_paths = StringUtils::Split(Params::GetString("predrt-files"), ",");
   for(int file_idx = 0; file_idx<mapping_paths.size(); file_idx++) {
    if (!FileUtils::Exists(mapping_paths.at(file_idx))) {
      carp(CARP_DEBUG, "The mapping file %s does not exist! \n", mapping_paths.at(file_idx).c_str());
      continue;
    }
    else { carp(CARP_DEBUG, "parsing the mapping file: %s", mapping_paths.at(file_idx).c_str()); }

    std::ifstream file_stream(mapping_paths.at(file_idx).c_str());
    string next_data_string;
    if (file_stream.is_open()) {
     unsigned int line_cnt = 0;
     while (getline(file_stream, next_data_string)) {
      vector<string> column_values = StringUtils::Split(StringUtils::Trim(next_data_string), "\t");
      if (column_values.size() < 2) { carp(CARP_FATAL, "Each row should contains two columns! (observed %d) \n", column_values.size()); }

      line_cnt++;
      // check if the first row is the header or the real mapping
      if (line_cnt <= 1 && !StringUtils::IsNumeric(column_values.at(1), true, true)) { continue; }

      double predrt = stod(column_values.at(1));
      tmp_map.insert(make_pair(column_values.at(0), predrt));
      predrt_vec.push_back(predrt);
      carp(CARP_DETAILED_DEBUG, "Peptide:%s \t predrt:%f", column_values.at(0).c_str(), predrt );
     }
     file_stream.close();
    }
   }

   if (predrt_vec.size() <= 0) { return; }
   double min_predrt = *min_element(predrt_vec.begin(), predrt_vec.end());
   double max_predrt = *max_element(predrt_vec.begin(), predrt_vec.end());
   carp(CARP_DETAILED_DEBUG, "min_predrt:%f \t max_predrt:%f", min_predrt, max_predrt );

   vector<double> rt_percent_vec = MathUtil::linspace(min_predrt, max_predrt, percent_bins);
   for (map<string, double>::iterator it = tmp_map.begin(); it != tmp_map.end(); it++) {
     double predrt = it->second;
     double predrt2 = 1.0*std::count_if(rt_percent_vec.begin(), rt_percent_vec.end(),[&](int val){ return val <= predrt; })/percent_bins;
     peptide_predrt_map->insert(make_pair(it->first, predrt2 ));
   }
   carp(CARP_DETAILED_DEBUG, "peptide_predrt_map size:%d", peptide_predrt_map->size());
}

void DIAmeterApplication::buildSpectraIndexFromIsoWindow(vector<SpectrumCollection::SpecCharge>* spec_charge_chunk, map<int, boost::tuple<double*, double*, int>>* ms2scan_mz_intensity_map) {
  for (vector<SpectrumCollection::SpecCharge>::const_iterator sc = spec_charge_chunk->begin();sc < spec_charge_chunk->begin() + (spec_charge_chunk->size()); sc++) {
    Spectrum* spectrum = sc->spectrum;
    int scan_num = spectrum->SpectrumNumber();
    int peak_num = spectrum->Size();

    double* mz_arr = new double[peak_num];
    double* intensity_arr = new double[peak_num];

    for (int peak_idx=0; peak_idx<peak_num; ++peak_idx) {
       double peak_mz = spectrum->M_Z(peak_idx);
       double peak_intensity = spectrum->Intensity(peak_idx);

       mz_arr[peak_idx] = peak_mz;

       if (Params::GetBool("spectra-denoising") && !spectrum->Is_supported(peak_idx)) { intensity_arr[peak_idx] = 0; }
       else { intensity_arr[peak_idx] = peak_intensity; }
    }
    (*ms2scan_mz_intensity_map)[scan_num] = boost::make_tuple(mz_arr, intensity_arr, peak_num);
  }
}

void DIAmeterApplication::loadMS1Spectra(const std::string& file,
    map<int, boost::tuple<double*, double*, double*, int>>* ms1scan_mz_intensity_rank_map,
    map<int, boost::tuple<double, double>>* ms1scan_slope_intercept_map
) {
  SpectrumCollection* spectra = loadSpectra(file);

  double accumulated_intensity_logrank = 0.0, accumulated_peaknum = 0.0, accumulated_intercept = 0.0, accumulated_intercept_cnt = 0;
  const vector<SpectrumCollection::SpecCharge>* spec_charges = spectra->SpecCharges();
  vector<int> ms1_scans;

  for (vector<SpectrumCollection::SpecCharge>::const_iterator sc = spec_charges->begin();sc < spec_charges->begin() + (spec_charges->size()); sc++) {
     Spectrum* spectrum = sc->spectrum;
     int ms1_scan_num = spectrum->MS1SpectrumNum();
     int peak_num = spectrum->Size();
     double noise_intensity_logrank = 0;

     ms1_scans.push_back(ms1_scan_num);

     vector<double> sorted_intensity_vec = spectrum->DescendingSortedPeakIntensity();
     double* mz_arr = new double[peak_num];
     double* intensity_arr = new double[peak_num];
     double* intensity_rank_arr = new double[peak_num];

     for (int peak_idx=0; peak_idx<peak_num; ++peak_idx) {
       double peak_mz = spectrum->M_Z(peak_idx);
       double peak_intensity = spectrum->Intensity(peak_idx);
       double peak_intensity_logrank = log(1.0+std::count_if(sorted_intensity_vec.begin(), sorted_intensity_vec.end(),[&](int val){ return val >= peak_intensity; }));

       mz_arr[peak_idx] = peak_mz;
       intensity_arr[peak_idx] = peak_intensity;
       intensity_rank_arr[peak_idx] = peak_intensity_logrank;
       noise_intensity_logrank = max(noise_intensity_logrank, peak_intensity_logrank);
     }

     // fitting the linear regression of log intensity
     int ignore_top = 20; int min_sample_size = 500;
     int retain_cnt = min(min_sample_size, int((peak_num - ignore_top) * 0.2));
     int ignore_bottom = max(0, int(peak_num-retain_cnt-ignore_top));

     if (peak_num >= min_sample_size) {
       vector<double> log_intensity_vec; vector<double> log_rank_vec;
       for (int peak_idx=0; peak_idx<peak_num; ++peak_idx) {
         double log_intensity = log(1.0 + sorted_intensity_vec.at(peak_idx));
         double log_rank = log(1.0 + peak_idx);

         if ((peak_idx >= ignore_top) && (peak_idx < (peak_num - ignore_bottom))) {
           log_intensity_vec.push_back(log_intensity);
           log_rank_vec.push_back(log_rank);
         }
       }

       if (log_intensity_vec.size() > 0) {
         boost::tuple<double, double> slope_intercept_tp = MathUtil::fitLinearRegression(&log_intensity_vec, &log_rank_vec);
         (*ms1scan_slope_intercept_map)[ms1_scan_num] = slope_intercept_tp;
         accumulated_intercept += slope_intercept_tp.get<1>();
         accumulated_intercept_cnt += 1;
       }
     }

     accumulated_intensity_logrank += noise_intensity_logrank;
     (*ms1scan_mz_intensity_rank_map)[ms1_scan_num] = boost::make_tuple(mz_arr, intensity_arr, intensity_rank_arr, peak_num);

     accumulated_peaknum += peak_num;
  }
  delete spectra;

  // calculate the scan gap in cycle
  if (ms1_scans.size() < 2) { carp(CARP_FATAL, "No MS1 scans! \t size:%f", ms1_scans.size()); }
  sort(ms1_scans.begin(), ms1_scans.end());
  scan_gap_ = ms1_scans[1] - ms1_scans[0];
  if (scan_gap_ <= 0) { carp(CARP_FATAL, "Scan gap cannot be non-positive:%d", scan_gap_); }

  // calculate the maximum ms1 scan number
  max_ms1scan_ = ms1_scans[ms1_scans.size()-1];

  // calculate the average noise intensity logrank, which is used as default value when MS1 scan is empty.
  avg_noise_intensity_logrank_ =  accumulated_intensity_logrank / max(1.0, 1.0*spec_charges->size());
  avg_ms1_peaknum_ = accumulated_peaknum / max(1.0, 1.0*spec_charges->size());
  avg_ms1_intercept_ = accumulated_intercept / max(1.0, accumulated_intercept_cnt);

}

SpectrumCollection* DIAmeterApplication::loadSpectra(const std::string& file) {
   SpectrumCollection* spectra = new SpectrumCollection();
   pb::Header spectrum_header;

   if (!spectra->ReadSpectrumRecords(file, &spectrum_header)) {
     carp(CARP_FATAL, "Error reading spectrum file %s", file.c_str());
   }

   // Precursor-window-type must be mz in DIAmeter, based upon which spectra are sorted
   // Precursor-window is the half size of isolation window
   spectra->Sort<ScSortByMzDIA>(ScSortByMzDIA());
   spectra->SetNormalizedObvRTime();
   return spectra;
}

void DIAmeterApplication::computeWindowDIA(
  const SpectrumCollection::SpecCharge& sc,
  vector<int>* negative_isotope_errors,
  vector<double>* out_min,
  vector<double>* out_max,
  double* min_range,
  double* max_range
) {
   double unit_dalton = BIN_WIDTH;
   double mz_minus_proton = sc.spectrum->PrecursorMZ() - MASS_PROTON;
   double precursor_window = fabs(sc.spectrum->IsoWindowUpperMZ()-sc.spectrum->IsoWindowLowerMZ()) / 2;

   if (MathUtil::AlmostEqual(precursor_window, 0)) {
      precursor_window = avg_isowin_width_ / 2;
      carp(CARP_WARNING, "Input file does not specify window width. Inferring a fixed isolation window size of %f m/z.", precursor_window);
   }

   for (vector<int>::const_iterator ie = negative_isotope_errors->begin(); ie != negative_isotope_errors->end(); ++ie) {
    out_min->push_back((mz_minus_proton - precursor_window) * sc.charge + (*ie * unit_dalton));
    out_max->push_back((mz_minus_proton + precursor_window) * sc.charge + (*ie * unit_dalton));
   }
   *min_range = (mz_minus_proton*sc.charge + (negative_isotope_errors->front() * unit_dalton)) - precursor_window*sc.charge;
   *max_range = (mz_minus_proton*sc.charge + (negative_isotope_errors->back() * unit_dalton)) + precursor_window*sc.charge;
}

double DIAmeterApplication::getTailorQuantile(TideMatchSet::Arr2* match_arr2) {
   //Implementation of the Tailor score calibration method
   double quantile_score = 1.0;
   vector<double> scores;
   double quantile_th = 0.01;
   // Collect the scores for the score tail distribution
   for (TideMatchSet::Arr2::iterator it = match_arr2->begin(); it != match_arr2->end(); ++it) {
    scores.push_back((double)(it->first / XCORR_SCALING));
   }
   sort(scores.begin(), scores.end(), greater<double>());  //sort in decreasing order
   int quantile_pos = (int)(quantile_th*(double)scores.size()+0.5);

   if (quantile_pos < 3) { quantile_pos = 3; }
   // suggested by Attila for bug fix
   if (quantile_pos >= scores.size()) { quantile_pos = scores.size()-1; }

   quantile_score = scores[quantile_pos]+5.0; // Make sure scores positive
   return quantile_score;
}

double DIAmeterApplication::closestPPMValue(const double* mz_arr, const double* intensity_arr, int peak_num, double query_mz, int ppm_tol, double intensity_default, bool large_better) {
  int matched_mz_idx = MathUtil::binarySearch(mz_arr, peak_num, query_mz);
  if (matched_mz_idx < 0) { return intensity_default; }

  double matched_mz = mz_arr[matched_mz_idx];
  double matched_intensity = intensity_arr[matched_mz_idx];

  // double matched_mz_linear = MathUtil::linearSearch(mz_arr, peak_num, query_mz);
  // carp(CARP_DETAILED_DEBUG, "matched_mz_linear:%f \t matched_mz_binary:%f", matched_mz_linear, matched_mz );
  // if (!MathUtil::AlmostEqual(matched_mz_linear, matched_mz, 4)) { carp(CARP_FATAL, "mz are not matched!");  }

  double ppm = fabs(query_mz - matched_mz) * 1000000 / max(query_mz, matched_mz); // query_mz
  if (ppm > ppm_tol) { return intensity_default; }

  return matched_intensity;
}

string DIAmeterApplication::getCoeffTag() {
  stringstream param_ss;
  string coeff_tag = Params::GetString("coeff-tag");
  if (coeff_tag.empty()) {
    param_ss << "prec_" << StringUtils::ToString(Params::GetDouble("coeff-precursor"), 2);
    param_ss << "_frag_" << StringUtils::ToString(Params::GetDouble("coeff-fragment"), 2);
    param_ss << "_rt_" << StringUtils::ToString(Params::GetDouble("coeff-rtdiff"), 2);
    param_ss << "_elu_" << StringUtils::ToString(Params::GetDouble("coeff-elution"), 2);
  } else { param_ss << coeff_tag;  }
  return param_ss.str();
}


string DIAmeterApplication::getName() const {
  return "diameter";
}

string DIAmeterApplication::getDescription() const {
  return
    "[[nohtml:DIAmeter detects peptides from data-independent acquisition "
    "mass spectrometry data without requiring a spectral library.]]"
    "[[html:<p>DIAmeter detects peptides from data-independent acquisition "
    "mass spectrometry data without requiring a spectral library. "
    "The input includes centroided DIA data and a proteome FASTA database. "
    "DIAmeter then searches the DIA data using Tide, "
    "allowing multiple peptide-spectrum matches (PSMs) per DIA spectrum. "
    "A subset of these PSMs are selected for further analysis, "
    "using a greedy bipartite graph matching algorithm. "
    "Finally, PSMs are augmented and filtered with auxiliary features describing "
    "various types of evidence supporting the detection of the associated peptide. "
    "The PSM feature vectors, the output of DIAmeter, should be processed subsequently "
    "by Percolator to induce a ranking on peptides. "
    "Percolator will assign each peptide a statistical confidence estimate, "
    "where highly ranked peptides are detected in the DIA data with stronger confidence. "
    "Further details are provided here:</p><blockquote>YY Lu, J Bilmes, RA Rodriguez-Mias, J Villen, and WS Noble. "
    "<a href=\"https://doi.org/10.1093/bioinformatics/btab284\">"
    "&quot;DIAmeter: Matching peptides to data-independent acquisition mass spectrometry data&quot;</a>. <em>Bioinformatics</em>. "
    "37(Supplement_1):i434–i442, 2021.</blockquote>"
    "<p>DIAmeter performs several intermediate steps, as follows:</p>"
    "<ol><li>If a FASTA file was provided, convert it to an index using tide-index. Otherwise, use the given Tide index.</li>"
    "<li>Convert the given fragmentation spectra to a binary format.</li>"
    "<li>Search the spectra against the database and extract the auxiliary features.</li>"
	"<li>Store the results in Percolator input (PIN) format.</li>"
    "<li>Run the PIN file through Percolator. </li></ol>"
	"]]";
}

vector<string> DIAmeterApplication::getArgs() const {
  string arr[] = {
  "tide spectra file+",
  "tide database"
  };
  return vector<string>(arr, arr + sizeof(arr) / sizeof(string));
}

vector<string> DIAmeterApplication::getOptions() const {
  string arr[] = {
  "max-precursor-charge",
  "mz-bin-offset",
  "mz-bin-width",
  "output-dir",
  "overwrite",
  // "precursor-window",
  "predrt-files",
  // "msamanda-regional-topk",
  // "coelution-oneside-scans",
  // "coelution-topk",
  // "coeff-precursor",
  // "coeff-fragment",
  // "coeff-rtdiff",
  // "coeff-elution",
  "prec-ppm",
  "frag-ppm",
  "top-match",
  "diameter-instrument",
  "verbosity"
  };
  return vector<string>(arr, arr + sizeof(arr) / sizeof(string));
}

vector< pair<string, string> > DIAmeterApplication::getOutputs() const {
  vector< pair<string, string> > outputs;

  outputs.push_back(make_pair("diameter.psm-features.txt",
    "a tab-delimited text file containing the feature of the searched PSMs. "));
  outputs.push_back(make_pair("diameter.psm-features.filtered.txt",
    "a tab-delimited text file containing the feature of the PSMs after filtering. "));
  outputs.push_back(make_pair("diameter.features.pin",
      "the searched PSM results in Percolator input (PIN) format. "));
  outputs.push_back(make_pair("diameter.params.txt",
  "a file containing the name and value of all parameters/options for the "
  "current operation. Not all parameters in the file may have been used in "
  "the operation. The resulting file can be used with the --parameter-file "
  "option for other Crux programs."));
  outputs.push_back(make_pair("diameter.log.txt",
  "a log file containing a copy of all messages that were printed to the "
  "screen during execution."));
  outputs.push_back(make_pair("percolator-output",
    "the output of percolator after running the PIN file."));

  return outputs;
}
bool DIAmeterApplication::needsOutputDirectory() const {
  return true;
}

COMMAND_T DIAmeterApplication::getCommand() const {
  return DIAMETER_COMMAND;
}

void DIAmeterApplication::processParams() {
  const string index = Params::GetString("tide database");
  if (!FileUtils::Exists(index)) { carp(CARP_FATAL, "'%s' does not exist", index.c_str()); }
  else if (FileUtils::IsRegularFile(index)) { // Index is FASTA file
    carp(CARP_INFO, "Creating index from '%s'", index.c_str());
    string targetIndexName = Params::GetString("store-index");
    if (targetIndexName.empty()) {
      targetIndexName = FileUtils::Join(Params::GetString("output-dir"), "tide-search.tempindex");
    }
    remove_index_ = targetIndexName;

    Params::Set("peptide-list", true);
    Params::Set("decoy-format", "peptide-reverse");

    TideIndexApplication indexApp;
    indexApp.processParams();
    if (indexApp.main(index, targetIndexName) != 0) { carp(CARP_FATAL, "tide-index failed."); }
    Params::Set("tide database", targetIndexName);

  } else { // Index is Tide index directory
    pb::Header peptides_header;
    string peptides_file = FileUtils::Join(index, "pepix");
    HeadedRecordReader peptide_reader(peptides_file, &peptides_header);
    if ((peptides_header.file_type() != pb::Header::PEPTIDES) || !peptides_header.has_peptides_header()) { carp(CARP_FATAL, "Error reading index (%s).", peptides_file.c_str()); }

    const pb::Header::PeptidesHeader& pepHeader = peptides_header.peptides_header();

    Params::Set("enzyme", pepHeader.enzyme());
    const char* digestString = digest_type_to_string(pepHeader.full_digestion() ? FULL_DIGEST : PARTIAL_DIGEST);
    Params::Set("digestion", digestString);
    Params::Set("isotopic-mass", pepHeader.monoisotopic_precursor() ? "mono" : "average");
  }

  // these are DIAmeter-specific param settings
  Params::Set("concat", true);
  Params::Set("use-tailor-calibration", true);
  Params::Set("precursor-window-type", "mz");
  Params::Set("spectrum-parser", "pwiz");
  Params::Set("num-threads", 1);

  // these are makepin-specific param settings
  output_pin_ = "diameter.features.pin";
  output_percolator_ = FileUtils::Join(Params::GetString("output-dir"), "percolator-output");

  if (StringUtils::IEquals(Params::GetString("diameter-instrument"), "orbitrap")) {
    Params::Set("spectra-denoising", false);
    Params::Set("psm-filter", false);
    Params::Set("prec-ppm", 10);
    Params::Set("frag-ppm", 10);
  }
  else if (StringUtils::IEquals(Params::GetString("diameter-instrument"), "tof5600")) {
    Params::Set("spectra-denoising", true);
    Params::Set("psm-filter", true);
    Params::Set("prec-ppm", 30);
    Params::Set("frag-ppm", 30);

    Params::Set("coeff-precursor", 3.2);
    Params::Set("coeff-fragment", 0.2);
    Params::Set("coeff-rtdiff", 0.2);
    Params::Set("coeff-elution", 0.2);
  }
  else if (StringUtils::IEquals(Params::GetString("diameter-instrument"), "tof6600")) {
    Params::Set("spectra-denoising", false);
    Params::Set("psm-filter", true);
    Params::Set("prec-ppm", 30);
    Params::Set("frag-ppm", 30);

    Params::Set("coeff-precursor", 25.6);
    Params::Set("coeff-fragment", 0.2);
    Params::Set("coeff-rtdiff", 0.2);
    Params::Set("coeff-elution", 0);
  }
  else if (StringUtils::IEquals(Params::GetString("diameter-instrument"), "na")) {
	// do nothing
  }
  else { carp(CARP_FATAL, "Wrong diameter-instrument setup %s!", Params::GetString("diameter-instrument").c_str()); }

  /*
  if (Params::GetBool("psm-filter")) {
    stringstream pin_ss;
    pin_ss << "diameter." << getCoeffTag() << ".pin";
    output_pin_ = pin_ss.str().c_str();

    stringstream percolator_ss;
    percolator_ss << "percolator-output-" << getCoeffTag();
    output_percolator_ = FileUtils::Join(Params::GetString("output-dir"), percolator_ss.str());
  }
  */
  Params::Set("output-file", output_pin_);
  Params::Set("unique-scannr", true);
  carp(CARP_INFO, "Updating output pin file = '%s'", output_pin_.c_str());

  // these are Percolator-specific param settings
  Params::Set("tdc", false);
  Params::Set("output-weights", true);
  Params::Set("only-psms", false);
  carp(CARP_INFO, "Updating output percolator dir = '%s'", output_percolator_.c_str());
}
