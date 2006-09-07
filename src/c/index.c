/*****************************************************************************
 * \file index.c
 * $Revision: 1.22 $
 * \brief: Object for representing an index of a database
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "utils.h"
#include "crux-utils.h"
#include "peptide.h"
#include "protein.h"
#include "index.h"
#include "carp.h"
#include "sorter.h"
#include "objects.h"
#include "peptide_constraint.h"
#include "database.h"

//maximum proteins the index can handle
#define MAX_PROTEIN 30000
#define MAX_INDEX_FILES 30000
#define MAX_FILE_NAME_LENGTH 30
#define NUM_CHECK_LINES 8
#define MAX_PROTEIN_IN_BIN 2500
#define MAX_FILE_SIZE_TO_USE_LIGHT_PROTEIN  500000000

//global variable to store the temp directory
//used for deleting directory when SIGINT
char temp_folder_name[12] = "";

/**
 * clean_up
 * cleans up the temporary directory when SIGINT
 */
void clean_up( int dummy ) {

  struct dirent **namelist =NULL;
  int num_file =0;

  fcloseall();
  chdir(temp_folder_name);
  //collect all files in temp dir
  num_file = scandir(".", &namelist, 0, alphasort);

  //delete all files in temp dir
  while(num_file--){
    remove(namelist[num_file]->d_name);
    free(namelist[num_file]);
  }
  free(namelist);

  chdir("..");
  //rmdir(temp_folder_name);
  exit(-1);

  //quiet compiler
  dummy = dummy;
}

/* 
 * How does the create_index (the routine) work?
 *
 * - Creates a database object from a the fasta file member variable.
 *
 * - Depending on the size of the database, determines how many passes i
 *   will need to make over the database peptides (first implementation
 *   will only make one pass, later implementation can make multiple passes
 *   with multiple iterators)
 *
 * - From the directory and mass resolution member variables, 
 *   creates the list of filenames necessary for storing the peptides 
 *   
 * - Creates a database_peptide_iterator object from the database 
 *   and peptide constraint objects
 *
 * - Then starts iterating through peptides 
 *
 *    - From the peptide mass determines which file to place it in, and
 *      then calls the serialize_peptide method
 *
 *      - The serialize peptide method is an object method specifically
 *        designed for this purpose, and writes a peptide in a format from
 *        which it can be reconstructed
 *
 *        - Serialize peptide needs to serialize the peptide_src objects. To
 *        do that, it needs the idx of the peptide_src's protein objects in
 *        the database. This is retrieved from the protein idx member
 *        variable (i.e. this protein is the 0th protein, 1st protein etc.)
 *        which is set at database creation. Note, that this won't have to 
 *        change when we move to light proteins.
 *
 * LATER At some point we will index each of the peptide files too (in a TOC), 
 * so that we can retrieve them rapidly later. 
 *
 * LATER We implement light proteins and possibly an 
 * LATER create_index for protein locations in the database allowing rapid
 * creation of light protein objects.
 */

/* 
 * How does generate_peptides (the executable) with --from-index work?
 *
 * - Given a fasta file, looks for the corresponding index on disk. Fail if it
 *   can't find the corresponding index files.
 *
 * - Instantiates an index object from the fasta file with new_index.
 *
 * - Attempts to parse_index the index.
 *
 * - Instantiates an index_peptide_iterator from the index, according to
 *   constraints passed on the command line to generate_peptides.
 *
 * - Then starts iterating through peptides, which are being loaded from
 *   disk, and outputs them as before
 * 
 * LATER We implement light proteins.
 * LATER use an index for protein offsets in the database allowing rapid
 * creation of light protein objects.
 *
 * LATER Develop a conversion from light to heavy and heavy to light protein
 * objects to avoid excessive memory use.
 */

/**
 * \struct index
 * \brief A index of a database
 */
struct index{
  DATABASE_T* database; ///< The database that has been indexed.
  char* directory; ///< The directory containing the indexed files
  //char* filenames[INDEX_MAX_FILES]; ///< The files that contain the peptides
  PEPTIDE_CONSTRAINT_T* constraint; ///< Constraint which these peptides satisfy
  BOOLEAN_T on_disk; ///< Does this index exist on disk yet?
  float mass_range;  ///< the range of mass that each index file should be partitioned into
  unsigned int max_size;  ///< maximum limit of each index file
  BOOLEAN_T is_unique; ///< only unique peptides? -in
};    

/**
 * \struct index files
 * \brief A struct that contains the information of each file
 */
struct index_file{
  char* filename;  ///< The file name that contain the peptides
  float start_mass; ///< the start mass limit in this file
  float interval;   ///< the interval of the peptides in this file
};
typedef struct index_file INDEX_FILE_T;


/**
 * \struct index_peptide_iterator
 * \brief An iterator to iterate over the peptides in a database
 */
struct index_peptide_iterator{
  INDEX_T* index; ///< The index object which we are iterating over
  INDEX_FILE_T* index_files[MAX_INDEX_FILES]; ///< the index file array that contain information of each index file 
  int total_index_files; ///< the total count of index_files
  int current_index_file; ///< the current working index_file
  //unsigned int peptide_idx; ///< The (non-object) index of the current peptide.
  //char* current_index_filename; ///< The current filename that we are reading from
  FILE* index_file; ///< The current file stream that we are reading from
  BOOLEAN_T has_next; ///< Is there another peptide?
  PEPTIDE_T* peptide; ///< the next peptide to return
};    

/**
 * \struct index_filtered_peptide_iterator
 * \brief An iterator to filter out the peptides wanted from the index_peptide_iterator
 */
struct index_filtered_peptide_iterator{
  INDEX_PEPTIDE_ITERATOR_T* index_peptide_iterator;  ///< The core peptide iterator
  BOOLEAN_T has_next; ///< Is there another peptide?
  PEPTIDE_T* peptide; ///< the next peptide to return
};    

/**
 * \struct bin_peptide_iterator
 * \brief An iterator to iterate over the peptides in a bin( one file handler)
 */
struct bin_peptide_iterator{
  INDEX_T* index; ///< The index object which we are iterating over
  FILE* index_file; ///< The current file stream that we are reading from
  BOOLEAN_T has_next; ///< Is there another peptide?
  PEPTIDE_T* peptide; ///< the next peptide to return
};    


/**
 * \struct bin_sorted_peptide_iterator
 * \brief Object to iterate over the peptides within a bin, in an
 * sort in mass
 */
struct bin_sorted_peptide_iterator {
  SORTED_PEPTIDE_ITERATOR_T* sorted_peptide_iterator;
};

/************
 * index
 ************/

/**
 * \returns An (empty) index object.
 */
INDEX_T* allocate_index(void){
  INDEX_T* index = (INDEX_T*)mycalloc(1, sizeof(INDEX_T));
  return index;
}

/**
 * given a fasta_file name it returns the index file directory name
 * format: myfasta_crux_index
 * \returns A heap allocated index file directory name of the given fasta file
 */
char* generate_directory_name(
  char* fasta_filename
  )
{
  int len = strlen(fasta_filename);
  int end_idx = len;
  int end_path = len;  //index of where the "." is located in the file
  char* dir_name = NULL;
  char* dir_name_tag =  "_crux_index";
  
  //cut off the ".fasta" if needed
  for(; end_idx > 0; --end_idx){
    if(strcmp(&fasta_filename[end_idx - 1], ".fasta") == 0){
      end_path = end_idx - 1;
      break;
    }
  }
  
  dir_name = (char*)mycalloc(end_path + strlen(dir_name_tag) + 1, sizeof(char));
  strncpy(dir_name, fasta_filename, end_path);
  strcat(dir_name, dir_name_tag);
  return dir_name;
}

/**
 * Assumes that the fasta file is always in the directory where the crux_index_file directory is located
 * \returns A new index object.
 */
