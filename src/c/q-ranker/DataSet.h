/*******************************************************************************
 * Percolator unofficial version
 * Copyright (c) 2006-8 University of Washington. All rights reserved.
 * Written by Lukas K�ll (lukall@u.washington.edu) in the 
 * Department of Genome Science at the University of Washington. 
 *
 * $Id: DataSet.h,v 1.53.2.2 2008/08/11 18:31:12 jasonw Exp $
 *******************************************************************************/
#ifndef DATASET_H_
#define DATASET_H_
#include <string>
#include <set>
#include <map>
#include <vector>
#include <iostream>
#include "PSMDescription.h"
#include "FeatureNames.h"
using namespace std;

using namespace qranker;
namespace qranker {

class Scores;
class Normalizer;
class ResultHolder;
class IntraSetRelation;
class NeuralNet;

typedef enum {NO_ENZYME,TRYPSIN,CHYMOTRYPSIN,ELASTASE} Enzyme;


class DataSet
{
 public:
	DataSet();
	virtual ~DataSet();
    void inline setLabel(int l) {label=l;}
    void computeIntraSetFeatures();
    void computeIntraSetFeatures(double *feat,string &pep,set<string> &prots);
    void computeAAFrequencies(const string& pep, double *feat);
    void readSQT(const string fname,IntraSetRelation * intrarel,const string & wild="", bool match=false);
    void modifySQT(const string & outFN, Scores * pSc ,const string greet, bool dtaSelect);
    void initFeatureTables(const unsigned int numFeatures, const unsigned int numSpectra, bool regresionTable = false);
//    static inline int getNumFeatures() { return numFeatures; }
    static FeatureNames& getFeatureNames() { return featureNames; }
    static void setQuadraticFeatures(bool on) { calcQuadraticFeatures=on; }
    static void setCalcDoc(bool on) { calcDOC=on; }
    static void setCalcIntraSetFeatures(bool on) { calcIntraSetFeatures=on; }
    static void setEnzyme(Enzyme enz) { enzyme=enz; }
    static void setAAFreqencies(bool on) { calcAAFrequencies=on; }
    static void setPTMfeature(bool on) { calcPTMs=on; }      
    static void setIsotopeMass(bool on) { isotopeMass=on; }      
    static void setNumFeatures(bool doc);
    static void inline setHitsPerSpectrum(int hits) {hitsPerSpectrum=hits;}
    static inline int rowIx(int row) { return row*FeatureNames::getNumFeatures(); }
    double * getFeature() {return feature;}
    const double * getFeatures(const int pos) const;
    int inline getSize() const {return numSpectra;}
    int inline const getLabel() const {return label;}
    PSMDescription* getNext(int& pos);
    void setRetentionTime(map<int,double>& scan2rt);    
    bool writeTabData(ofstream & out, const string & lab);
    void readTabData(ifstream & dataStream, const vector<unsigned int> &ixs);
    bool getGistDataRow(int& pos,string & out);
    void readGistData(ifstream & is, const vector<unsigned int> &ixs);
    void print_10features();
    void print_features();
    void print(Scores& test, vector<ResultHolder> &outList);

protected:
    void readFeatures(const string &in,PSMDescription &psm,int match,bool getIntra);
    string modifyRec(const string record, int& row, const set<int>& theMs, Scores * pSc, bool dtaSelect);
    static unsigned int peptideLength(const string& pep);
    static unsigned int cntPTMs(const string& pep);
    static unsigned int cntEnz(const string& peptide);
    static double isTryptic(const char n,const char c);
    static double isChymoTryptic(const char n,const char c);
    static double isElastasic(const char n,const char c);
    static double isEnz(const char n,const char c); 
    vector<string> ids;
    static bool calcQuadraticFeatures;
    static bool calcAAFrequencies;
    static Enzyme enzyme;
    static bool calcIntraSetFeatures;
    static bool calcPTMs;
    static bool calcDOC;
    static bool isotopeMass;
//    static int numFeatures;
//    static int numRealFeatures;
    static int hitsPerSpectrum;
    static string aaAlphabet;
    static string ptmAlphabet;
    const static int maxNumRealFeatures = 16 + 3 + 20*3 + 1 + 1 + 3; // Normal + Amino acid + PTM + hitsPerSpectrum + doc
//    vector<set<string> > proteinIds;
//    vector<string> pepSeq;
    vector<PSMDescription> psms;
    int label;
    double *feature,*regressionFeature;
    int numSpectra;
    string sqtFN;
    string pattern;
    string fileId;
    bool doPattern;
    bool matchPattern;
    IntraSetRelation * intra;
    static FeatureNames featureNames;
};

} // qranker namspace

#endif /*DATASET_H_*/
