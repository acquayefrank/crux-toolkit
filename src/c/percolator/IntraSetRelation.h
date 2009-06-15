/*******************************************************************************
 * Percolator v 1.05
 * Copyright (c) 2006-8 University of Washington. All rights reserved.
 * Written by Lukas K�ll (lukall@u.washington.edu) in the 
 * Department of Genome Science at the University of Washington. 
 *
 * $Id: IntraSetRelation.h,v 1.6.8.1 2008/05/27 20:29:31 lukall Exp $
 *******************************************************************************/
#ifndef INTRASETRELATION_H_
#define INTRASETRELATION_H_

class IntraSetRelation
{
public:
  IntraSetRelation();
  virtual ~IntraSetRelation();
  void registerRel(string pep, set<string> &prot);
  int getNumProt(string & prot) {return (numProteins.count(prot)?numProteins[prot]:1);}
  int getNumProt(set<string> &prot);
  int getNumPep(string & pep) {return (numPeptides.count(pep)?numPeptides[pep]:1);}
  int getPepSites(set<string> &prot);
protected:
  map<string,int> numProteins;
  map<string,int> numPeptides;
  map<string,set<string> > prot2pep;
};

#endif /*INTRASETRELATION_H_*/
