#include <cstdlib>
#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <cstdlib>
using namespace std;
#include "DataSet.h"
#include "Scores.h"
#include "SetHandler.h"
#include "Caller.h"
#include "Globals.h"
#include "PercolatorCInterface.h"

static Caller *pCaller = NULL;
static NSet nset = FOUR_SETS;
static unsigned int numFeatures = 0;
static SetHandler::Iterator * normal = NULL;
static SetHandler::Iterator * decoy1 = NULL;
static SetHandler::Iterator * decoy2 = NULL;
static SetHandler::Iterator * decoy3 = NULL;


Caller * getCaller() {
    if (pCaller==NULL) {
      cerr << "Object pCaller not properly assigned" << endl;
      exit(-1);
    }
    return pCaller;
} 


/** Call that initiates percolator */
void pcInitiate(NSet sets, unsigned int numFeat, unsigned int numSpectra, char ** featureNames, double pi0) {
    pCaller=new Caller();
    nset=sets;
    numFeatures = numFeat;
    pCaller->filelessSetup((unsigned int) sets, numFeatures, numSpectra, featureNames, pi0);
    normal = new SetHandler::Iterator(pCaller->getSetHandler(Caller::NORMAL));
    decoy1 = new SetHandler::Iterator(pCaller->getSetHandler(Caller::SHUFFLED));
    if (nset>2)
      decoy2 = new SetHandler::Iterator(pCaller->getSetHandler(Caller::SHUFFLED_TEST));
    if (nset>3)
      decoy3 = new SetHandler::Iterator(pCaller->getSetHandler(Caller::SHUFFLED_THRESHOLD));
}

/** Call that sets verbosity level
 *  0 is quiet, 2 is default, 6 is more than you want */
void pcSetVerbosity(int verbosity) {
  if (verbosity<0 || verbosity > 6) {
    cerr << "Wrong value of verbosity, should be between 0 and 6, you tried " << verbosity << endl;
    exit(-1);
  }
  Globals::getInstance()->setVerbose(verbosity);
}


/** Register a PSM */
void pcRegisterPSM(SetType set, char * identifier, double * features) {
  if ((int)set>(int)nset) {
     cerr << "Tried to access undefined set" << endl;
     exit(-1);
  }
  double * vec = NULL;
  switch(set) {
    case TARGET:
      vec = normal->getNext();
      break;
    case DECOY1:
      vec = decoy1->getNext();
      break;
    case DECOY2:
      vec = decoy2->getNext();
      break;
    case DECOY3:
      vec = decoy3->getNext();
      break;
  }
  if (vec==NULL) {
     cerr << "Pointer out of bound" << endl;
     exit(-1);
  }
  for (unsigned int ix=0;ix<numFeatures;ix++) {
    vec[ix] = features[ix];
  }
}

/** Function called when we want to start processing */
void pcExecute() {
  bool separateShuffledTestSetHandler = nset>TWO_SETS;
  bool separateShuffledThresholdSetHandler = nset==FOUR_SETS;
  pCaller->fillFeatureSets(separateShuffledTestSetHandler,separateShuffledThresholdSetHandler);
  C_DARRAY(w,DataSet::getNumFeatures()+1)
  pCaller->preIterationSetup(w);
  pCaller->train(w);  
  pCaller->getTestSet()->calcScores(w);
  D_DARRAY(w)
} 

/** Function called when retrieving target scores and q-values after processing,
  * the array should be numSpectra long and will be filled in the same order
  * as the features were inserted */
void pcGetScores(double *scoreArr,double *qArr) {
  int ix=0;
  SetHandler::Iterator iter(pCaller->getSetHandler(Caller::NORMAL));
  while(double * feat = iter.getNext()) {
    double score = pCaller->getTestSet()->calcScore(feat);
    double q = pCaller->getTestSet()->getQ(score);
    scoreArr[ix] = score;
    qArr[ix++] = q;
  }
} 

/** Function that should be called after processing finished */
void pcCleanUp() {
    if (pCaller) {
      delete pCaller;
      pCaller=NULL;
    }
    if (normal) { delete normal; normal = NULL; }
    if (decoy1) { delete decoy1; decoy1 = NULL; }
    if (decoy2) { delete decoy2; decoy2 = NULL; }
    if (decoy3) { delete decoy3; decoy3 = NULL; }
    
    Globals::clean();
}