INDEX_T* new_index(
  char* fasta_filename,  ///< The fasta file
  PEPTIDE_CONSTRAINT_T* constraint,  ///< Constraint which these peptides satisfy
  float mass_range,  ///< the range of mass that each index file should be partitioned into
  unsigned int max_size, ///< maximum limit of each index file
  BOOLEAN_T is_unique, ///< only unique peptides? -in
  BOOLEAN_T use_light ///< should i use light/heavy functionality? -in
  )
{
  char** filename_and_path = NULL;
  char* working_dir = NULL;
  INDEX_T* index = allocate_index();
  DATABASE_T* database = new_database(fasta_filename, use_light); //probably should change

  filename_and_path = parse_filename_path(fasta_filename);
  working_dir = generate_directory_name(filename_and_path[0]);
  DIR* check_dir = NULL;

  //check if already parsed
  if(!get_database_is_parsed(database)){
    if(!parse_database(database)){
      carp(CARP_FATAL, "failed to parse database, cannot create new index");
      free_database(database);
      free(index);
      fcloseall();
      exit(1);
    }
  }
  
  //check if the index files are on disk
  //are we currently in the crux dircetory
  if(filename_and_path[1] == NULL){
    if((check_dir =  opendir(working_dir)) != NULL){
      set_index_on_disk(index, TRUE);
    }
    else{
      set_index_on_disk(index, FALSE);
    }
  }
  else{//we are not in crux directory
    char* full_path = cat_string(filename_and_path[1],  working_dir);
    if((check_dir = opendir(full_path)) != NULL){
      set_index_on_disk(index, TRUE);
    }
    else{
      set_index_on_disk(index, FALSE);
    }
    free(full_path);
  }
  
  //set each field
  set_index_directory(index, working_dir);
  set_index_constraint(index, constraint);
  set_index_database(index, database);
  set_index_mass_range(index, mass_range);
  set_index_max_size(index, max_size);
  set_index_is_unique(index, is_unique);

  //free filename and path string array
  free(check_dir);
  free(working_dir);
  free(filename_and_path[0]);
  free(filename_and_path[1]);
  free(filename_and_path);
  
  return index;
}         

/**
 * wrapper function, create index object for search purpose
 * If no crux_index files been created, returns null
 * \returns A new index object ready for search.
 */
INDEX_T* new_search_index(
  char* fasta_filename,  ///< The fasta file
  PEPTIDE_CONSTRAINT_T* constraint,  ///< Constraint which these peptides satisfy
  BOOLEAN_T is_unique ///< only unique peptides? -in
  )
{
  INDEX_T* search_index = NULL;
  BOOLEAN_T use_light = FALSE;

  /**
   * use heavy/light protein function if the fasta file is larger thatn limit
   * this way it only parse the proteins that are need when going through the index
   * for smaller size fasta files it's faster to just parse all of them
   */
  if(get_filesize(fasta_filename) >  MAX_FILE_SIZE_TO_USE_LIGHT_PROTEIN){
    use_light = TRUE;
    carp(CARP_INFO, "Using heavy/light protein function for file size: %d",
         (int)get_filesize(fasta_filename));
  }
  
  //sets mass_range, max_size to an arbitrary 0
  search_index = new_index(fasta_filename, constraint, 0, 0, is_unique, use_light);
  
  //check if crux_index files have been made
  if(!get_index_on_disk(search_index)){
    carp(CARP_ERROR, 
         "must create index files before search, and fasta file must be in the directory where index file directory is present");
    free_index(search_index);
    return NULL;
  }

  return search_index;
}

/**
 * Frees an allocated index object.
 */
void free_index(
  INDEX_T* index
  )
{
  free_database(index->database);
  free(index->directory);
  free_peptide_constraint(index->constraint);
  free(index);
}

/**
 * write to the file stream various information of the
 * index files created
 */
BOOLEAN_T write_header(
  INDEX_T* index, ///< the working index -in
  FILE* file ///< out put stream for crux_index_map -in
  )
{
  time_t hold_time;
  hold_time = time(0);
  PEPTIDE_CONSTRAINT_T* constraint = index->constraint;

  fprintf(file, "#\tmin_mass: %.2f\n", get_peptide_constraint_min_mass(constraint));
  fprintf(file, "#\tmax_mass: %.2f\n", get_peptide_constraint_max_mass(constraint));
  fprintf(file, "#\tmin_length: %d\n", get_peptide_constraint_min_length(constraint));
  fprintf(file, "#\tmax_length: %d\n", get_peptide_constraint_max_length(constraint));
  fprintf(file, "#\tpeptide_type: %d\n", get_peptide_constraint_peptide_type(constraint));
  fprintf(file, "#\tmissed_cleavage: %d\n", get_peptide_constraint_num_mis_cleavage(constraint));
  fprintf(file, "#\tmass_type: %d\n", get_peptide_constraint_mass_type(constraint));
  fprintf(file, "#\tredundancy: %d\n", get_index_is_unique(index));
  
  fprintf(file, "#\tCRUX index directory: %s\n", index->directory);
  fprintf(file, "#\ttime created: %s",  ctime(&hold_time)); 
  fprintf(file, "#\tmaximum size of each index file: %d\n", index->max_size);
  fprintf(file, "#\ttarget mass range for index file: %.2f\n", index->mass_range);
  fprintf(file, "#\tcopyright: %s\n", "William Noble");
  return TRUE;
}

/**
 * heap allocated, users must free
 * \returns a temporary directory name template
 */
char* make_temp_dir_template(void){
  char* template = (char*)mycalloc(12, sizeof(char));
  strcpy(template, "crux_XXXXXX");
  return template;
}

/**
 * heap allocated filename
 *\returns the filename for the given index
 */
char* get_crux_filename(
  long bin_idx,  ///< the bin_indx name you want -in
  int part  ///< the what sub part of the dir is it? only needed when spliting -in
  )
{
  char* file_num = 0;
  char* filename_tag = "crux_index_";
  char* filename = NULL;

  //quiet compiler
  part = part;

  //add functionallity to make _1, _2, _3
  file_num = int_to_char(bin_idx + 1);
  filename = cat_string(filename_tag, file_num);

  free(file_num);
  return filename;
}

/**
 * Calculate the total number of bins( file handlers) that will be needed
 * \returns the total number of bins needed
 */
long get_num_bins_needed(
  INDEX_T* index, ///< working index -in
  int* mass_limits  ///< an array that holds the min/max mass limit -in
  )
{
  int min_length = get_peptide_constraint_min_length(index->constraint);
  int max_length = get_peptide_constraint_max_length(index->constraint);
  float min_mass = get_peptide_constraint_min_mass(index->constraint);
  float max_mass = get_peptide_constraint_max_mass(index->constraint);
  float min_mass_limit = min_mass;
  float max_mass_limit = max_mass;
  long num_bins = 0;

  //reset minimum mass limit
  if(min_length * 57 + MASS_H2O_MONO > min_mass){
    min_mass_limit = min_length * 57 + MASS_H2O_MONO; 
  }
  
  //reset maximum mass limit
  if(max_length * 187 + MASS_H2O_AVERAGE < max_mass){
    max_mass_limit = max_length * 187 + MASS_H2O_AVERAGE;
  }

  //set mass limit info array
  min_mass_limit = (int)min_mass_limit;
  max_mass_limit = (int)max_mass_limit + 1;

  (mass_limits)[0] = min_mass_limit;
  (mass_limits)[1] = max_mass_limit;

  num_bins = ((max_mass_limit - min_mass_limit) / index->mass_range) + 1;  //check..

  return num_bins;
}                         

/**
 * user MUST set the unix system max allowed file handlers enough to allow this procsess
 * check and change on command line by "ulimit -n", need root permission to change...
 *generates all the file handlers(bins) that are needed
 *\returns TRUE, if successfully opened all needed bins, else FALSE
 */
BOOLEAN_T generate_file_handlers(
  FILE** file_array,  ///< the file handler array -out
  long num_bins  ///< total number of bins needed
  )
{
  long bin_indx = 0;
  FILE* file = NULL;
  char* filename = NULL;
  
  //create all the file handlers need for create index
  for(; bin_indx < num_bins; ++bin_indx){
    filename = get_crux_filename(bin_indx, 0);
    file = fopen(filename, "w+" );
    free(filename);
    if(file == NULL){
      carp(CARP_WARNING, "cannot open all file handlers needed");
      free(file_array);
      return FALSE;
    }
    (file_array)[bin_indx] = file;
  }
  
  return TRUE;
}

/**
 * user MUST set the unix system max allowed file handlers enough to allow this procsess
 * check and change on command line by "ulimit -n", need root permission to change...
 *generates all the file handlers(bins) that are needed
 *\returns TRUE, if successfully opened bin, else FALSE
 */
BOOLEAN_T generate_one_file_handler(
  FILE** file_array,  ///< the file handler array -out                            
  long bin_index ///< the bin index to create a file handler -in
  )
{
  FILE* file = NULL;
  char* filename = NULL;
  
  //create the file handler needed for create index
  filename = get_crux_filename(bin_index, 0);
  file = fopen(filename, "w+" );
  free(filename);
  if(file == NULL){
    carp(CARP_WARNING, "cannot open all file handlers needed");
    return FALSE;
  }
  file_array[bin_index] = file;
  return TRUE;
}

/**
 * given a mass, it finds the correct bin for that mass and returns
 * the file handler to that bin
 *\returns the FILE handler that the mass should be collected
 */
