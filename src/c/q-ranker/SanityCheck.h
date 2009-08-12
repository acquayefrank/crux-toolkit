/*******************************************************************************
 * Percolator unofficial version
 * Copyright (c) 2006-8 University of Washington. All rights reserved.
 * Written by Lukas K�ll (lukall@u.washington.edu) in the 
 * Department of Genome Science at the University of Washington. 
 *
 * $Id: SanityCheck.h,v 1.5 2008/06/06 17:13:32 lukall Exp $
 *******************************************************************************/
#ifndef SANITYCHECK_H_
#define SANITYCHECK_H_

class Scores;
class Normalizer;

namespace qranker {

class SanityCheck
{
public:
  SanityCheck();
  virtual ~SanityCheck();
    
  void readWeights(istream & weightStream, vector<double>& w);
  int getInitDirection(vector<Scores>& testset,vector<Scores> &trainset, Normalizer * pNorm,vector<vector<double> >& w,double test_fdr);
  virtual bool validateDirection(vector<vector<double> >& w);
  void resetDirection(vector<vector<double> >& w);

  static void setInitWeightFN(string fn) {initWeightFN=fn;}
  static void setOverrule(bool orl) {overRule=orl;}
protected:
  virtual void getDefaultDirection(vector<vector<double> >& w);
  int initPositives;
  double fdr;
  static bool overRule;
  static string initWeightFN;
  vector<Scores> *pTestset, *pTrainset;
};

} // qranker namspace

#endif /*SANITYCHECK_H_*/
