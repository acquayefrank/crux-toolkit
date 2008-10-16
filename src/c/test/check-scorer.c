#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "check-peak.h"
#include "../spectrum.h"
#include "../spectrum_collection.h"
#include "../peak.h"
#include "../crux-utils.h"
#include "../scorer.h"
#include "../objects.h"
#include "../parameter.h"
#include "../ion_series.h"

#define scan_num 16
//#define ms2_file "test.ms2"
#define ms2_file "test2.ms2"
#define parameter_file "test_parameter_file"

START_TEST (test_create){
  SPECTRUM_T* spectrum = NULL;
  SPECTRUM_COLLECTION_T * collection = NULL; ///<spectrum collection
  ION_SERIES_T* ion_series = NULL;
  SCORER_T* scorer = NULL;
  float score = 0;

  //parse paramter file
  //parse_update_parameters(parameter_file);

  initialize_parameters();
  char* ops[1] = {"parameter-file"};
  fail_unless( select_cmd_line_options(ops, 1) == TRUE,
               "Failed to select command line options");
  char* args[1] = {"protein input"};
  fail_unless( select_cmd_line_arguments(args, 1) == TRUE,
    "Failed to select command line arguments");

  char* fake_argv[4] = {"app", "--parameter-file", parameter_file, "prot"};
  // parse command line and param file
  fail_unless(parse_cmd_line_into_params_hash(4, fake_argv, "app")==TRUE,
              "Failed to parse param file %s with mods", "1mod");
  

  //set parameter for fasta_file, although not used here...
  //set_string_parameter("fasta-file", "fasta_file");
  
  //parameters has been confirmed
  //parameters_confirmed();
  
  int peptide_charge = get_int_parameter("charge");

  //set ion constraint to sequest settings
  ION_CONSTRAINT_T* ion_constraint = new_ion_constraint_sequest_sp(peptide_charge);  
  
  //create new ion series
  ion_series = new_ion_series("AKLVKNMT", 2, ion_constraint);

  //now predict ions
  predict_ions(ion_series);

  //read ms2 file
  collection = new_spectrum_collection(ms2_file);
  spectrum = allocate_spectrum();
  
  //search for spectrum with correct scan number
  fail_unless(get_spectrum_collection_spectrum(collection, scan_num, spectrum), "failed to find scan_num in ms3 file");

  //create new scorer
  scorer = new_scorer(SP);  

  //check if scorer has been set right
  fail_unless(get_scorer_type(scorer) == SP, "failed to set scorer type");
  fail_unless(compare_float(get_scorer_sp_beta(scorer), 0.075) == 0, "failed to set beta");
  fail_unless(compare_float(get_scorer_sp_max_mz(scorer), 4000) == 0, "failed to set max mz");

  //calculates the Sp score
  score = score_spectrum_v_ion_series(scorer, spectrum, ion_series);

  //print the Sp score
  printf("Sp score is: %.2f\n", score);
  
  fail_unless(compare_float(score, 5.35885334014892578125) == 0, "sp score does not match the expected value");
  
  //free heap
  free_scorer(scorer);
  free_ion_constraint(ion_constraint);
  free_ion_series(ion_series);
  free_spectrum_collection(collection);
  free_spectrum(spectrum);
}
END_TEST

Suite *scorer_suite(void){
  Suite *s = suite_create("Scorer");
  TCase *tc_core = tcase_create("Core");
  suite_add_tcase(s, tc_core);
  tcase_add_test(tc_core, test_create);
  return s;
}