FILE* get_bin_file(
  int mass, ///< the query mass -in
  int low_mass,///< the mass limit array -in
  FILE*** file_array ///< file handler array -in
  )
{
  FILE* file = NULL;
  long bin_idx = (mass - low_mass)/100 + 1;
  file = (*file_array)[bin_idx];
  return file;
}

/**
 * given the file bin, reparses the peptides, sort them then reprint them in the crux_index file
 *\returns the sorted bin
 */
FILE* sort_bin(
  FILE* file, ///< the working file handler to the bin -in
  long bin_idx, ///< bin index in the file array -in
  INDEX_T* index ///< working index -in
  )
{
  char* filename = NULL;

  //check if file is empty
  if(ftell(file) == 0){
    return file;
  }

  BIN_SORTED_PEPTIDE_ITERATOR_T* peptide_iterator =
    new_bin_sorted_peptide_iterator(index, file);
  PEPTIDE_T* working_peptide = NULL;
  
  //get the filename for this file bin
  filename = get_crux_filename(bin_idx, 0);
  //close unsorted bin
  fclose(file);
  //create new bin which will be sorted 
  file = fopen(filename, "w" );
  
  //serialize all peptides in sorted order
  while(bin_sorted_peptide_iterator_has_next(peptide_iterator)){
    working_peptide = bin_sorted_peptide_iterator_next(peptide_iterator);
    serialize_peptide(working_peptide, file, 2);
    free_peptide(working_peptide);
  }
  
  free(filename);
  free_bin_sorted_peptide_iterator(peptide_iterator);

  return file;
}

/**
 * stores the peptide in the correct bin, if bin exceeds MAX_PROTEIN_IN_BIN, serialize all 
 * peptides in the bin.
 *\returns TRUE, if successful in storing the peptide or serializing peptides, else FALSE
 */
BOOLEAN_T dump_peptide(
  FILE** file_array,  ///< the working file handler array to the bins -in/out
  long int file_idx, ///< the index of the file array that the peptide belongs to -in
  PEPTIDE_T* working_peptide, ///< the peptide to be stored -in
  PEPTIDE_T** peptide_array, ///< the peptide array that stores the peptides before they get serialized -out
  int* bin_count ///< the count array of peptides in each bin -in
  )
{  
  int peptide_idx = 0;
  FILE* file = NULL;
  int current_count;
  
  //if the peptide count is over the limit
  if((current_count = bin_count[file_idx]) > MAX_PROTEIN_IN_BIN){
    file = file_array[file_idx];
    //print out all peptides
    for(; peptide_idx < current_count; ++peptide_idx){
      serialize_peptide(peptide_array[peptide_idx] , file, 3);
      free_peptide(peptide_array[peptide_idx]);
    }
    serialize_peptide(working_peptide, file, 3);
    free_peptide(working_peptide);
    bin_count[file_idx] = 0;
  }
  //if the peptide count is bellow the limit
  else{
    //store peptide in peptide array , these peptides will be printed later togehter
    peptide_array[(bin_count[file_idx])] = working_peptide;
    ++bin_count[file_idx];
  }
  return TRUE;
}

/**
 * serializes all peptides left int he peptide array, should be used at very last
 *\returns TRUE, if successful in serializing all peptides, else FALSE
 *frees both peptide_array and bin_count once all serialized
 */
BOOLEAN_T dump_peptide_all(
  FILE** file_array,   ///< the working file handler array to the bins -out
  PEPTIDE_T*** peptide_array, ///< the peptide array that stores the peptides before they get serialized -in
  int* bin_count, ///< the count array of peptides in each bin -in
  int num_bins ///< the total number of bins -in
  )
{  
  int peptide_idx = 0;
  FILE* file = NULL;
  PEPTIDE_T** working_array = NULL;
  int bin_idx = 0;
  int file_idx = 0;
  
  //print out all remaining peptides in the file_array
  for(; file_idx < num_bins; ++file_idx){
    //no peptides in this bin
    if((file = file_array[file_idx]) == NULL){
      free(peptide_array[file_idx]);
      continue;
    }
    working_array = peptide_array[file_idx];
    bin_idx = bin_count[file_idx];
    //print out all peptides in this specific bin
    while(bin_idx > 0){
      serialize_peptide(working_array[peptide_idx] , file, 3);
      free_peptide(working_array[peptide_idx]);
      --bin_idx;
      ++peptide_idx;
    }
    peptide_idx = 0;
    free(peptide_array[file_idx]);
  }
  //free both peptide array and bin_count array , bye bye
  free(bin_count);
  free(peptide_array);
  return TRUE;
}

/**
 * The main index method. Does all the heavy lifting, creating files
 * serializing peptides, etc. The index directory itself should have 
 * a standard suffix (e.g. cruxidx), so that a given fasta file will have
 * an obvious index location.
 *
 * assumes that the current directory is the crux directory where the fasta file is located
 *
 * Note: create an index .info file as to map masses to files and to store 
 * all information that was used to create this index.
 * \returns TRUE if success. FALSE if failure.
 */
BOOLEAN_T create_index(
  INDEX_T* index ///< An allocated index -in
  )
{
  FILE* info_out = NULL; // the file stream where the index creation infomation is sent
  
  //new stuff
  char* temp_dir_name = NULL;
  FILE** file_array = NULL;
  int* mass_limits = (int*)mycalloc(2, sizeof(int));
  long num_bins = 0;
  DATABASE_PEPTIDE_ITERATOR_T* peptide_iterator = NULL;
  PEPTIDE_T* working_peptide = NULL;
  float working_mass;
  char* filename = NULL;
  float mass_range = index->mass_range;

  //check if already created index
  if(index->on_disk){
    carp(CARP_INFO, "index already been created on disk");
    return TRUE;
  }
  
  //create temporary directory
  if((temp_dir_name = mkdtemp(make_temp_dir_template()))== NULL){
    carp(CARP_WARNING, "cannot create temporary directory");
    return FALSE;
  }
  
  //copy temporary folder name for SIGINT cleanup purpose
  strncpy(temp_folder_name, temp_dir_name, 12); 

  //move into temporary directory
  if(chdir(temp_dir_name) != 0){
    carp(CARP_WARNING, "cannot enter temporary directory");
    return FALSE;
  }
  
  //get number of bins needed
  num_bins = get_num_bins_needed(index, mass_limits);
  
  //create file handler array
  file_array = (FILE**)mycalloc(num_bins, sizeof(FILE*));

  //peptide array to store the peptides before serializing them all together
  PEPTIDE_T*** peptide_array = (PEPTIDE_T***)mycalloc(num_bins, sizeof(PEPTIDE_T**));
  int sub_indx = 0;
  for(; sub_indx < num_bins; ++sub_indx){
    peptide_array[sub_indx] = (PEPTIDE_T**)mycalloc(MAX_PROTEIN_IN_BIN, sizeof(PEPTIDE_T*));
  }
  // int array that stores the peptide count for each peptide array branch
  int* bin_count = (int*)mycalloc(num_bins, sizeof(int));

  //create the index map & info
  info_out = fopen("crux_index_map", "w");
  write_header(index, info_out);

  //create database peptide_iterator
  peptide_iterator =
    new_database_peptide_iterator(index->database, index->constraint);

  long int file_idx = 0;
  int low_mass = mass_limits[0];
  long int count_peptide = 0;

  
  //iterate through all peptides
  while(database_peptide_iterator_has_next(peptide_iterator)){    
    ++count_peptide;
    if(count_peptide % 1000 == 0){
      fprintf(stderr,"reached peptide: %d\n", (int)count_peptide);
    }

    working_peptide = database_peptide_iterator_next(peptide_iterator);
    working_mass = get_peptide_peptide_mass(working_peptide);
    file_idx = (working_mass - low_mass) / mass_range;

    //check if first time using this bin, if so create new file handler
    if(file_array[file_idx] == NULL){
      if(!generate_one_file_handler(file_array, file_idx)){
        carp(CARP_ERROR, "check filehandler limit on system");
        fcloseall();
        return FALSE;
      }
    }
    //dump peptide in bin or temporary matrix
    dump_peptide(file_array, file_idx, working_peptide, peptide_array[file_idx], bin_count); 
  }

  //dump all the left over peptides
  dump_peptide_all(file_array, peptide_array, bin_count, num_bins);
  

  long bin_idx = 0;
  //sort each bin  
  for(; bin_idx < num_bins; ++bin_idx){
    if(file_array[bin_idx] == NULL){
      continue;
    }
    // sort each bin
    if((file_array[bin_idx] = sort_bin(file_array[bin_idx], bin_idx, index)) == NULL){
      carp(CARP_WARNING, "failed to sort each bin");
      fcloseall();
      return FALSE;
    }

    //split if too big
    //print to crux_map
    filename = get_crux_filename(bin_idx, 0); //0 can change if need split the file
    fprintf(info_out, "%s\t%.2f\t", filename, mass_limits[0] + (bin_idx * index->mass_range));
    fprintf(info_out, "%.2f\n", index->mass_range);
    //free up heap
    free(filename);
    fclose(file_array[bin_idx]);
  }
  
  //close crux_index_map, free heap allocated objects
  fclose(info_out);
  free(mass_limits);
  free(file_array);
  free_database_peptide_iterator(peptide_iterator);

  chdir("..");
  
  //rename temporary direcotry to final directory name
  if(rename(temp_dir_name, index->directory) != 0){
    carp(CARP_WARNING, "cannot rename directory");
    return FALSE;
  }
  free(temp_dir_name);

  //set permission for the directory
  chmod(index->directory, S_IRWXU+S_IRWXG+S_IROTH+S_IXOTH);

  index->on_disk = TRUE;
  return TRUE;
}

