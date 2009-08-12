/*******************************************************************************
 * Percolator unofficial version
 * Copyright (c) 2006-8 University of Washington. All rights reserved.
 * Written by Lukas K�ll (lukall@u.washington.edu) in the 
 * Department of Genome Science at the University of Washington. 
 *
 * $Id: SanityCheck.cpp,v 1.6 2008/06/20 23:55:35 lukall Exp $
 *******************************************************************************/
#include <string>
#include <fstream>
#include <iostream>
using namespace std;
#include "DataSet.h"
#include "Scores.h"
#include "Normalizer.h"
#include "SanityCheck.h"
#include "Globals.h"

namespace qranker {

// Class doing the sanity check for non SQT file condition
// In most sence a place holder as very little logic is build-in in this case

SanityCheck::SanityCheck() : initPositives(0),pTestset(NULL),pTrainset(NULL)
{
}

SanityCheck::~SanityCheck()
{
}

bool SanityCheck::overRule=false;
string SanityCheck::initWeightFN="";

int SanityCheck::getInitDirection(vector<Scores>& testset, vector<Scores>& trainset,Normalizer * pNorm, vector<vector<double> >& w,double test_fdr) {
  pTestset = &testset; pTrainset = &trainset; fdr = test_fdr;
  
  if (initWeightFN.size()>0) {
    vector<double> ww(FeatureNames::getNumFeatures()+1);
    ifstream weightStream(initWeightFN.data(),ios::in);
    readWeights(weightStream,ww);
    weightStream.close();
    pNorm->normalizeweight(ww,w[0]); 
    for (size_t set = 1; set<w.size();++set)
       copy(w[0].begin(),w[0].end(),w[set].begin());
  } else {
    getDefaultDirection(w);
  }
  initPositives = 0;
  for (size_t set = 0; set<w.size();++set)
    initPositives += (*pTestset)[set].getInitDirection(fdr,w[set],false);
  return initPositives;
}

void SanityCheck::getDefaultDirection(vector< vector<double> >& w) {
  // Set init direction to be the most discriminative direction
  for (size_t set = 0; set<w.size();++set)
    (*pTrainset)[set].getInitDirection(fdr,w[set],true);
}


bool SanityCheck::validateDirection(vector<vector<double> >& w) {
  if (!pTestset) {
    cerr << "Wrongly set up of object SanityCheck" << endl;
    exit(-1);
  }
  int overFDR=0;
  for (size_t set = 0; set<w.size();++set)
    overFDR += (*pTestset)[set].calcScores(w[set],fdr);
  if (VERB>0) cerr << "Found " << overFDR << " peptides scoring over " << fdr*100 << "% FDR level on testset" << endl;
  if (overFDR<=0) {
     cerr << "No target score better than best decoy" << endl;
     resetDirection(w);
     return false;
  }
  if (initPositives>=overFDR) {
     cerr << "Less identifications after percolator processing than before processing" << endl;
     resetDirection(w);
     return false;
  }
  return true;
}

void SanityCheck::readWeights(istream & weightStream, vector<double>& w) {
  char buffer[1024],c;
  while (!(((c = weightStream.get())== '-') || (c >= '0' && c <= '9'))) {
    weightStream.getline(buffer,1024);
  }
  weightStream.getline(buffer,1024);
// Get second line containing raw features
  for(unsigned int ix=0;ix<FeatureNames::getNumFeatures()+1;ix++) {
    weightStream >> w[ix];
  }
}

void SanityCheck::resetDirection(vector<vector<double> >& w) {
  if (!overRule) {
    cerr << "Reseting score vector, using default vector" << endl;
    getDefaultDirection(w);  
  }
}

} // qranker namspace

