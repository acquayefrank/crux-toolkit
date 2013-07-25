/*******************************************************************************
 * Percolator v 1.05
 * Copyright (c) 2006-8 University of Washington. All rights reserved.
 * Written by Lukas K�ll (lukall@u.washington.edu) in the 
 * Department of Genome Science at the University of Washington. 
 *
 * $Id: SetHandler.h,v 1.20.4.1 2008/05/27 20:29:31 lukall Exp $
 *******************************************************************************/
#ifndef SETHANDLER_H_
#define SETHANDLER_H_
class SetHandler
{
protected:
    vector<DataSet *> subsets;
    vector<const double *> examples;
    double * labels;
    double * c_vec;
    int n_examples;
    IntraSetRelation * intra;
public:
	SetHandler();
	virtual ~SetHandler();
    void filelessSetup(const unsigned int numFeatures, const unsigned int numSpectra,const int label);
    void readFile(const string & p_fn, const int label);
    void readFile(const string & fn, const string & wc,const bool match);
    void static readFile(const string fn, const int label, vector<DataSet *> & sets, IntraSetRelation * intra,const string & wild = "", const bool match=false, bool calc=true);
    void static modifyFile(const string& fn, vector<DataSet *> & sets, double * w, Scores& sc , const string& greet, bool dtaSelect);
    void modifyFile(const string& fn, double * w, Scores& sc , const string& greet, bool dtaSelect);
    void generateTrainingSet(const double fdr,const double cpos, const double cneg, const Scores & sc);
	void setSet();
	void fillTestSet(SetHandler& trainSet,const string& shuffled2FN="");
    void createXvalSets(vector<SetHandler>& train,vector<SetHandler>& test, const unsigned int xval_fold);
	double * getNext(int& ,int& ) const;
    const double * getFeatures(const int setPos,const int ixPos) const;
    void readTab(const string & dataFN, const int label);
    void static writeTab(const string &dataFN, const SetHandler& norm, const SetHandler& shuff, const SetHandler& shuff2);
    void readGist(const string & dataFN, const string & labelFN, const int label);
    void static gistWrite(const string & fileNameTrunk,const SetHandler& norm,const SetHandler& shuff,const SetHandler& shuff2);
	int const getLabel(int setPos);
    inline int const getTrainingSetSize() {return examples.size();}
    void print(Scores &test);    
    inline int const getSize() {return n_examples;}
    inline DataSet * getSubSet(int ix) {return (subsets[ix]);}
    vector<DataSet *> & getSubsets() {return subsets;}
    vector<const double *> * getTrainingSet() {return & examples;}
    inline const double * getLabels() {return labels;}
    inline const double * getC() {return c_vec;}
    class Iterator {
    public:
      Iterator(SetHandler * s) : sh(s), set(0), ix(-1) {;}
      double * getNext() {return sh->getNext(set,ix);}
    private:
      SetHandler * sh;
      int set;
      int ix;
    };
    Iterator getIterator() {return Iterator(this);}
};

#endif /*SETHANDLER_H_*/