/**
 * The main index method. Does all the heavy lifting, creating files
 * serializing peptides, etc. The index directory itself should have 
 * a standard suffix (e.g. cruxidx), so that a given fasta file will have
 * an obvious index location.
 *
 * assumes that the current directory is the crux directory where the fasta file is located
 *
 * Note: create an index .info file as to map masses to files and to store 
 * all information that was used to create this index.
 * \returns TRUE if success. FALSE if failure.
 */
/*
BOOLEAN_T create_index(
  INDEX_T* index ///< An allocated index -in
  )
{
  FILE* output = NULL; // the file stream for each index file
  FILE* info_out = NULL; // the file stream where the index creation infomation is sent
  DATABASE_SORTED_PEPTIDE_ITERATOR_T* sorted_iterator = NULL;
  PEPTIDE_T* peptide = NULL;
  unsigned int num_peptides = 0; //current number of peptides index file 
  int num_file = 1; //the ith number of index file created
  float current_mass_limit = index->mass_range;
  char* filename_tag = "crux_index_";
  char* file_num = NULL;

  //check if already created index
  if(index->on_disk){
    carp(CARP_INFO, "index already been created on disk");
    return TRUE;
  }
  //create temporary directory
  if(mkdir("crux_temp", S_IRWXU+S_IRWXG+S_IRWXO) != 0){
    carp(CARP_WARNING, "cannot create temporary directory");
    return FALSE;
  }
  
  //create peptide iterator
  sorted_iterator = 
    new_database_sorted_peptide_iterator(index->database, 
                                         index->constraint, MASS, get_index_is_unique(index));
     
  //check if any peptides are found
  if(!database_sorted_peptide_iterator_has_next(sorted_iterator)){
    carp(CARP_WARNING, "no matches found");
    return FALSE;
  }
  
  //move into temporary directory
  if(chdir("crux_temp") != 0){
    carp(CARP_WARNING, "cannot enter temporary directory");
    return FALSE;
  }
  
  //create the index map & info
  info_out = fopen("crux_index_map", "w");
  write_header(index, info_out);

  do{ 
    char* filename;

    //open the next index file
    if(num_peptides == 0){
      file_num = int_to_char(num_file);
      filename = cat_string(filename_tag, file_num);
      output = fopen(filename, "r+" );
      fprintf(info_out, "%s\t%.2f\t", filename, 0.00);
      free(file_num);
      free(filename);
    }
    
    peptide = database_sorted_peptide_iterator_next(sorted_iterator);
    
    //set the index file to the correct interval
    while(get_peptide_peptide_mass(peptide) > current_mass_limit ||
          num_peptides > index->max_size-1){
      fclose(output);
      ++num_file;
      num_peptides = 0;
      file_num = int_to_char(num_file);
      filename = cat_string(filename_tag, file_num);
      output = fopen(filename, "w");
     
      //reset mass limit and update crux_index_map
      if(get_peptide_peptide_mass(peptide) > current_mass_limit){
        fprintf(info_out, "%.2f\n", index->mass_range);
        fprintf(info_out, "%s\t%.2f\t", filename, current_mass_limit+0.01);//!!boarder conditions
        current_mass_limit += index->mass_range;
      }
      else{ //num_peptides > index->max_size
        fprintf(info_out, "%.2f\n",
                index->mass_range - 
                (current_mass_limit - get_peptide_peptide_mass(peptide))-0.01);
        fprintf(info_out, "%s\t%.2f\t", filename, get_peptide_peptide_mass(peptide));
        current_mass_limit = index->mass_range + get_peptide_peptide_mass(peptide);
      }

      free(file_num);
      free(filename);
    }

    serialize_peptide(peptide, output);
    free_peptide(peptide);
    
    ++num_peptides;
  } //serialize the peptides into index files
  while(database_sorted_peptide_iterator_has_next(sorted_iterator));
  
  //print land line in crux index map
  fprintf(info_out, "%.2f\n", index->mass_range);
  
  //close last index file and crux_index_map file
  fclose(info_out);
  fclose(output);

  //free iterator
  free_database_sorted_peptide_iterator(sorted_iterator);

  chdir("..");
  //rename crux_temp to final directory name
  if(rename("crux_temp", index->directory) != 0){
    carp(CARP_WARNING, "cannot rename directory");
    return FALSE;
  }

  index->on_disk = TRUE;
  return TRUE;
}
*/

/**
 * Does this index exist on disk?
 *
 * \returns TRUE if it does. FALSE if it does not.
 */
BOOLEAN_T index_exists(
  INDEX_T* index ///< An allocated index
  )
{
  return index->on_disk;
}

/*
 * Private methods
 */

/*
 * Returns the index filename appropriate for this peptide
 */
char* get_peptide_file_name(
    INDEX_T* index,
    PEPTIDE_T* peptide
    );



/*********************************************
 * set and get methods for the object fields
 *********************************************/

/**
 *\returns the directory of the index
 * returns a heap allocated new copy of the directory
 * user must free the return directory name
 */
char* get_index_directory(
  INDEX_T* index ///< The index -in
  )
{
  return my_copy_string(index->directory);
}

/**
 * sets the directory of the index
 * index->directory must been initiailized
 */
void set_index_directory(
  INDEX_T* index, ///< The index -in
  char* directory ///< the directory to add -in
  )
{
  free(index->directory);
  index->directory = my_copy_string(directory);
}

/**
 *\returns a pointer to the database
 */
DATABASE_T* get_index_database(
  INDEX_T* index ///< The index -in
  )
{
  return index->database;
}

/**
 * sets the database of the index
 */
void set_index_database(
  INDEX_T* index, ///< The index -in
  DATABASE_T* database ///< The database that has been indexed. -in
  )
{
  index->database = database;
}

/**
 *\returns a pointer to the peptides constraint
 */
PEPTIDE_CONSTRAINT_T* get_index_constraint(
  INDEX_T* index ///< The index -in
  )
{
  return index->constraint;
}

/**
 * sets the peptides constraint
 */
void set_index_constraint(
  INDEX_T* index, ///< The index -in
  PEPTIDE_CONSTRAINT_T* constraint ///< Constraint which these peptides satisfy -in
  )
{
  index->constraint = constraint;
}

/**
 *\returns TRUE if index files are on disk else FALSE
 */
BOOLEAN_T get_index_on_disk(
  INDEX_T* index ///< The index -in
  )
{
  return index->on_disk;
}

/**
 * sets the on disk field of index
 */
void set_index_on_disk(
  INDEX_T* index, ///< The index -in
  BOOLEAN_T on_disk ///< Does this index exist on disk yet? -in
  )
{
  index->on_disk = on_disk;
}

/**
 *\returns the range of mass that each index file should be partitioned into
 */
float get_index_mass_range(
  INDEX_T* index ///< The index -in
  )
{
  return index->mass_range;
}

/**
 * sets the mass_range field of index
 */
void set_index_mass_range(
  INDEX_T* index, ///< The index -in
  float mass_range  ///< the range of mass that each index file should be partitioned into -in
  )
{
  index->mass_range = mass_range;
}

/**
 *\returns maximum limit of each index file
 */
unsigned int get_index_max_size(
  INDEX_T* index ///< The index -in
  )
{
  return index->max_size;
}

/**
 * sets the maximum limit of each index file for the index object
 */
void set_index_max_size(
  INDEX_T* index, ///< The index -in
  unsigned int max_size  ///< maximum limit of each index file -in
  )
{
  index->max_size = max_size;
}


/**
 *\returns TRUE if only allow unique peptides else FALSE
 */
BOOLEAN_T get_index_is_unique(
  INDEX_T* index ///< The index -in
  )
{
  return index->is_unique;
}

/**
 * sets the is_unique field
 */
void set_index_is_unique(
  INDEX_T* index, ///< The index -in
  BOOLEAN_T is_unique ///< do you allow duplicate peptides? -in
  )
{
  index->is_unique = is_unique;
}


