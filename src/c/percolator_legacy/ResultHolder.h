/*******************************************************************************
 * Percolator v 1.05
 * Copyright (c) 2006-8 University of Washington. All rights reserved.
 * Written by Lukas K�ll (lukall@u.washington.edu) in the 
 * Department of Genome Science at the University of Washington. 
 *
 * $Id: ResultHolder.h,v 1.2.4.1 2008/05/27 20:29:32 lukall Exp $
 *******************************************************************************/
#ifndef RESULTHOLDER_H_
#define RESULTHOLDER_H_

class ResultHolder
{
public:
	ResultHolder();
	ResultHolder(const double score,const double q,const double po,const string& i,const string& pe = "",const string& p  = "");
	virtual ~ResultHolder();
    double score,q,posterior;
    string id,pepSeq,prot;
};

bool operator>(const ResultHolder &one, const ResultHolder &other);
bool operator<(const ResultHolder &one, const ResultHolder &other); 
ostream& operator<<(ostream &out,const ResultHolder &obj);

#endif /*RESULTHOLDER_H_*/
