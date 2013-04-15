/*******************************************************************************
 * Percolator v 1.05
 * Copyright (c) 2006-8 University of Washington. All rights reserved.
 * Written by Lukas K�ll (lukall@u.washington.edu) in the 
 * Department of Genome Science at the University of Washington. 
 *
 * $Id: UniNormalizer.cpp,v 1.12.8.1 2008/05/27 20:29:31 lukall Exp $
 *******************************************************************************/
#include <set>
#include <vector>
#include <iostream>
#include <math.h>
using namespace std;
#include "DataSet.h"
#include "Normalizer.h"
#include "UniNormalizer.h"
#include "SetHandler.h"

UniNormalizer::UniNormalizer()
{
  sub.resize(DataSet::getNumFeatures(),0.0);
  div.resize(DataSet::getNumFeatures(),0.0);
}

UniNormalizer::~UniNormalizer()
{
}

void UniNormalizer::normalize(const double *in,double* out){
  for (int ix=0;ix<DataSet::getNumFeatures();ix++) {
  	out[ix]=(in[ix]-sub[ix])/div[ix];
  }
}

void UniNormalizer::unnormalizeweight(const double *in,double* out){
  double sum = 0;
  int i=0;
  for (;i<DataSet::getNumFeatures();i++) {
  	out[i]=in[i]/div[i];
  	sum += sub[i]*in[i]/div[i];
  }
  out[i]=in[i]-sum;
}

void UniNormalizer::normalizeweight(const double *in,double* out){
  double sum = 0;
  int i=0;
  for (;i<DataSet::getNumFeatures();i++) {
  	out[i]=in[i]*div[i];
  	sum+=sub[i]*in[i];
  }
  out[i]=in[i]+sum;
}

void UniNormalizer::setSet(set<DataSet *> &setVec){
  const double * features;
  int ix;
  vector<double> mins(DataSet::getNumFeatures(),1e+100);
  vector<double> maxs(DataSet::getNumFeatures(),-1e+100);
  set<DataSet *>::iterator it;
  for (it=setVec.begin();it!=setVec.end();++it) {
    int ixPos=-1;
    while((features=(*it)->getNext(ixPos))!=NULL) {
	  for (ix=0;ix<DataSet::getNumFeatures();ix++) {
	    mins[ix]=min(features[ix],mins[ix]);
	    maxs[ix]=max(features[ix],maxs[ix]);
      }
	}
  }
  for (ix=0;ix<DataSet::getNumFeatures();ix++) {
  	sub[ix]=mins[ix];
  	div[ix]=maxs[ix]-mins[ix];
  	if (div[ix]<=0)
  	  div[ix]=1.0;
  }
}