/**************************
 * Index file
 **************************/

//FIXME see if filename is a heap allocated
/**
 *\returns a new heap allocated index file object
 */
INDEX_FILE_T* new_index_file(
  char* filename,  ///< the filename to add -in
  float start_mass,  ///< the start mass of the index file  -in
  float range  ///< the mass range of the index file  -in
  )
{
  INDEX_FILE_T* index_file =
    (INDEX_FILE_T*)mycalloc(1, sizeof(INDEX_FILE_T));
  
  index_file->filename = filename;
  index_file->start_mass = start_mass;
  index_file->interval = range;

  return index_file;
}

/**
 * adds a new index_file object to the index_file
 * \returns TRUE if successfully added the new index_file
 */
BOOLEAN_T add_new_index_file(
  INDEX_PEPTIDE_ITERATOR_T* index_peptide_iterator,  ///< the index_peptide_iterator to add file -out
  char* filename_parsed,  ///< the filename to add -in
  float start_mass,  ///< the start mass of the index file  -in
  float range  ///< the mass range of the index file  -in
  )
{
  char* filename = my_copy_string(filename_parsed);
  
  //check if total index files exceed MAX limit
  if(index_peptide_iterator->total_index_files > MAX_INDEX_FILES-1){
    carp(CARP_WARNING, "too many index files to read");
    return FALSE;
  }
  //create new index_file
  index_peptide_iterator->index_files[index_peptide_iterator->total_index_files] =
    new_index_file(filename, start_mass, range);
  
  ++index_peptide_iterator->total_index_files;
  return TRUE;
}

/**
 * frees the index file
 */
void free_index_file(
  INDEX_FILE_T* index_file  ///< the index file object to free -in
  )
{
  free(index_file->filename);
  free(index_file);
}

/*************************************
 * index_peptide iterator subroutines
 *************************************/

/**
 * Checks up to the NUM_CHECK_LINES limit
 * checks if the peptide query is supported by the crux_index database
 * \returns TRUE if the database supports the peptide query FALSE if not
 */
BOOLEAN_T check_index_db_boundary(
  char* new_line,  ///< the parsed header line -in
  INDEX_T* index ///< the query index -in
  )
{
  PEPTIDE_CONSTRAINT_T* constraint = index->constraint;///< the query peptide constraint -in
  float check_value;
  float real_value;
  char temp_string[2] = "";
  char field[20] = "";

  //parse the # ..... line
  if(sscanf(new_line,"%s %s %f", 
            temp_string, field, &check_value) < 3){
    return FALSE;
  }
  //check peptide min mass
  if(strncmp("min_mass:", field, 9) == 0){
    if(check_value > (real_value = get_peptide_constraint_min_mass(constraint))){
      carp(CARP_ERROR, "min_mass: %.2f is below supported database mass %.2f", real_value, check_value);
      return FALSE;
    }
  }
  //check peptide max mass  
  else if(strncmp("max_mass:", field, 9) == 0){
    if(check_value < (real_value = get_peptide_constraint_max_mass(constraint))){
      carp(CARP_ERROR, "max_mass: %.2f is above supported database mass %.2f", real_value, check_value);
      return FALSE;
    }
  }
  //check peptide min length
  else if(strncmp("min_length:", field, 11) == 0){
    if(check_value > (real_value = get_peptide_constraint_min_length(constraint))){
      carp(CARP_ERROR, "min_length: %d is below supported database length %d", (int)real_value, (int)check_value);
      return FALSE;
    }
  }
  //check peptide max length
  else if(strncmp("max_length:", field, 11) == 0){
    if(check_value < (real_value = get_peptide_constraint_max_length(constraint))){
      carp(CARP_ERROR, "max_length: %d is above supported database length %d", (int)real_value, (int)check_value);
      return FALSE;
    }
  }
  //check peptide_type
  else if(strncmp("peptide_type:", field, 13) == 0){
    //search is allowed if the database was created as ANY_TRYPTIC or matches the query peptide type
    if((int)check_value != ANY_TRYPTIC  &&
       (int)check_value != (int)get_peptide_constraint_peptide_type(constraint)){
     
      if((int)check_value == TRYPTIC){
         carp(CARP_ERROR, "peptide_type does not match the database supported type: TRYPRIC");
      }
      else if((int)check_value == NOT_TRYPTIC){
        carp(CARP_ERROR, "peptide_type does not match the database supported type: NON_TRYPRIC");
      }
      else if((int)check_value == PARTIALLY_TRYPTIC){
        carp(CARP_ERROR, "peptide_type does not match the database supported type: PARTIALLY_TRYPRIC");
      }
      return FALSE;
    }
  }
  //check peptide missed_cleavage
  else if(strncmp("missed_cleavage:", field, 16) == 0){
    if(compare_float(check_value, (real_value = get_peptide_constraint_num_mis_cleavage(constraint))) != 0){
      if((int)real_value != TRUE){
        carp(CARP_ERROR, "missed_cleavage:FALSE, does not match the database supported TRUE");
      }
      else{
        carp(CARP_ERROR, "missed_cleavage:TRUE, does not match the database supported FALSE");
      }
      return FALSE;
    }
  }
  //check peptide mass_type
  else if(strncmp("mass_type:", field, 10) == 0){
    if(compare_float(check_value, (real_value = get_peptide_constraint_mass_type(constraint))) != 0){
      if((int)real_value != AVERAGE){
        carp(CARP_ERROR, "mass_type: MONO, does not match the database supported type AVERAGE");
      }
      else{
        carp(CARP_ERROR, "mass_type: AVERAGE, does not match the database supported type MONO");
      }
      return FALSE;
    }
  }
  //check redundancy
  else if(strncmp("redundancy:", field, 11) == 0){
    if(compare_float(check_value, (real_value = get_index_is_unique(index))) != 0){
      if((int)real_value == FALSE){
        carp(CARP_ERROR, "peptide redundancy: REDUNDANT, does not match the database supported type UNIQUE");
      }
      else{
        carp(CARP_ERROR, "peptide redundancy: UNIQUE, does not match the database supported type REDUNDANT");
      }
      return FALSE;
    }
  }
  
  return TRUE;
}

/**
 * parses the "crux_index_map" file that contains the mapping between
 * each crun_index_* file and mass range
 * Adds all crux_index_* files that are with in the peptide constraint mass range
 * \returns TRUE if successfully parses crux_index_map
 */
BOOLEAN_T parse_crux_index_map(
  INDEX_PEPTIDE_ITERATOR_T* index_peptide_iterator  ///< working index_peptide_iterator -in
  )
{
  FILE* file = NULL;
  
  //used to parse each line from file
  char* new_line = NULL;
  int line_length;
  size_t buf_length = 0;
  
  //used to parse with in a line
  char filename[MAX_FILE_NAME_LENGTH] = "";
  float start_mass;
  float range;
  BOOLEAN_T start_file = FALSE;
  float min_mass = 
    get_peptide_constraint_min_mass(index_peptide_iterator->index->constraint);
  float max_mass = 
    get_peptide_constraint_max_mass(index_peptide_iterator->index->constraint);
  int num_line = 0;

  //move into the dir crux_files
  chdir(index_peptide_iterator->index->directory);
  
  //open crux_index_file
  file = fopen("crux_index_map", "r");
  if(file == NULL){
    carp(CARP_WARNING, "cannot open crux_index_map file");
    fclose(file);
    return FALSE;
  }
  
  while((line_length =  getline(&new_line, &buf_length, file)) != -1){
    //check header lines
    if(new_line[0] == '#'){
      //check if crux_index_database was created in a condition which the current query is supported
      if(num_line < NUM_CHECK_LINES && 
         !check_index_db_boundary(new_line, index_peptide_iterator->index)){
        carp(CARP_ERROR, "The current crux_index database does not support the query");
        fclose(file);
        free(new_line);
        return FALSE;
      }
      ++num_line;
      continue;
    }
    //is it a line for a crux_index_*
    else if(new_line[0] == 'c' && new_line[1] == 'r'){
      //read the crux_index_file information
      if(sscanf(new_line,"%s %f %f", 
                filename, &start_mass, &range) < 3){
        free(new_line);
        carp(CARP_WARNING, "incorrect file format");
        fclose(file);
        return FALSE;
      }
      //find the first index file with in mass range
      else if(!start_file){
        if(min_mass > start_mass + range - 0.0001){
          continue;
        }
        else{
          start_file = TRUE;
          if(!add_new_index_file(index_peptide_iterator, filename, start_mass, range)){
            carp(CARP_WARNING, "failed to add index file");
            fclose(file);
            free(new_line);
            return FALSE;
          }
          continue;
        }
      }
      //add all index_files that are with in peptide constraint mass interval
      else if(max_mass > (start_mass - 0.0001)){
        if(!add_new_index_file(index_peptide_iterator, filename, start_mass, range)){
          carp(CARP_WARNING, "failed to add index file");
          free(new_line);
          return FALSE;
        }
        continue;
      }
      //out of mass range
      break;
    }
  }
  free(new_line);
  fclose(file);
  return TRUE;
}

