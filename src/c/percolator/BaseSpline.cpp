/*******************************************************************************
 Copyright (c) 2008 Lukas K�ll

 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:

 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software. 
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
 
 $Id: BaseSpline.cpp,v 1.6 2008/05/20 00:24:43 lukall Exp $
 
 *******************************************************************************/
#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <vector>
#include<algorithm>
using namespace std;
#include "ArrayLibrary.h"
#include "BaseSpline.h"
#include "Globals.h"

class SplinePredictor {
  BaseSpline *bs;
public:
  SplinePredictor(BaseSpline *b) {bs=b;}
  double operator() (double x) {return bs->predict(x);}
};

double BaseSpline::convergeEpsilon = 1e-2;
double BaseSpline::stepEpsilon = 1e-5;

double BaseSpline::splineEval(double xx) {
  xx = transf(xx);
  size_t n = x.size();
  vector<double>::iterator left,right = lower_bound(x.begin(),x.end(),xx);
  if (right==x.end()) {
    double derl = (g[n-1]-g[n-2])/(x[n-1]-x[n-2]) + (x[n-1]-x[n-2])/6*gamma[n-3];
    double gx = g[n-1]+(xx-x[n-1])*derl;
    return gx;  
  }
  size_t rix = right - x.begin();
  if (*right==xx)
    return g[rix];
  if (rix>0) {
    left = right;
    left--;
    double dr = *right-xx;
    double dl = xx-*left;
    double gamr = (rix<(n-1)?gamma[rix-1]:0.0);
    double gaml = (rix>1?gamma[rix-1-1]:0.0);
    double h = *right-*left;
    double gx = (dl*g[rix]+dr*g[rix-1])/h - dl*dr/6*((1.0+dl/h)*gamr+(1.0+dr/h)*gaml);
    return gx;
  }
  // if (rix==0)
  double derr = (g[1]-g[0])/(x[1]-x[0])-(x[1]-x[0])/6*gamma[0];
  double gx = g[0]-(x[0]-xx)*derr;
  return gx;
}

static double tao = 2/(1+sqrt(5));   // inverse of golden section

void BaseSpline::iterativeReweightedLeastSquares() {

  Numerical::epsilon = 1e-15;
  unsigned int n = x.size(), wrongDirection=0;
  initiateQR();
  double alpha=1.,step,cv=1e100;
  initg();
  do {
    int iter = 0;
    do {
      g=gnew;
      calcPZW();
      PackedMatrix aWiQ = alpha * diagonalPacked(Vec(n,1)/w)*Q;
      PackedMatrix M = R + Qt * aWiQ;
      gamma = Qt * z;
      solveEquation<double>(M,gamma);
      gnew= z - aWiQ*gamma;
      step =  norm(g-gnew)/n;
      if(VERB>2) cerr << "step size:" << step << endl;
    } while ((step > stepEpsilon) && (++iter<10));
    double p1 = 1-tao; 
    double p2 = tao; 
    pair<double,double> res = alphaLinearSearch(0.0,1.0,p1,p2,
                              crossValidation(-log(p1)),
                              crossValidation(-log(p2)));
                              
    if(VERB>3) cerr << "Alpha=" << res.first << ", cv=" << res.second << endl;     
                              
    if ((cv-res.second)/cv<convergeEpsilon && !(res.second>cv && wrongDirection++<3))
      break;
    cv=res.second;alpha=res.first;
  } while (true);
  if(VERB>2) cerr << "Alpha selected to be " << alpha  << endl;     
  g=gnew;
}


pair<double,double> BaseSpline::alphaLinearSearch(double min_p,double max_p, double p1, double p2, double cv1, double cv2) {
  double newPoint,newCV;
  if (cv1>cv2) {
    newPoint = p1 + tao*(max_p-p1);
    newCV = crossValidation(-log(newPoint));
    if(VERB>3) cerr << "New point with alpha=" << -log(newPoint) << ", giving cv=" << newCV << " taken in consideration" << endl;
    if ((cv1-min(cv2,newCV))/cv1<convergeEpsilon || (newPoint-p2 < 1e-5))
      return (cv2>newCV?make_pair(-log(newPoint),newCV):make_pair(-log(p2),cv2));
    return alphaLinearSearch(p1,max_p,p2,newPoint,cv2,newCV);
  } else {
    newPoint = min_p + (1-tao)*(p2 - min_p);
    newCV = crossValidation(-log(newPoint));
    if(VERB>3) cerr << "New point with alpha=" << -log(newPoint) << ", giving cv=" << newCV << " taken in consideration" << endl;
    if ((cv2-min(cv1,newCV))/cv2<convergeEpsilon || (p1-newPoint < 1e-5))
      return (cv1>newCV?make_pair(-log(newPoint),newCV):make_pair(-log(p1),cv1));
    return alphaLinearSearch(min_p,p2,newPoint,p1,newCV,cv1);  
  }
}

