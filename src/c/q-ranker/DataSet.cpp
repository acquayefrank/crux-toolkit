/*******************************************************************************
 * Percolator unofficial version
 * Copyright (c) 2006-8 University of Washington. All rights reserved.
 * Written by Lukas K�ll (lukall@u.washington.edu) in the 
 * Department of Genome Science at the University of Washington. 
 *
 * $Id: DataSet.cpp,v 1.85.2.2 2008/08/11 18:31:12 jasonw Exp $
 *******************************************************************************/
#include <assert.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#ifdef WIN32
#include <float.h>
#define isfinite _finite
#endif
#include <set>
#include <map>
#include <utility>
#include <vector>
#include <string>
using namespace std;
#include "Net.h"
#include "DataSet.h"
#include "IntraSetRelation.h"
#include "Scores.h"
#include "ResultHolder.h"
#include "Globals.h"

namespace qranker {

//int DataSet::numFeatures = maxNumRealFeatures;
//int DataSet::numRealFeatures = maxNumRealFeatures;
int DataSet::hitsPerSpectrum = 1;
bool DataSet::calcQuadraticFeatures = false;
bool DataSet::calcAAFrequencies = false;
Enzyme DataSet::enzyme = TRYPSIN;
bool DataSet::calcIntraSetFeatures = true;
bool DataSet::calcPTMs = false;
bool DataSet::isotopeMass = false;
bool DataSet::calcDOC = false;
string DataSet::aaAlphabet = "ACDEFGHIKLMNPQRSTVWY";
string DataSet::ptmAlphabet = "#*@";
FeatureNames DataSet::featureNames;


static char buf[4096];


DataSet::DataSet()
{
   feature = NULL;
   regressionFeature = NULL;
   numSpectra=0;
   sqtFN = "";
   pattern="";
   doPattern=false;
   matchPattern=true;
}

DataSet::~DataSet()
{
    if (feature) {
        delete [] feature;
        feature=NULL;
    }
    if (regressionFeature) {
        delete [] regressionFeature;
        regressionFeature=NULL;
    }
}

PSMDescription* DataSet::getNext(int& pos) {
  pos++;
  if (pos<0)
    pos=0;
  if(pos>=getSize())
    return NULL;
  return &psms[pos];
}

const double * DataSet::getFeatures(const int pos) const {
  return &feature[DataSet::rowIx(pos)];
}

bool DataSet::getGistDataRow(int & pos,string &out){
  ostringstream s1;
  PSMDescription* pPSM = NULL;
  if ((pPSM = getNext(pos)) == NULL) return false; 
  double* feature = pPSM->features;
  s1 << ids[pos];
  for (unsigned int ix = 0;ix<FeatureNames::getNumFeatures();ix++) {
    s1 << '\t' << feature[ix];
  }
  s1 << endl;
  out = s1.str();
  return true;
}

bool DataSet::writeTabData(ofstream & out, const string & lab) {
  int pos=-1;
  PSMDescription* pPSM = NULL;
  while ((pPSM = getNext(pos)) != NULL) {
    double* frow = pPSM->features;
    out << ids[pos] << '\t' << lab;
    for (unsigned int ix = 0;ix<FeatureNames::getNumFeatures();ix++) {
      out << '\t' << frow[ix];
    }
    out << "\t" << pPSM->peptide;
    set<string>::const_iterator it = pPSM->proteinIds.begin();
    for(;it!=pPSM->proteinIds.end();it++) {
      out << "\t" << *it;
    }
    out << endl;
  }    
  return true;
}


void DataSet::print_features() {
   for(int i=0;i<getSize();i++) {
       for(unsigned int j=0;j<FeatureNames::getNumFeatures();j++) {
          cerr << j+1 << ":" << feature[DataSet::rowIx(i)+j] << " ";
       }
       cerr << endl;
   }
}

void DataSet::print_10features() {
   cerr << DataSet::getFeatureNames().getFeatureNames() << endl;
   for(int i=0;i<10;i++) {
       for(unsigned int j=0;j<FeatureNames::getNumFeatures();j++) {
          cerr << feature[DataSet::rowIx(i)+j] << "\t";
       }
       cerr << endl;
   }
}

void DataSet::print(Scores& test, vector<ResultHolder> &outList) {
  ostringstream out;
  size_t ix = 0;
  vector<PSMDescription>::const_iterator psm = psms.begin();
  for(;psm!=psms.end();psm++,ix++) {
    double score = test.getScoreHolder(psm->features)->score;
    double q = psm->q;
    set<string> prots = psm->proteinIds;
    set<string>::const_iterator it = psm->proteinIds.begin();
    for(;it!=psm->proteinIds.end();it++) {
      out << "\t" << *it;
    }
    ResultHolder rh(score,q,2.0,ids[ix],psm->peptide,out.str());    
    outList.push_back(rh);
    out.str("");
  }
}

/*
void DataSet::setNumFeatures(bool doc) {
  numRealFeatures= maxNumRealFeatures
                 - (enzyme==NO_ENZYME?3:0) 
                 - (calcPTMs?0:1) 
                 - (hitsPerSpectrum>1?0:1) 
                 - (calcIntraSetFeatures?0:2)
                 - (calcAAFrequencies?0:aaAlphabet.size()*3)
                 - 3; // DOC features
  numFeatures=(calcQuadraticFeatures?numRealFeatures*(numRealFeatures+1)/2:numRealFeatures);
  calcDOC = doc;
  if (doc) {numRealFeatures+=3; numFeatures+=3; }
}
*/

double DataSet::isTryptic(const char n,const char c) {
  return (
  (((n=='K' || n=='R') && c != 'P') ||
  n=='-' || c=='-')
  ?1.0:0.0);
}

// [FHWYLM].[^P]
double DataSet::isChymoTryptic(const char n,const char c) {
  return (
  (((n=='F' || n=='H' || n=='W' || n=='Y' || n=='L' || n=='M') && c!= 'P') ||
  n=='-' || c=='-')
  ?1.0:0.0);
}

// [LVAG].[^P]
double DataSet::isElastasic(const char n,const char c) {
  return (
  (((n=='L' || n=='V' || n=='A' || n=='G' ) && c!= 'P') ||
  n=='-' || c=='-')
  ?1.0:0.0);
}

double DataSet::isEnz(const char n,const char c) {
    switch(enzyme) {
      case TRYPSIN:
        return isTryptic(n,c);
      case CHYMOTRYPSIN:
        return isChymoTryptic(n,c);
      case ELASTASE:
        return isElastasic(n,c);
      case NO_ENZYME:
      default:
        return 0;
    }
}

unsigned int DataSet::cntEnz(const string& peptide) {
    unsigned int pos=2, cnt=0;
    char n = peptide.at(pos++);
    while (pos<peptide.size()-2) {
      char c = peptide.at(pos++);
      if (isEnz(n,c))
        cnt++;
      n=c;
    }
    return cnt;
}

void DataSet::setRetentionTime(map<int,double>& scan2rt) {    
  vector<PSMDescription>::iterator psm = psms.begin();
  if (scan2rt.size() == 0) {
    if (VERB>1) cerr << "Approximating retention time with scan number." << endl;  
    double minRT = (double) psm->scan, diffRT = psms.rbegin()->scan - psm->scan;
    if (diffRT==0.0) diffRT = 1.0; 
    for(; psm != psms.end(); ++psm) {
      psm->retentionTime = 2.*((double) psm->scan - minRT)/diffRT-.5;
    }
  } else {
    double minRT = scan2rt[0], diffRT = scan2rt[scan2rt.size()-1] - scan2rt[0];    
    if (diffRT==0.0) diffRT = 1.0;
    for(; psm != psms.end(); ++psm) {
      assert(scan2rt.count(psm->scan)>0); 
      psm->retentionTime = 2.*(scan2rt[psm->scan] - minRT)/diffRT - .5;
    }  
  }
}

void DataSet::readGistData(ifstream & is, const vector<unsigned int>& ixs) {
  string tmp,line;
  is.clear();
  is.seekg(0,ios::beg);
  getline(is,line);
  getline(is,line);
  unsigned int m=0,n=ixs.size();
  istringstream buff(line);
  while(true) {
    buff >> tmp;
    if (!buff) break;
    m++;
  }
  if (m<3) {
    cerr << "To few features in Gist data file";
    exit(-1);
  }
  m--; // remove id line

  initFeatureTables(m,n);
  string seq;

  is.clear();
  is.seekg(0,ios::beg);
  getline(is,line); // id line

  unsigned int ix = 0;
  getline(is,line);
  for(unsigned int i=0;i<n;i++) {
    while (ix<ixs[i]){
    	getline(is,line);
    	ix++;
    }
    buff.str(line);
    buff.clear();
    buff >> ids[i];
    double *featureRow=&feature[rowIx(i)];
    psms[i].features = featureRow;
    for(register unsigned int j=0;j<m;j++) {
      buff >> featureRow[j];
    } 
  } 
}

void DataSet::readTabData(ifstream & is, const vector<unsigned int>& ixs) {
  string tmp,line;
  is.clear();
  is.seekg(0,ios::beg);
  getline(is,line);
  getline(is,line);
  unsigned int m=0,n=ixs.size();
  istringstream buff(line);
  double a;
  buff >> tmp >> tmp; // remove id and label
  while(true) {
    buff >> a;
    if (buff.good()) {
      m++;
    } else {
      buff >> tmp;
    }
    if (!buff) break;
    buff.clear();
  }
  if (m<1) {
    cerr << "To few features in Tab data file";
    exit(-1);
  }
  initFeatureTables(m,n);

  string seq;

  is.clear();
  is.seekg(0,ios::beg);
  getline(is,line); // id line

  unsigned int ix = 0;
  getline(is,line);
  for(unsigned int i=0;i<n;i++) {
    while (ix<ixs[i]){
        getline(is,line);
        ix++;
    }
    buff.str(line);
    buff.clear();
    buff >> ids[i];
    buff >> tmp; // get rid of label
    double *featureRow=&feature[rowIx(i)];
    psms[i].features = featureRow;
    for(register unsigned int j=0;j<m;j++) {
      buff >> featureRow[j];
    }
    buff >> psms[i].peptide;
    while(!!buff) {
      buff >> tmp;
      if (tmp.size()>0) {
        psms[i].proteinIds.insert(tmp);
      }
    } 
  } 
}


string DataSet::modifyRec(const string record, int& row, const set<int>& theMs, Scores * pSc, bool dtaSelect) {
  vector<pair<double,string> > outputs;
  outputs.clear();
  istringstream in(record);
  ostringstream out,outtmp;
  string line,tmp,lineRem,id;
  getline(in,line);
  out << line << endl;
  PSMDescription psmTmp;
  psmTmp.features = new double[FeatureNames::getNumFeatures()];
  double rSp,mass;
  set<int>::const_iterator it;
  for(it=theMs.begin();it!=theMs.end();it++) {
    psmTmp.clear();
    readFeatures(record,psmTmp,*it,true);
    PSMDescription psm = psms[row++];
    in >> tmp >> tmp >> rSp >> mass;
//    outtmp << "M\t%i\t" << rSp << "\t" << mass << "\t";
    outtmp << "M\t" << tmp << "\t" << rSp << "\t" << mass << "\t";
    // deltCn and XCorr and Sp
    in >> tmp >> tmp >> tmp;
    outtmp << "%6.4g\t" << 1.0-psm.pep << "\t" << -psm.q;
    getline(in,lineRem);
    outtmp << lineRem << endl;
    // L lines
    while(in.peek()=='L' && getline(in,line)) {      
      assert(line[0]=='L');
      outtmp << line << endl;
    }
    outputs.push_back(pair<double,string>(1-psm.pep,outtmp.str()));
    outtmp.clear();
    outtmp.str("");
  }
  if(dtaSelect) {
    outputs.push_back(pair<double,string>(-1000,
      "M\t600\t600\t1\t%6.4g\t-1000\t-1\t0\t0\tI.AMINVALI.D\tU\nL\tPlaceholder satisfying DTASelect\n"));
  }
  sort(outputs.begin(),outputs.end());
  reverse(outputs.begin(),outputs.end());
  double x0=0,delt;
  for(unsigned int ix=0;ix<outputs.size();ix++) {
    string aStr = outputs[ix].second;
    double score = outputs[ix].first;
    if (ix==0) x0=score;
    if (x0 != 0) {
      delt = (x0-score)/x0;
    } else delt = 0;
//    sprintf(buf,outputs[ix].second.c_str(),ix+1,delt);
    sprintf(buf,aStr.c_str(),delt);
    out << buf;
  }
  delete [] psmTmp.features;
  return out.str();
}

void DataSet::modifySQT(const string & outFN, Scores * pSc ,const string greet, bool dtaSelect) {
  string line;
  ifstream sqtIn(sqtFN.data(),ios::in);
  ofstream sqtOut(outFN.data(),ios::out);
  istringstream greetStream(greet);
  sqtOut.precision(5);
  sqtOut << "H\tfile massaged by" << endl;
  while(getline(greetStream,line)) {
    sqtOut << "H\t" << line << endl;
  }
  sqtOut << "H\t" << "InputFile: " << sqtFN << endl;
  sqtOut << "H\t" << "OutputFile: " << outFN << endl;
  sqtOut << "H\t" << "Output from percolator are put into the M-lines:" << endl;
  sqtOut << "H\t" << "6th field is relpace the percolator score and" << endl;
  sqtOut << "H\t" << "7th field is relpace the negative percolator q-value" << endl;
  sqtOut << "H\t" << "The q-value is negated to be able to set a upper limit with DTASelect" << endl;
  if (VERB>1) cerr << "Writing Output to sqt file " << outFN << endl;
  
  ostringstream buff;
  istringstream lineParse;
  int lines=0,ms=0,charge=0,row =0;
  string tmp,lineRem;
  set<int> theMs;
  while (getline(sqtIn,line)) {
    if (line[0]=='H')
      sqtOut << line <<endl;
    if (line[0]=='S') {
      if(lines>1) {
        if (ms>hitsPerSpectrum)
          ms=hitsPerSpectrum;
        string record=buff.str();
        sqtOut << modifyRec(record,row,theMs, pSc, dtaSelect);
      }
      buff.str("");
      buff.clear();
      lines=1;
      buff << line << endl;
      lineParse.str(line);
      lineParse >> tmp >> tmp >> tmp >> charge;
      ms=0;
      theMs.clear();
    }
    if (line[0]=='M') {
      ++ms;
      ++lines;
      buff << line << endl;    
    }
    if (line[0]=='L') {
      ++lines;
      buff << line << endl;
      if((int)theMs.size()<hitsPerSpectrum && (!doPattern || (line.find(pattern,0)!= string::npos)==matchPattern)) {
        theMs.insert(ms-1);
      }
    }
  }
  if(lines>1) {
    string record=buff.str();
    sqtOut << modifyRec(record,row,theMs, pSc, dtaSelect);
  }
  sqtIn.close();
  sqtOut.close();
}
   
void DataSet::readFeatures(const string &in,PSMDescription &psm,int match, bool getIntra) {
  istringstream instr(in),linestr;
  ostringstream idbuild;
  string line,tmp;
  int charge;
  unsigned int scan;
  double * feat = psm.features;
  double mass,deltCn,otherXcorr=0.0,xcorr=0.0,lastXcorr=0.0, nSM=0.0;
  bool gotL=true;
  int ms=0;
  
  while (getline(instr,line)) {
    if (line[0]=='S') {
      linestr.clear();
      linestr.str(line);
      linestr >> tmp >> tmp >> scan >> charge >> tmp >> tmp >> mass >> tmp >> tmp >> nSM;
    }
    if (line[0]=='M') {
      linestr.clear();
      linestr.str(line);
      if (ms==1) {
        linestr >> tmp >> tmp >> tmp >> tmp >> deltCn >> otherXcorr;
        lastXcorr=otherXcorr;
      } else {
        linestr >> tmp >> tmp >> tmp >> tmp >> tmp >> lastXcorr;     
      }
      if (match==ms) {
        double rSp,cMass,sp,matched,expected;
        linestr.seekg(0, ios::beg);
        if (!(
          linestr >> tmp >> tmp >> rSp >> cMass >> tmp >> xcorr >> sp >> matched >> expected >> psm.peptide)) {
          cerr << "Could not parse the M line:" << endl;
          cerr << line << endl;
          exit(-1);
        }
        double dM = mass-cMass;
        psm.scan = scan;
        psm.massDiff = dM;

        feat[0]=log(rSp);                     // rank by Sp
        feat[1]=0.0;                     // delt5Cn (leave until last M line)
        feat[2]=0.0;                     // deltCn (leave until next M line)
        feat[3]=xcorr;                   // Xcorr
        feat[4]=sp;                      // Sp
        feat[5]=matched/expected;        // Fraction matched/expected ions
        feat[6]=mass;                    // Observed mass
        feat[7]=peptideLength(psm.peptide);      // Peptide length
        int nxtFeat=8;
        for(int c=getFeatureNames().getMinCharge(); c<=getFeatureNames().getMaxCharge(); c++)
          feat[nxtFeat++]=(charge==c?1.0:0.0);     // Charge
        if (enzyme!=NO_ENZYME) {
          feat[nxtFeat++]=isEnz(psm.peptide.at(0),psm.peptide.at(2));        
          feat[nxtFeat++]=isEnz(psm.peptide.at(psm.peptide.size()-3),psm.peptide.at(psm.peptide.size()-1));
          feat[nxtFeat++]=(double)cntEnz(psm.peptide);
        }
        feat[nxtFeat++]=log(nSM);
        feat[nxtFeat++]=dM;              // obs - calc mass
        feat[nxtFeat++]=(dM<0?-dM:dM);   // abs only defined for integers on some systems   
        if (calcPTMs)
          feat[nxtFeat++]=cntPTMs(psm.peptide);        
        if (hitsPerSpectrum>1)
          feat[nxtFeat++]=(ms==0?1.0:0.0);        
        if (calcAAFrequencies) {
          computeAAFrequencies(psm.peptide,&feat[nxtFeat]);
          nxtFeat += aaAlphabet.size();
        }
        if (calcDOC) {
          // These features will be set before each iteration
          psm.calcRegressionFeature();        
          feat[nxtFeat++]=abs(psm.pI-6.5);
          feat[nxtFeat++]=abs(psm.massDiff);              
          feat[nxtFeat++]=abs(psm.retentionTime);
        }
        gotL = false;
      }
      ms++;
    }
    if (line[0]=='L' && !gotL) {
      if (instr.peek() != 'L') gotL=true;
      string p;
      linestr.clear();
      linestr.str(line);
      linestr >> tmp >> p;    
      psm.proteinIds.insert(p);
    }
  }
//  feat[2]=deltCn;
  if (xcorr>0) {
    feat[1]=(xcorr-lastXcorr)/xcorr;
    feat[2]=(xcorr-otherXcorr)/xcorr;
  }
  if (!isfinite(feat[2])) cerr << in;
  if (getIntra)
    computeIntraSetFeatures(feat,psm.peptide,psm.proteinIds);
}

void DataSet::computeAAFrequencies(const string& pep, double *feat) {
  // Overall amino acid composition features
  string::size_type pos = aaAlphabet.size();
  for (;pos--;) {feat[pos]=0.0;}
  int len=0;
  for (string::const_iterator it=pep.begin()+2;it!=pep.end()-2;it++) {
    pos=aaAlphabet.find(*it);
    if (pos!=string::npos) feat[pos]++;
    len++;
  }
  for (pos = aaAlphabet.size();pos--;) {feat[pos]/=len;}
}

unsigned int DataSet::peptideLength(const string& pep) {
  unsigned int len =0;
  for (string::size_type pos = 2;(pos+2)<pep.size();pos++) {
    if (aaAlphabet.find(pep.at(pos))!=string::npos) len++;
  }
  return len;
}

unsigned int DataSet::cntPTMs(const string& pep) {
  unsigned int len =0;
  for (string::size_type pos = 2;(pos+2)<pep.size();pos++) {
    if (ptmAlphabet.find(pep.at(pos))!=string::npos) len++;
  }
  return len;
}

void DataSet::computeIntraSetFeatures(double * feat,string &pep,set<string> &prots) {
  if (DataSet::calcIntraSetFeatures) {
    feat[getFeatureNames().getIntraSetFeatNum()]=
      log((double)intra->getNumPep(pep));
//    feat[numRealFeatures-2]=
//      log((double)intra->getNumProt(prots));
    feat[getFeatureNames().getIntraSetFeatNum()+1]=
      log((double)intra->getPepSites(prots));
  }
  if (DataSet::calcQuadraticFeatures) {
    int ix = getFeatureNames().getQuadraticFeatNum();
    for (int ixf1=1;ixf1<getFeatureNames().getQuadraticFeatNum();ixf1++){
      double f1 = feat[ixf1];
      for (int ixf2=0;ixf2<ixf1;ixf2++,ix++){
        double f2 = feat[ixf2];
        double fp=f1*f2;
        double newFeature;
        if (fp>=0.0) {
          newFeature=sqrt(fp);
        } else {
          newFeature=-sqrt(-fp);
        }
        feat[ix]=newFeature;
      }        
    }
  }
}

void DataSet::computeIntraSetFeatures() {
  for(int row=0;row<numSpectra;row++) {
    computeIntraSetFeatures(&feature[rowIx(row)],psms[row].peptide,psms[row].proteinIds);
  }
  return;
}

void DataSet::readSQT(const string fname, IntraSetRelation * intraRel,const string & wild, bool match) {
  intra=intraRel;
  matchPattern=match;
  pattern=wild;
  doPattern = !wild.empty();
  sqtFN.assign(fname);
  int n = 0,charge=0,ms=0, minCharge=100, maxCharge=0;
  string line,tmp,prot;
  istringstream lineParse;  
  ifstream sqtIn;
  sqtIn.open(sqtFN.data(),ios::in);
  if (!sqtIn) {
  	cerr << "Could not open file " << sqtFN << endl;
  	exit(-1);
  }
  bool look = false;
  while (getline(sqtIn,line)) {
    if (line[0]=='S' && sqtIn.peek() != 'S') {
         lineParse.clear();
         lineParse.str(line);  
         lineParse >> tmp >> tmp >> tmp >> charge;       
         look=true;
         if (minCharge>charge) minCharge=charge; 
         if (maxCharge<charge) maxCharge=charge; 
         ms=0;
    }
    if (look & line[0]=='L' && ms < hitsPerSpectrum) {
         lineParse.clear();
         lineParse.str(line);  
         lineParse >> tmp >> prot;
         if(!doPattern || ((line.find(wild,0)!= string::npos)==match)) {
             ++ms;
             ++n;
         }
    }
  }
  if (VERB>1) cerr << n << " records in file " << sqtFN << endl;
  if (n<=0) {
    cerr << "The file " << sqtFN << " does not contain any records" << endl;
    sqtIn.close();
    exit(-1);
  }
  sqtIn.clear();
  sqtIn.seekg(0,ios::beg);

  getFeatureNames().setSQTFeatures(minCharge,maxCharge,enzyme!=NO_ENZYME,calcPTMs,hitsPerSpectrum>1,
                                   (calcAAFrequencies?aaAlphabet:""),calcIntraSetFeatures,calcQuadraticFeatures,calcDOC); 
  initFeatureTables(FeatureNames::getNumFeatures(),n, calcDOC);

  string seq;
  
  fileId = fname;
  size_t spos = fileId.rfind('/');
  if (spos!=string::npos)
    fileId.erase(0,spos+1);
  spos = fileId.find('.');
  if (spos!=string::npos)
    fileId.erase(spos);
  

  ostringstream buff,id;
  
  int ix=0,lines=0;
  string scan;
  set<int> theMs;
  while (getline(sqtIn,line)) {
    if (line[0]=='S') {
      if(lines>1) {
        string record=buff.str();
        string idstr = id.str();
        set<int>::const_iterator it;
        for(it=theMs.begin();it!=theMs.end();it++) {
          id.str("");id << idstr << '_' << (*it +1);
          ids[ix]=id.str();
          psms[ix].features = &feature[rowIx(ix)];
          readFeatures(record,psms[ix],*it,false);
          intra->registerRel(psms[ix].peptide,psms[ix].proteinIds);
          ix++;
        }
      }
      buff.str("");
      buff.clear();
      id.str("");
      lines=1;
      buff << line << endl;
      lineParse.clear();
      lineParse.str(line);
      lineParse >> tmp >> tmp >> scan >> charge;
      id << fileId << '_' << scan << '_' << charge;
      ms=0;
      theMs.clear();
    }
    if (line[0]=='M') {
      ++ms;
      ++lines;
      buff << line << endl;    
    }
    if (line[0]=='L') {
      ++lines;
      buff << line << endl;
      if((int)theMs.size()<hitsPerSpectrum && (!doPattern || (line.find(wild,0)!= string::npos)==match)) {
        theMs.insert(ms-1);
      }
    }
  }
  if(lines>1) {
    string record=buff.str();
    string idstr = id.str();
    set<int>::const_iterator it;
    for(it=theMs.begin();it!=theMs.end();it++) {
      id.str("");id << idstr << '_' << *it;
      ids[ix]=id.str();
      readFeatures(record,psms[ix],*it,false);
      intra->registerRel(psms[ix].peptide, psms[ix].proteinIds);
      ix++;
    }
  }
  sqtIn.close();
//  cout << "Read File" << endl;
}




void DataSet::initFeatureTables(const unsigned int numFeat, const unsigned int numSpec, bool regressionTable) {
  FeatureNames::setNumFeatures(numFeat);
  numSpectra = numSpec;
  feature = new double[numFeat*numSpec];
  
  if (regressionTable) {
    regressionFeature = new double[DescriptionOfCorrect::totalNumRTFeatures()*numSpec];
  }
  ids.resize(numSpectra,"");
  psms.resize(numSpectra);
  for(int ix = 0;ix<numSpectra;++ix)
    psms[ix].features = &feature[rowIx(ix)];
  if (regressionTable) {
    double *ptr = regressionFeature;
    size_t nf = DescriptionOfCorrect::totalNumRTFeatures();
    for(int ix = 0;ix<numSpectra;++ix,ptr+=nf)
      psms[ix].retentionFeatures = ptr;
  }
}

} // qranker namspace