/**
 * Assumes that the file* is set at the start of the peptide_src count field
 * creates a new peptide and then adds it to the iterator to return
 * \returns TRUE if successfully parsed the pepdtide from the crux_index_* file
 */
BOOLEAN_T parse_peptide_index_file(
  void* general_peptide_iterator, 
  float peptide_mass, 
  int peptide_length,
  INDEX_TYPE_T index_type
  )
{
  INDEX_PEPTIDE_ITERATOR_T* peptide_iterator_db = NULL;
  BIN_PEPTIDE_ITERATOR_T* peptide_iterator_bin = NULL; 
  
  DATABASE_T* database = NULL;
  PEPTIDE_T* peptide = allocate_peptide();
  FILE* file = NULL;
  PROTEIN_T* parent_protein = NULL;
  int num_src = -1;
  int current_src = 1;
  int peptide_type = -1;
  int start_idx = -1;
  int protein_idx = -1;

  //variables used to parse each line
  char* new_line = NULL;
  int line_length;
  size_t buf_length = 0;
  PEPTIDE_SRC_T* peptide_src_array = NULL;

  //cast peptide iterator to correct type
  if(index_type == DB_INDEX){
    peptide_iterator_db = 
      (INDEX_PEPTIDE_ITERATOR_T*)general_peptide_iterator;
    file = peptide_iterator_db->index_file;
    database = peptide_iterator_db->index->database;
  }
  else if(index_type == BIN_INDEX){
    peptide_iterator_bin = 
      (BIN_PEPTIDE_ITERATOR_T*)general_peptide_iterator;
    file = peptide_iterator_bin->index_file;
    database = peptide_iterator_bin->index->database;
  }
  
  //set peptide fields
  set_peptide_length( peptide, peptide_length);
  set_peptide_peptide_mass( peptide, peptide_mass);

  //parse each line
  while((line_length =  getline(&new_line, &buf_length, file)) != -1){
    //get total number of peptide src
    if(num_src == -1){
      if(sscanf(new_line,"%d", &num_src) != 1){
        carp(CARP_WARNING, "failed to read number of peptide source, mass: %.2f", peptide_mass);
      }
      //create all needed peptide srcm than add to peptide
      peptide_src_array =  new_peptide_src_array(num_src);
      add_peptide_peptide_src_array(peptide, peptide_src_array);
      continue;
    }
    //get peptide_type
    if(peptide_type == -1){
      if(sscanf(new_line,"%d", &peptide_type) != 1){
        carp(CARP_WARNING, "failed to read peptide_type, mass: %.2f", peptide_mass);
      }
      //peptide_type = new_line[1]
      continue;
    }
    //get start_idx
    if(start_idx == -1){
      if(sscanf(new_line,"%d", &start_idx) != 1){
        carp(CARP_WARNING, "failed to read start_idx, mass: %.2f", peptide_mass);
      }
      //start_idx = new_line[1]
      continue;
    }
    //get protein idx
    if(protein_idx == -1){
      if(sscanf(new_line,"%d", &protein_idx) != 1){
        carp(CARP_WARNING, "failed to read protein_idx, mass: %.2f", peptide_mass);
      }
      //protein_idx = new_line[1]
    }
    
    //get the petide src parent protein
    parent_protein = 
      get_database_protein_at_idx(database, protein_idx);
    
    //set the peptide src with the correct values
    set_peptide_src_array( peptide_src_array, current_src-1, peptide_type, parent_protein, start_idx);
    
    //update current_src
    ++current_src;
  
    //check if there's anymore peptide src to parse
    if(current_src > num_src){
      break;
    }
    //reset variables
    peptide_type = -1;
    start_idx = -1;
    protein_idx = -1;
  }
  //set new peptide in interator
  //cast peptide iterator to correct type
  if(index_type == DB_INDEX){
    peptide_iterator_db->peptide = peptide;
  }
  else if(index_type == BIN_INDEX){
    peptide_iterator_bin->peptide = peptide;
  }
  
  free(new_line);
  return TRUE;   
}

/**
 * fast forward the index file pointer to the beginning of the
 * first peptide that would meet the peptide constraint,
 * parse the peptide, then adds it to the index-peptide-iterator to return
 * \returns TRUE if successfully finds and parses a peptide that meets the constraints
 */
BOOLEAN_T fast_forward_index_file(
  INDEX_PEPTIDE_ITERATOR_T* index_peptide_iterator, ///< working index_peptide_iterator -in/out
  FILE* file ///< the file stream to fast foward -in
  )
{
  //used to parse each line from file
  char* new_line = NULL;
  int line_length;
  size_t buf_length = 0;
  char temp_string[5] = "";
  BOOLEAN_T in_peptide = FALSE;
  BOOLEAN_T is_mass_ok = FALSE;

  //peptide fields
  int peptide_length;
  float peptide_mass;

  //parse each line
  while((line_length =  getline(&new_line, &buf_length, file)) != -1){
    
    //find start of peptide
    if(!in_peptide){
      if(new_line[0] != '*'){
        //skip all lines until the start of peptide sign '*'
        continue;
      }
      else{
        in_peptide = TRUE;
      }
    }
    
    //with in peptide fields
    if(in_peptide){
      
      //check peptide mass
      if(!is_mass_ok){
        //parse peptide mass
        if(sscanf(new_line,"%s %f", 
                  temp_string, &peptide_mass) < 2){
          die("crux_index incorrect file format");
          return FALSE;
        }
        //check peptide mass larger than peptide constraint, break no more peptides to return
        if(peptide_mass > get_peptide_constraint_max_mass(index_peptide_iterator->index->constraint)){
          in_peptide = FALSE;
          break;
        }
        //check peptide mass larger than peptide constraint, continue to next peptide
        else if(peptide_mass < get_peptide_constraint_min_mass(index_peptide_iterator->index->constraint)){
          in_peptide = FALSE;
          continue;
        }
        else{
          is_mass_ok = TRUE;
          continue;
        }
      }
      
      //check length
      if(is_mass_ok){
        //read in the peptide length
        if(sscanf(new_line,"%d", &peptide_length) != 1){
          die("crux_index incorrect file format");
        }
        //check peptide mass within peptide constraint
        if(peptide_length > get_peptide_constraint_max_length(index_peptide_iterator->index->constraint) ||
           peptide_length < get_peptide_constraint_min_length(index_peptide_iterator->index->constraint)) {
          in_peptide = FALSE;
          is_mass_ok = FALSE;
          continue;
        }
        else{
          free(new_line);
          //set file pointer
          index_peptide_iterator->index_file = file;

          //parse the peptide, adds it to the iterator to return
          if(!parse_peptide_index_file(index_peptide_iterator, peptide_mass, peptide_length, DB_INDEX)){
            carp(CARP_WARNING, "failed to parse peptide, mass: %.2f, length: %d", 
                 peptide_mass, peptide_length);
            return FALSE;
          }
          
          //found a peptide that meets constraint, and successfully parsed
          return TRUE;
        }        
      }
    }
  }
  //there's no petide that fits the peptide constraint
  free(new_line);
  return FALSE;
}

/**
 * \returns TRUE if successfully initiallized the index_peptide_iterator
 */
BOOLEAN_T initialize_index_peptide_iterator(
  INDEX_PEPTIDE_ITERATOR_T* index_peptide_iterator ///< the index_peptide_iterator to initialize -in
  )
{
  FILE* file = NULL;
  char* filename = NULL;
  index_peptide_iterator->has_next = FALSE;

  do{
    //no more index_files to search
    if(index_peptide_iterator->current_index_file >= index_peptide_iterator->total_index_files){
      if(file != NULL){
        fclose(file);
      }
      return TRUE;
    }
    
    filename = 
      index_peptide_iterator->index_files[index_peptide_iterator->current_index_file]->filename;
    
    //update current index file
    ++index_peptide_iterator->current_index_file;
    
    //close previous file
    if(file != NULL){
      fclose(file);
    }

    //open next index file
    file = fopen(filename, "r");
    if(file == NULL){
      carp(CARP_WARNING, "cannot open %s file", filename);
      return FALSE;
    }
  } 
  //move file* to the begining of the first peptide that meets the constraint
  while(!fast_forward_index_file(index_peptide_iterator, file));
  
  //set interator to TRUE, yes there's a next peptide to return
  index_peptide_iterator->has_next = TRUE;
  return TRUE;
}