void BaseSpline::initiateQR() {
  int n = x.size();
  dx.resize(n-1,0.0);
  for (int ix=0;ix<n-1;ix++) {
    dx[ix]=x[ix+1]-x[ix];
    assert(dx[ix]>0); 
  }
  R.resize(n-2);
  Q.resize(n);

  //Fill Q
  Q[0].push_back(0,1/dx[0]);
  Q[1].push_back(0,-1/dx[0]-1/dx[1]);
  Q[1].push_back(1,1/dx[1]);
  for (int j=2;j<n-2;j++) {
    Q[j].push_back(j-2,1/dx[j-1]);
    Q[j].push_back(j-1,-1/dx[j-1]-1/dx[j]);
    Q[j].push_back(j,1/dx[j]);
  }
  Q[n-2].push_back(n-4,1/dx[n-3]);
  Q[n-2].push_back(n-3,-1/dx[n-3]-1/dx[n-2]);
  Q[n-1].push_back(n-3,1/dx[n-2]);
  //Fill R
  for (int i=0;i<n-3;i++) {
    R[i].push_back(i,(dx[i]+dx[i+1])/3);
    R[i].push_back(i+1,dx[i+1]/6);
    R[i+1].push_back(i,dx[i+1]/6);
  }
  R[n-3].push_back(n-3,(dx[n-3]+dx[n-2])/3);
  Qt=transpose(Q);
}

double BaseSpline::crossValidation(double alpha) {

  int n = R.size();
//  Vec k0(n),k1(n),k2(n);
  vector<double> k0(n),k1(n),k2(n);
  
  PackedMatrix B = R + alpha*Qt*diagonalPacked(Vec(n+2,1.0)/w)*Q;
  // Get the diagonals from K
  // ka[i]=B[i,i+a]=B[i+a,i]
  for (int row=0;row<n;++row) {
    for(int rowPos=B[row].packedSize();rowPos--;) {
      int col = B[row].index(rowPos);
      if (col==row) { k0[row]= B[row][rowPos]; }
      else if (col+1==row) {k1[row]=B[row][rowPos];}
      else if (col+2==row) {k2[row]=B[row][rowPos];}
    }    
  }
  
  // LDL decompose Page 26 Green Silverman
  // d[i]=D[i,i]
  // la[i]=L[i+a,i]
//  Vec d(n),l1(n),l2(n);
  vector<double> d(n),l1(n),l2(n);
  d[0]=k0[0];
  l1[0]=k1[0]/d[0];
  d[1]=k0[1]-l1[0]*l1[0]*d[0];
  
  for (int row=2;row<n;++row) {
    l2[row-2]=k2[row-2]/d[row-2];
    l1[row-1]=(k1[row-1]-l1[row-2]*l2[row-2]*d[row-2])/d[row-1];
    d[row]=k0[row] - l1[row-1]*l1[row-1]*d[row-1] - l2[row-2]*l2[row-2]*d[row-2];
  }  
  // Find diagonals of inverse Page 34 Green Silverman
  // ba[i]=B^{-1}[i+a,i]=B^{-1}[i,i+a]
//  Vec b0(n),b1(n),b2(n);
  vector<double> b0(n),b1(n),b2(n);
  for (int row=n;--row;) {
    if (row==n-1) { b0[n-1] = 1/d[n-1]; }
    else if (row==n-2) { b0[n-2] = 1/d[n-2]-l1[n-2]*b1[n-2]; }
    else {    
      b0[row] = 1/d[row] - l1[row]*b1[row] - l2[row]*b2[row];
    }
    if (row==n-1) {
      b1[n-2] = - l1[n-2]*b0[n-1];
    } else if (row>=1)
      b1[row-1] = -l1[row-1]*b0[row] - l1[row]*b1[row];
    if (row>=2)
      b2[row-2] = -l1[row-2]*b0[row];
  }


  // Calculate diagonal elemens a[i]=Aii p35 Green Silverman
  // (expanding q acording to p12) 
//  Vec a(n+2),c(n+1);
  vector<double> a(n),c(n-1);
  for (int ix=0;ix<n-1;ix++)
    c[ix] = 1/dx[ix];
  
  for (int ix=0;ix<n;ix++) {
    if (ix>0) {
      a[ix]+= b0[ix-1]*c[ix-1]*c[ix-1];
      if (ix<n-1) {      
        a[ix]+= b0[ix]*(-c[ix-1]-c[ix])*(-c[ix-1]-c[ix]);
        a[ix]+= 2*b1[ix]*c[ix]*(-c[ix-1]-c[ix]);
        a[ix]+= 2*b1[ix-1]*c[ix-1]*(-c[ix-1]-c[ix]);
        a[ix]+= 2*b2[ix-1]*c[ix-1]*c[ix];
      }
    }
    if (ix<n-1)
      a[ix]+= b0[ix+1]*c[ix]*c[ix];
  }

  double cv = 0.0;
  for (int ix=0;ix<n;ix++) {
    double f =(z[ix]-gnew[ix])/(alpha*alpha*a[ix]*a[ix]);
    cv += f*f*w[ix];
  }

  return cv;
}

void BaseSpline::predict(const vector<double>& xx, vector<double>& predict) {
  predict.clear();  
  transform(xx.begin(),xx.end(),back_inserter(predict),SplinePredictor(this));
}

void BaseSpline::setData(const vector<double>& xx) {
  x.clear();
  double minV = *min_element(xx.begin(),xx.end()); 
  double maxV = *max_element(xx.begin(),xx.end());
  if (minV>=0.0 && maxV<=1.0) {
    if(VERB>1) cerr << "Logit transforming all scores prior to PEP calculation" << endl;
    transf = Transform((minV>0.0 && maxV<1.0)?0.0:1e-5,true);
  } else if (minV>=0.0) {
    if(VERB>1) cerr << "Log transforming all scores prior to PEP calculation" << endl;
    transf = Transform(minV>0.0?0.0:1e-5,false,true);  
  } 
  transform(xx.begin(),xx.end(),back_inserter(x),transf);
}