/**
 * \returns TRUE if successfully setup the index_peptide_iterator for the next iteration
 */
BOOLEAN_T setup_index_peptide_iterator(
  INDEX_PEPTIDE_ITERATOR_T* index_peptide_iterator ///< the index_peptide_iterator to initialize -in
  )
{
  
  FILE* file = index_peptide_iterator->index_file;
  char* filename = NULL;
  index_peptide_iterator->has_next = FALSE;

  //move file* to the begining of the first peptide that meets the constraint
  while(!fast_forward_index_file(index_peptide_iterator, file)){
    
    //no more index_files to search
    if(index_peptide_iterator->current_index_file >= index_peptide_iterator->total_index_files){
      fclose(file);
      return TRUE;
    }
    
    filename = 
      index_peptide_iterator->index_files[index_peptide_iterator->current_index_file]->filename;
    
    //update current index file
    ++index_peptide_iterator->current_index_file;

    //open index file
    fclose(file);
    file = fopen(filename, "r");
    if(file == NULL){
      carp(CARP_WARNING, "cannot open %s file", filename);
      return FALSE;
    }
  }

  //successfully parse peptide
  index_peptide_iterator->has_next = TRUE;
  return TRUE;
}

/***********************************************
 *  The basic index_peptide_iterator functions.
 ***********************************************/

/**
 * Instantiates a new index_peptide_iterator from a index.
 * \returns a new heap allocated index_peptide_iterator object
 */
INDEX_PEPTIDE_ITERATOR_T* new_index_peptide_iterator(
  INDEX_T* index ///< The index object which we are iterating over -in
  )
{
  //allocate a new index_peptide_iterator object
  INDEX_PEPTIDE_ITERATOR_T* index_peptide_iterator =
    (INDEX_PEPTIDE_ITERATOR_T*)mycalloc(1, sizeof(INDEX_PEPTIDE_ITERATOR_T));
  
  //set index
  index_peptide_iterator->index = index;
  
  //parse index_files that are with in peptide_constraint from crux_index_map
  if(!parse_crux_index_map( index_peptide_iterator)){
    //failed to parse crux_index_map
    free_index(index_peptide_iterator->index);
    free_index_peptide_iterator(index_peptide_iterator);
    die("failed to parse crux_index_map file");
  }

  // if no index files to parse, then there's no peptides to return
  // initialize index_file stream at the first new peptide
  if(index_peptide_iterator->total_index_files == 0 ||
     !initialize_index_peptide_iterator(index_peptide_iterator)){
    //no peptides to return
    index_peptide_iterator->has_next = FALSE;
  }
  
  return index_peptide_iterator;
}

/**
 *  The basic iterator functions.
 * \returns The next peptide in the index.
 */
PEPTIDE_T* index_peptide_iterator_next(
  INDEX_PEPTIDE_ITERATOR_T* index_peptide_iterator ///< the index_peptide_iterator to initialize -in
  )
{
  PEPTIDE_T* peptide_to_return = index_peptide_iterator->peptide;

  //check if there's actually a peptide to return
  if(!index_peptide_iterator_has_next(index_peptide_iterator) ||
     index_peptide_iterator->peptide == NULL){
    die("index_peptide_iterator, no peptides to return");
  }
  
  //setup the interator for the next peptide, if avaliable
  if(!setup_index_peptide_iterator(index_peptide_iterator)){
    die("failed to setup index_peptide_iterator for next iteration");
  }
 
  return peptide_to_return;
}

/**
 * The basic iterator functions.
 * check to see if the index_peptide_iterator has more peptides to return
 *\returns TRUE if there are additional peptides to iterate over, FALSE if not.
 */
BOOLEAN_T index_peptide_iterator_has_next(
  INDEX_PEPTIDE_ITERATOR_T* index_peptide_iterator ///< the index_peptide_iterator to initialize -in
  )
{
  return index_peptide_iterator->has_next; 
}

/**
 * Frees an allocated index_peptide_iterator object.
 */
void free_index_peptide_iterator(
  INDEX_PEPTIDE_ITERATOR_T* index_peptide_iterator ///< the iterator to free -in
  )
{
  int file_idx = 0;
  
  //free all index files
  for(; file_idx < index_peptide_iterator->total_index_files; ++file_idx){
    free_index_file(index_peptide_iterator->index_files[file_idx]);
  }
  
  //if did not iterate over all peptides, free the last peptide not returned
  if(index_peptide_iterator_has_next(index_peptide_iterator)){
    free_peptide_for_array(index_peptide_iterator->peptide);
  }
  free(index_peptide_iterator);
}

/***********************************************
 * index_filtered_peptide_iterator
 ***********************************************/

/**
 * sets up the index_filered_peptide_iterator
 * \returns TRUE if successfully sets up the iterator, else FALSE
 */
BOOLEAN_T setup_index_filtered_peptide_iterator(
  INDEX_FILTERED_PEPTIDE_ITERATOR_T* iterator
  )
{
  PEPTIDE_T* peptide = NULL;
  PEPTIDE_SRC_T* src = NULL;
  PEPTIDE_TYPE_T peptide_type = 
    get_peptide_constraint_peptide_type(iterator->index_peptide_iterator->index->constraint);
  BOOLEAN_T match = FALSE;

  //initialize index_filered
  while(index_peptide_iterator_has_next(iterator->index_peptide_iterator)){
    peptide = index_peptide_iterator_next(iterator->index_peptide_iterator);
    src = get_peptide_peptide_src(peptide);
    //mass, length has been already checked in index_peptide_iterator
    //check if peptide type matches the constraint
    while(src != NULL){
      if(get_peptide_src_peptide_type(src) == peptide_type){
        match = TRUE;
        break;
      }
      //check the next peptide src
      src = get_peptide_src_next_association(src);
    }

    //add more filters to the peptides here, if they don't meet
    // requirements change 'match' to FALSE 

    //this peptide meets the peptide_type
    if(match){
      iterator->peptide = peptide;
      iterator->has_next = TRUE;
      return TRUE;
    }
    free_peptide_for_array(peptide);
  }
  //no peptides meet the constraint
  iterator->has_next = FALSE;
  return TRUE;
}

/**
 * Instantiates a new index_filtered_peptide_iterator from a index.
 * \returns a new heap allocated index_filtered_peptide_iterator object
 */
INDEX_FILTERED_PEPTIDE_ITERATOR_T* new_index_filtered_peptide_iterator(
  INDEX_T* index ///< The index object which we are iterating over -in
  )
{
  //create new index_filtered_peptide_iterator
  INDEX_FILTERED_PEPTIDE_ITERATOR_T* index_filtered_iterator =
    (INDEX_FILTERED_PEPTIDE_ITERATOR_T*)mycalloc(1, sizeof(INDEX_FILTERED_PEPTIDE_ITERATOR_T));

 //create new index peptide iterator, the core peptide iterator
  index_filtered_iterator->index_peptide_iterator = new_index_peptide_iterator(index);
  
  //setup index_filered_iterator
  if(!setup_index_filtered_peptide_iterator(index_filtered_iterator)){
    carp(CARP_ERROR, "Failed to setup index filtered peptide iterator");
    exit(1);
  }
  
  return index_filtered_iterator;
}

/**
 *  The basic iterator functions.
 * \returns The next peptide in the index.
 */
PEPTIDE_T* index_filtered_peptide_iterator_next(
  INDEX_FILTERED_PEPTIDE_ITERATOR_T* index_filtered_peptide_iterator ///< the index_filtered_peptide_iterator to initialize -in
  )
{
  PEPTIDE_T* peptide_to_return = index_filtered_peptide_iterator->peptide;

  //check if there's actually a peptide to return
  if(!index_filtered_peptide_iterator_has_next(index_filtered_peptide_iterator) ||
     index_filtered_peptide_iterator->peptide == NULL){
    die("index_filtered_peptide_iterator, no peptides to return");
  }
  
  //setup the interator for the next peptide, if avaliable
  if(!setup_index_filtered_peptide_iterator(index_filtered_peptide_iterator)){
    die("failed to setup index_filtered_peptide_iterator for next iteration");
  }
  return peptide_to_return;
}

/**
 * The basic iterator functions.
 * check to see if the index_filtered_peptide_iterator has more peptides to return
 *\returns TRUE if there are additional peptides to iterate over, FALSE if not.
 */
BOOLEAN_T index_filtered_peptide_iterator_has_next(
  INDEX_FILTERED_PEPTIDE_ITERATOR_T* index_filtered_peptide_iterator ///< the index_filtered_peptide_iterator to initialize -in
  )
{
  return index_filtered_peptide_iterator->has_next; 
}

/**
 * Frees an allocated index_filtered_peptide_iterator object.
 */
void free_index_filtered_peptide_iterator(
  INDEX_FILTERED_PEPTIDE_ITERATOR_T* index_filtered_peptide_iterator ///< the iterator to free -in
  )
{
  free_index_peptide_iterator(index_filtered_peptide_iterator->index_peptide_iterator);
    
  //if did not iterate over all peptides, free the last peptide not returned
  if(index_filtered_peptide_iterator_has_next(index_filtered_peptide_iterator)){
    free_peptide_for_array(index_filtered_peptide_iterator->peptide);
  }
  free(index_filtered_peptide_iterator);
}

/****************************
 * bin_peptide_iterator
 ******************************/

/**
 * reads in the first peptide from the bin
 * parse the peptide, then adds it to the bin-peptide-iterator to return
 * \returns TRUE if successfully initializes the bin_peptide_iterator
 */
BOOLEAN_T initialize_bin_peptide_iterator(
  BIN_PEPTIDE_ITERATOR_T* bin_peptide_iterator ///< working bin_peptide_iterator -in/out
  )
{
  //used to parse each line from file
  char* new_line = NULL;
  int line_length;
  size_t buf_length = 0;
  char temp_string[5] = "";
  BOOLEAN_T in_peptide = FALSE;
  BOOLEAN_T is_mass_ok = FALSE;
  FILE* file = bin_peptide_iterator->index_file;
    
  //peptide fields
  int peptide_length;
  float peptide_mass;

 
  //parse each line
  while((line_length =  getline(&new_line, &buf_length, file)) != -1){
    
    //find start of peptide
    if(!in_peptide){
      if(new_line[0] != '*'){
        //skip all lines until the start of peptide sign '*'
        continue;
      }
      else{
        in_peptide = TRUE;
      }
    }
    
    //with in peptide fields
    if(in_peptide){
      
      //read peptide mass
      if(!is_mass_ok){
        //parse peptide mass
        if(sscanf(new_line,"%s %f", 
                  temp_string, &peptide_mass) < 2){
          die("crux_index incorrect file format");
          return FALSE;
        }
        is_mass_ok = TRUE;
        continue;
      }
      
      //read length
      if(is_mass_ok){
        //read in the peptide length
        if(sscanf(new_line,"%d", &peptide_length) != 1){
          die("crux_index incorrect file format");
        }
        
        free(new_line);
        //set file pointer
        bin_peptide_iterator->index_file = file;
        
        //parse the peptide, adds it to the iterator to return
        if(!parse_peptide_index_file(bin_peptide_iterator, peptide_mass, peptide_length, BIN_INDEX)){
          carp(CARP_WARNING, "failed to parse peptide, mass: %.2f, length: %d", 
               peptide_mass, peptide_length);
          return FALSE;
        }
        else{
          bin_peptide_iterator->has_next = TRUE;
          return TRUE;
        }
      }
    }
  }
  
  bin_peptide_iterator->has_next = FALSE;
  free(new_line);
  return TRUE;
}

/**
 * Instantiates a new bin_peptide_iterator from a gvien bin file handler.
 * \returns a new heap allocated bin_peptide_iterator object
 */
BIN_PEPTIDE_ITERATOR_T* new_bin_peptide_iterator(
  INDEX_T* index, ///< The index object which we are iterating over -in
  FILE* file
  )
{
  //allocate a new index_peptide_iterator object
  BIN_PEPTIDE_ITERATOR_T* bin_peptide_iterator =
    (BIN_PEPTIDE_ITERATOR_T*)mycalloc(1, sizeof(BIN_PEPTIDE_ITERATOR_T));
  
  //set index, file
  bin_peptide_iterator->index = index;
  bin_peptide_iterator->index_file = file;
    
  if(!initialize_bin_peptide_iterator(bin_peptide_iterator)){
    carp(CARP_WARNING, "failed to initalize bin peptide iterator");
    bin_peptide_iterator->has_next = FALSE;
  }
    
  return bin_peptide_iterator;
}


/**
 *  The basic iterator functions.
 * \returns The next peptide in the index.
 */
PEPTIDE_T* bin_peptide_iterator_next(
  BIN_PEPTIDE_ITERATOR_T* bin_peptide_iterator ///< the bin_peptide_iterator to get peptide -in
  )
{
  PEPTIDE_T* peptide_to_return = bin_peptide_iterator->peptide;

  //check if there's actually a peptide to return
  if(!bin_peptide_iterator_has_next(bin_peptide_iterator) ||
     bin_peptide_iterator->peptide == NULL){
    die("bin_peptide_iterator, no peptides to return");
  }
  
  //setup the interator for the next peptide, if avaliable
  if(!initialize_bin_peptide_iterator(bin_peptide_iterator)){
    die("failed to setup bin_peptide_iterator for next iteration");
  }
 
  return peptide_to_return;
}

/**
 * The basic iterator functions.
 * check to see if the bin_peptide_iterator has more peptides to return
 *\returns TRUE if there are additional peptides to iterate over, FALSE if not.
 */
BOOLEAN_T bin_peptide_iterator_has_next(
  BIN_PEPTIDE_ITERATOR_T* bin_peptide_iterator ///< the bin_peptide_iterator to initialize -in
  )
{
  return bin_peptide_iterator->has_next; 
}

/**
 * Frees an allocated bin_peptide_iterator object.
 */
void free_bin_peptide_iterator(
  BIN_PEPTIDE_ITERATOR_T* bin_peptide_iterator ///< the iterator to free -in
  )
{
  
  //if did not iterate over all peptides, free the last peptide not returned
  if(bin_peptide_iterator_has_next(bin_peptide_iterator)){
    free_peptide(bin_peptide_iterator->peptide);
  }
  free(bin_peptide_iterator);
}

/******************************
 * bin_sorted_peptide_iterator
 ******************************/

/**
 * Instantiates a new sorted_bin_peptide_iterator from a gvien bin file handler.
 * \returns a new heap allocated sorted_bin_peptide_iterator object
 */
BIN_SORTED_PEPTIDE_ITERATOR_T* new_bin_sorted_peptide_iterator(
  INDEX_T* index, ///< The index object which we are iterating over -in
  FILE* file ///< the working bin file handler -in
  )
{
  //create database sorted peptide iterator
  BIN_SORTED_PEPTIDE_ITERATOR_T* bin_sorted_peptide_iterator =
    (BIN_SORTED_PEPTIDE_ITERATOR_T*)mycalloc(1, sizeof(BIN_SORTED_PEPTIDE_ITERATOR_T));

  //reset file to start
  rewind(file);

  //create bin_peptide_iterator
  BIN_PEPTIDE_ITERATOR_T* bin_peptide_iterator =
    new_bin_peptide_iterator(index, file);

  //create a sorted peptide iterator that will sort all the peptides from bin peptide_iterator
  SORTED_PEPTIDE_ITERATOR_T* sorted_peptide_iterator =
    new_sorted_peptide_iterator_bin(bin_peptide_iterator, MASS, TRUE);

  //set sorted_peptide_iterator
  bin_sorted_peptide_iterator->sorted_peptide_iterator = sorted_peptide_iterator;
  
  free_bin_peptide_iterator(bin_peptide_iterator); //CHECK ME might wanna check this...

  return bin_sorted_peptide_iterator;
}

/**
 *  The basic iterator functions.
 * \returns The next peptide in the index.
 */
PEPTIDE_T* bin_sorted_peptide_iterator_next(
  BIN_SORTED_PEPTIDE_ITERATOR_T* bin_sorted_peptide_iterator ///< the bin_peptide_iterator to get peptide -in
  )
{
  return sorted_peptide_iterator_next(bin_sorted_peptide_iterator->sorted_peptide_iterator);
}

/**
 * The basic iterator functions.
 * check to see if the bin_sorted_peptide_iterator has more peptides to return
 *\returns TRUE if there are additional peptides to iterate over, FALSE if not.
 */
BOOLEAN_T bin_sorted_peptide_iterator_has_next(
  BIN_SORTED_PEPTIDE_ITERATOR_T* bin_sorted_peptide_iterator ///< the bin_peptide_iterator to initialize -in
  )
{
  return sorted_peptide_iterator_has_next(bin_sorted_peptide_iterator->sorted_peptide_iterator);
}

/**
 * Frees an allocated bin_peptide_iterator object.
 */
void free_bin_sorted_peptide_iterator(
  BIN_SORTED_PEPTIDE_ITERATOR_T* bin_sorted_peptide_iterator ///< the iterator to free -in
  )
{
  free_sorted_peptide_iterator(bin_sorted_peptide_iterator->sorted_peptide_iterator);
  free(bin_sorted_peptide_iterator);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 2
 * End:
 */
