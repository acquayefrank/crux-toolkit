/*******************************************************************************
 Copyright 2006-2012 Lukas Käll <lukas.kall@scilifelab.se>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.

 *******************************************************************************/

#include <cassert>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <utility>
#include <algorithm>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <cmath>
#include <memory>

#include "DataSet.h"
#include "Normalizer.h"
#include "SetHandler.h"
#include "Scores.h"
#include "Globals.h"
#include "PosteriorEstimator.h"
#include "ssl.h"
#include "MassHandler.h"

#include "app/PercolatorAdapter.h"
#include "util/AminoAcidUtil.h"
#include "util/mass.h"
#include "util/Params.h"
#include "util/StringUtils.h"

inline bool operator>(const ScoreHolder& one, const ScoreHolder& other) {
  return (one.score > other.score) 
      || (one.score == other.score && one.pPSM->scan > other.pPSM->scan) 
      || (one.score == other.score && one.pPSM->scan == other.pPSM->scan && 
            one.pPSM->expMass > other.pPSM->expMass)
      || (one.score == other.score && one.pPSM->scan == other.pPSM->scan && 
            one.pPSM->expMass == other.pPSM->expMass && one.label > other.label);
}

inline bool operator<(const ScoreHolder& one, const ScoreHolder& other) {
  return (one.score < other.score) 
      || (one.score == other.score && one.pPSM->scan < other.pPSM->scan) 
      || (one.score == other.score && one.pPSM->scan == other.pPSM->scan && 
            one.pPSM->expMass < other.pPSM->expMass)
      || (one.score == other.score && one.pPSM->scan == other.pPSM->scan && 
            one.pPSM->expMass == other.pPSM->expMass && one.label < other.label);
}

inline double truncateTo(double truncateMe, const char* length) {
  char truncated[64];
  char format[64];
  strcpy(format,"%.");
  strcat(format,length);
  strcat(format,"lf\n");
  sprintf(truncated, format, truncateMe);
  return atof(truncated);
}

void ScoreHolder::printPSM(ostream& os, bool printDecoys, bool printExpMass) {
  if (!isDecoy() || printDecoys) {
    os << "    <psm p:psm_id=\"" << pPSM->getId() << "\"";
    if (printDecoys) {
      if (isDecoy())
        os << " p:decoy=\"true\"";
      else 
        os << " p:decoy=\"false\"";
    }
    os << ">" << endl;
    
    os << "      <svm_score>" << fixed      << score << "</svm_score>" << endl;
    os << "      <q_value>"   << scientific << q     << "</q_value>" << endl;
    os << "      <pep>"       << scientific << pep   << "</pep>" << endl;
    
    if (printExpMass) {
      os << "      <exp_mass>" << fixed << setprecision (4) << pPSM->expMass << "</exp_mass>" << endl;
    }   
    
    os << "      <calc_mass>" << fixed << setprecision (3) << pPSM->calcMass << "</calc_mass>" << endl;
    
    if (DataSet::getCalcDoc()) {
      os << "      <retentionTime observed=\"" 
         << pPSM->getUnnormalizedRetentionTime()
         << "\" predicted=\""
         << PSMDescriptionDOC::unnormalize(pPSM->getPredictedRetentionTime()) << "\"/>"
         << endl;
    }

    if (pPSM->getPeptideSequence().size() > 0) {
      string n = pPSM->getFlankN();
      string c = pPSM->getFlankC();
      string centpep = pPSM->getPeptideSequence();
      os << "      <peptide_seq n=\"" << n << "\" c=\"" << c << "\" seq=\"" << centpep << "\"/>" << endl;
    }
    
    std::vector<std::string>::const_iterator pidIt = pPSM->proteinIds.begin();
    for ( ; pidIt != pPSM->proteinIds.end() ; ++pidIt) {
      os << "      <protein_id>" << getRidOfUnprintablesAndUnicode(*pidIt) << "</protein_id>" << endl;
    }
    
    os << "      <p_value>" << scientific << p << "</p_value>" <<endl;
    os << "    </psm>" << endl;
  }
}

void ScoreHolder::printPeptide(ostream& os, bool printDecoys, bool printExpMass, Scores& fullset) {
  if (!isDecoy() || printDecoys) {  
    os << "    <peptide p:peptide_id=\"" << pPSM->getPeptideSequence() << "\"";
    if (printDecoys) {
      if (isDecoy())
        os << " p:decoy=\"true\"";
      else 
        os << " p:decoy=\"false\"";
    }
    os << ">" << endl;
    
    os << "      <svm_score>" << fixed       << score     << "</svm_score>" << endl;
    os << "      <q_value>"   << scientific  << q   << "</q_value>" << endl;
    os << "      <pep>"        << scientific  << pep << "</pep>" << endl;
    
    if (printExpMass) {
      os << "      <exp_mass>" << fixed << setprecision (4) << pPSM->expMass << "</exp_mass>" << endl;
    }
    os << "      <calc_mass>" << fixed << setprecision (3)  << pPSM->calcMass << "</calc_mass>" << endl;
    
    std::vector<std::string>::const_iterator pidIt = pPSM->proteinIds.begin();
    for ( ; pidIt != pPSM->proteinIds.end() ; ++pidIt) {
      os << "      <protein_id>" << getRidOfUnprintablesAndUnicode(*pidIt) << "</protein_id>" << endl;
    }
    
    os << "      <p_value>" << scientific << p << "</p_value>" <<endl;
    os << "      <psm_ids>" << endl;
    
    // output all psms that contain the peptide
    std::vector<PSMDescription*>::const_iterator psmIt = fullset.getPsms(pPSM).begin();
    for ( ; psmIt != fullset.getPsms(pPSM).end() ; ++psmIt) {
      os << "        <psm_id>" << (*psmIt)->getId() << "</psm_id>" << endl;
    }
    os << "      </psm_ids>" << endl;
    os << "    </peptide>" << endl;
  }
}

void Scores::merge(std::vector<Scores>& sv, double fdr) {
  scores_.clear();
  for (std::vector<Scores>::iterator a = sv.begin(); a != sv.end(); a++) {
    sort(a->begin(), a->end(), greater<ScoreHolder> ());
    if (usePi0_) a->estimatePi0();
    a->calcQ(fdr);
    a->normalizeScores(fdr);
    copy(a->begin(), a->end(), back_inserter(scores_));
  }
  postMergeStep();
}

void Scores::postMergeStep() {
  sort(scores_.begin(), scores_.end(), greater<ScoreHolder> ());
  totalNumberOfDecoys_ = count_if(scores_.begin(),
      scores_.end(),
      mem_fun_ref(&ScoreHolder::isDecoy));
  totalNumberOfTargets_ = count_if(scores_.begin(),
      scores_.end(),
      mem_fun_ref(&ScoreHolder::isTarget));
  targetDecoySizeRatio_ = totalNumberOfTargets_ / max(1.0, (double)totalNumberOfDecoys_);
  if (usePi0_) estimatePi0();
  else pi0_ = 1.0;
}

void Scores::printRetentionTime(ostream& outs, double fdr) {
  std::vector<ScoreHolder>::iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    if (scoreIt->isTarget()) 
      outs << scoreIt->pPSM->getUnnormalizedRetentionTime() << "\t"
        << PSMDescriptionDOC::unnormalize(doc_.estimateRT(scoreIt->pPSM->getRetentionFeatures()))
        << "\t" << scoreIt->pPSM->peptide << endl;
  }
}

double Scores::calcScore(const double* feat, const std::vector<double>& w) const {
  register int ix = FeatureNames::getNumFeatures();
  register double score = w[ix];
  for (; ix--;) {
    score += feat[ix] * w[ix];
  }
  return score;
}

void Scores::scoreAndAddPSM(ScoreHolder& sh, 
    const std::vector<double>& rawWeights, FeatureMemoryPool& featurePool) {
  const unsigned int numFeatures = FeatureNames::getNumFeatures();
  if (DataSet::getCalcDoc()) {
    size_t numRTFeatures = RTModel::totalNumRTFeatures();
    double* rtFeatures = new double[numRTFeatures]();
    DescriptionOfCorrect::calcRegressionFeature(sh.pPSM);
    for (size_t i = 0; i < numRTFeatures; ++i) {
      rtFeatures[i] = Normalizer::getNormalizer()->normalize(rtFeatures[i], numFeatures + i);
    }
    sh.pPSM->setRetentionFeatures(rtFeatures);
    doc_.setFeatures(sh.pPSM);
  }
  
  for (unsigned int j = 0; j < numFeatures; j++) {
    sh.score += sh.pPSM->features[j] * rawWeights[j];
  }
  sh.score += rawWeights[numFeatures];
  
  featurePool.deallocate(sh.pPSM->features);
  sh.pPSM->deleteRetentionFeatures();
  
  if (sh.label != 1 && sh.label != -1) {
    std::cerr << "Warning: the PSM " << sh.pPSM->getId()
        << " has a label not in {1,-1} and will be ignored." << std::endl;
    PSMDescription::deletePtr(sh.pPSM);
  } else {
    scores_.push_back(sh);
  }
}

void Scores::print(int label, std::ostream& os) {
  std::vector<ScoreHolder>::iterator scoreIt = scores_.begin();

  bool lnNumDSP = false;
  int lnNumSPIdx = PercolatorAdapter::findFeatureIndex("lnnumsp");
  if (lnNumSPIdx == -1) {
    if ((lnNumSPIdx = PercolatorAdapter::findFeatureIndex("lnnumdsp")) != -1) {
      lnNumDSP = true;
    }
  }

  os //<< "file\t"
     << "file_idx\t"
     << "scan\t"
     << "charge\t"
     << "spectrum precursor m/z\t"
     << "spectrum neutral mass\t"
     << "peptide mass\t"
     << "percolator score\t"
     << "percolator q-value\t"
     << "percolator PEP\t"
     << (lnNumDSP ? "distinct matches/spectrum" : "total matches/spectrum") << '\t'
     << "sequence\t"
     //<< "modifications\t"
     << "protein id\t"
     << "flanking aa\n";

  Normalizer* normalizer = Normalizer::getNormalizer();
  double* nSub = normalizer->getSub();
  double* nDiv = normalizer->getDiv();

  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    if (scoreIt->label != label) {
      continue;
    }
    std::string fileIdxStr, chargeStr;
    int charge = 0;
    const std::string& psmId = scoreIt->pPSM->getId();
    std::vector<std::string> idPieces = StringUtils::Split(psmId, '_');
    if (idPieces.size() == 5) {
      fileIdxStr = idPieces[1];
      chargeStr = idPieces[3];
      StringUtils::TryFromString(chargeStr, &charge);
    }

    std::string flankingStr = "XX";
    std::string seq = scoreIt->pPSM->peptide;
    if (seq.length() >= 5 && seq[1] == '.' && seq[seq.length() - 2] == '.') {
      flankingStr[0] = seq[0];
      flankingStr[1] = seq[seq.length() - 1];
      seq = seq.substr(2, seq.length() - 4);
    }

    double neutralMass = scoreIt->pPSM->expMass - MASS_PROTON;
    double peptideMass = MASS_H2O_MONO; // Reported as 0 if a problem occurs
    for (size_t i = 0; i < seq.length(); i++) {
      if (seq[i] == '[') {
        double modMass = 0.0;
        size_t j = seq.find(']', ++i);
        if (j == string::npos || !StringUtils::TryFromString(seq.substr(i, j - i), &modMass)) {
          peptideMass = 0;
          break;
        }
        peptideMass += modMass;
        i = j;
      } else {
        try {
          peptideMass += AminoAcidUtil::GetMass(seq[i], true);
        } catch (...) {
          peptideMass = 0;
          break;
        }
      }
    }

    int precision = Params::GetInt("precision");
    int massPrecision = Params::GetInt("mass-precision");
    os //<< "" << '\t' // file
       << fileIdxStr << '\t'
       << scoreIt->pPSM->scan << '\t'
       << chargeStr << '\t'
       << StringUtils::ToString((charge > 0 ? neutralMass/charge + MASS_PROTON : 0), massPrecision) << '\t'
       << StringUtils::ToString(neutralMass, massPrecision) << '\t'
       << StringUtils::ToString(peptideMass, massPrecision) << '\t'
       << StringUtils::ToString(scoreIt->score, precision) << '\t'
       << StringUtils::ToString(scoreIt->q, precision, false) << '\t'
       << StringUtils::ToString(scoreIt->pep, precision, false) << '\t'
       << (lnNumSPIdx >= 0 ? exp(PercolatorAdapter::unnormalize(scoreIt->pPSM, lnNumSPIdx, nDiv, nSub)) : 0) << '\t'
       << seq << '\t'
       //<< "" << '\t' // mods
       << StringUtils::Join(scoreIt->pPSM->proteinIds, ',') << '\t'
       << flankingStr << std::endl;
  }
}

void Scores::fillFeatures(SetHandler& setHandler) {
  scores_.clear();
  setHandler.fillFeatures(scores_,1);
  setHandler.fillFeatures(scores_,-1);
  totalNumberOfTargets_ = setHandler.getSizeFromLabel(1);
  totalNumberOfDecoys_ = setHandler.getSizeFromLabel(-1);
  targetDecoySizeRatio_ = (double)totalNumberOfTargets_ / totalNumberOfDecoys_;
  
  if (VERB > 1) {
    cerr << "Train/test set contains " << totalNumberOfTargets_
        << " positives and " << totalNumberOfDecoys_
        << " negatives, size ratio=" << targetDecoySizeRatio_
        << " and pi0=" << pi0_ << endl;
  }
  
  if (totalNumberOfTargets_ == 0) {
    throw MyException("Error: no target PSMs were provided.\n");
  } else if (totalNumberOfDecoys_ == 0) {
    throw MyException("Error: no decoy PSMs were provided.\n");
  }
  
  // check for the minimum recommended number of positive and negative hits
  if (totalNumberOfTargets_ <= (unsigned)(FeatureNames::getNumFeatures() * 5)) {
    std::cerr << "Warning : the number of positive samples read is too small to perform a correct classification.\n" << std::endl;
  }
  if (totalNumberOfDecoys_ <= (unsigned)(FeatureNames::getNumFeatures() * 5)) {
    std::cerr << "Warning : the number of negative samples read is too small to perform a correct classification.\n" << std::endl;
  }
}

/**
 * Divides the PSMs from pin file into xval_fold cross-validation sets based on
 * their spectrum scan number
 * @param train vector containing the training sets of PSMs
 * @param test vector containing the test sets of PSMs
 * @param xval_fold: number of folds in train and test
 */
void Scores::createXvalSetsBySpectrum(std::vector<Scores>& train, 
    std::vector<Scores>& test, const unsigned int xval_fold, 
    FeatureMemoryPool& featurePool) {
  // set the number of cross validation folds for train and test to xval_fold
  train.resize(xval_fold, Scores(usePi0_));
  test.resize(xval_fold, Scores(usePi0_));
  // remain keeps track of residual space available in each fold
  std::vector<int> remain(xval_fold);
  // set values for remain: initially each fold is assigned (tot number of
  // scores_ / tot number of folds)
  int fold = xval_fold, ix = scores_.size();
  while (fold--) {
    remain[fold] = ix / (fold + 1);
    ix -= remain[fold];
  }

  // store possible spectra with relative scores_
  multimap<unsigned int,ScoreHolder> spectraScores;
  // populate spectraScores
  std::vector<ScoreHolder>::iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    spectraScores.insert(std::make_pair(scoreIt->pPSM->scan, *scoreIt));
  }

  // put scores into the folds; choose a fold (at random) and change it only
  // when scores from a new spectra are encountered
  // note: this works because multimap is an ordered container!
  unsigned int previousSpectrum = spectraScores.begin()->first;
  size_t randIndex = PseudoRandom::lcg_rand() % xval_fold;
  for (multimap<unsigned int, ScoreHolder>::iterator it = spectraScores.begin(); 
        it != spectraScores.end(); ++it) {
    const unsigned int curScan = (*it).first;
    const ScoreHolder sh = (*it).second;
    // if current score is from a different spectra than the one encountered in
    // the previous iteration, choose new fold
    
    if (previousSpectrum != curScan) {
      randIndex = PseudoRandom::lcg_rand() % xval_fold;
      // allow only indexes of folds that are non-full
      while (remain[randIndex] <= 0){
        randIndex = PseudoRandom::lcg_rand() % xval_fold;
      }
    }
    // insert
    for (unsigned int i = 0; i < xval_fold; ++i) {
      if (i == randIndex) {
        test[i].addScoreHolder(sh);
      } else {
        train[i].addScoreHolder(sh);
      }
    }
    // update number of free position for used fold
    --remain[randIndex];
    // set previous spectrum to current one for next iteration
    previousSpectrum = curScan;
  }

  // calculate ratios of target over decoy for train and test set
  for (unsigned int i = 0; i < xval_fold; ++i) {
    train[i].recalculateSizes();
    test[i].recalculateSizes();
  }
  
  std::map<double*, double*> movedAddresses;
  size_t idx = 0;
  for (unsigned int i = 0; i < xval_fold; ++i) {
    bool isTarget = true;
    test[i].reorderFeatureRows(featurePool, isTarget, movedAddresses, idx);
    isTarget = false;
    test[i].reorderFeatureRows(featurePool, isTarget, movedAddresses, idx);
  }
}

void Scores::recalculateSizes() {
  totalNumberOfTargets_ = 0;
  totalNumberOfDecoys_ = 0;
  std::vector<ScoreHolder>::const_iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    if (scoreIt->isTarget()) {
      ++totalNumberOfTargets_;
    } else {
      ++totalNumberOfDecoys_;
    }
  }
  targetDecoySizeRatio_ = totalNumberOfTargets_ / (double)totalNumberOfDecoys_;
}

void Scores::reorderFeatureRows(FeatureMemoryPool& featurePool, 
    bool isTarget, std::map<double*, double*>& movedAddresses, size_t& idx) {
  size_t numFeatures = FeatureNames::getNumFeatures();
  std::vector<ScoreHolder>::const_iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    if (scoreIt->isTarget() == isTarget) {
      double* newAddress = featurePool.addressFromIdx(idx++);
      double* oldAddress = scoreIt->pPSM->features;
      while (movedAddresses.find(oldAddress) != movedAddresses.end()) {
        oldAddress = movedAddresses[oldAddress];
      }
      std::swap_ranges(oldAddress, oldAddress + numFeatures, newAddress);
      scoreIt->pPSM->features = newAddress;
      if (oldAddress != newAddress) {
        movedAddresses[newAddress] = oldAddress;
      }
    }
  }
}

void Scores::normalizeScores(double fdr) {
  // sets q=fdr to 0 and the median decoy to -1, linear transform the rest to fit
  unsigned int medianIndex = std::max(0u,totalNumberOfDecoys_/2u),decoys=0u;
  std::vector<ScoreHolder>::iterator it = scores_.begin();
  double q1 = it->score;
  double median = q1 + 1.0;

  for (; it != scores_.end(); ++it) {
    if (it->q < fdr)
      q1 = it->score;
    if (it->isDecoy()) {
      if(++decoys==medianIndex) {
        median = it->score;
        break;
      }
    }
  }
  //NOTE perhaps I should also check when q1 and median are both negatives
  //NOTE in such cases the normalization could give negative scores_ which would
  //     cause an assertion to fail in qvality
  if (q1 <= median || it == scores_.end()) {
    ostringstream temp;
    temp << "Error : the input data has too good separation between target "
         << "and decoy PSMs.\n" << std::endl;
    throw MyException(temp.str());
  }
   
  double diff = q1-median;
  std::vector<ScoreHolder>::iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    scoreIt->score -= q1;
    scoreIt->score /= diff;
  }
  
}

/**
 * Calculates the SVM cost/score of each PSM and sorts them
 * @param w normal vector used for SVM cost
 * @param fdr FDR threshold specified by user (default 0.01)
 * @return number of true positives
 */
int Scores::calcScores(std::vector<double>& w, double fdr) {
  unsigned int ix;
  std::vector<ScoreHolder>::iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    scoreIt->score = calcScore(scoreIt->pPSM->features, w);
  }
  sort(scores_.begin(), scores_.end(), greater<ScoreHolder> ());
  if (VERB > 3) {
    if (scores_.size() >= 10) {
      cerr << "10 best scores and labels" << endl;
      for (ix = 0; ix < 10; ix++) {
        cerr << scores_[ix].score << " " << scores_[ix].label << endl;
      }
      cerr << "10 worst scores and labels" << endl;
      for (ix = scores_.size() - 10; ix < scores_.size(); ix++) {
        cerr << scores_[ix].score << " " << scores_[ix].label << endl;
      }
    } else {
      cerr << "Too few scores to display top and bottom PSMs (" << scores_.size() << " scores found)." << endl;
    }
  }
  return calcQ(fdr);
}

/**
 * Calculates the q-value for each psm in scores_: the q-value is the minimal
 * FDR of any set that includes the particular psm
 * @param fdr FDR threshold specified by user (default 0.01)
 * @return number of true positives
 */
int Scores::calcQ(double fdr) {
  assert(totalNumberOfDecoys_+totalNumberOfTargets_==size());

  int targets = 0, decoys = 0;
  double efp = 0.0, q; // estimated false positives, q value
  
  int numPos = 0;
  /*
  double p1 = 1e-3, p2 = 1e-2;
  size_t idx1, idx2;
  */
  // NOTE check this
  std::vector<ScoreHolder>::iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    if (scoreIt->isTarget()) {
      targets++;
      scoreIt->p = (decoys+(double)1)/(totalNumberOfDecoys_+(double)1);
      /*if (scoreIt->p < p1) idx1 = targets + decoys;
      if (scoreIt->p < p2) idx2 = targets + decoys;*/
    } else {
      decoys++;
      if (usePi0_) {
        efp = pi0_ * decoys * targetDecoySizeRatio_;
      } else {
        efp = decoys;
      }
      scoreIt->p = (decoys)/(double)(totalNumberOfDecoys_);
    }
    if (targets) {
      q = efp / (double)targets;
    } else {
      q = pi0_;
    }
    if (q > pi0_) {
      q = pi0_;
    }
    scoreIt->q = q;
    if (fdr >= q) {
      numPos = targets;
    }
  }
  if (scores_.size() > 0) {
    for (int ix = scores_.size(); --ix;) {
      if (scores_[ix - 1].q > scores_[ix].q) {
        scores_[ix - 1].q = scores_[ix].q;
      }
    }
  }
  
  /* extrapolate p-values beyond last decoy
  double score1 = scores_.at(idx1).score;
  double score2 = scores_.at(idx2).score;
  double slope = (log(p2) - log(p1)) / (score2 - score1);
  
  std::cerr << "P-value slope: " << slope << " " << score1 << " " << score2 << std::endl;
  for (size_t idx = 0; idx < scores_.size(); ++idx) {
    if (scores_.at(idx).p < p1) {
      if (scores_.at(idx).isTarget()) { // isTarget
        scores_.at(idx).p = exp((scores_.at(idx).score - score1)*slope + log(p1));
        if (scores_.at(idx).p == 0.0) scores_.at(idx).p = DBL_MIN;
      }
    } else {
      break;
    }
  }
  */
  
  return numPos;
}

void Scores::generateNegativeTrainingSet(AlgIn& data, const double cneg) {
  unsigned int ix2 = 0;
  std::vector<ScoreHolder>::const_iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    if (scoreIt->isDecoy()) {
      data.vals[ix2] = scoreIt->pPSM->features;
      data.Y[ix2] = -1;
      data.C[ix2++] = cneg;
    }
  }
  data.negatives = ix2;
}

void Scores::generatePositiveTrainingSet(AlgIn& data, const double fdr,
    const double cpos) {
  unsigned int ix2 = data.negatives, p = 0;
  std::vector<ScoreHolder>::const_iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    if (scoreIt->isTarget()) {
      if (fdr < scoreIt->q) {
        break;
      }
      data.vals[ix2] = scoreIt->pPSM->features;
      data.Y[ix2] = 1;
      data.C[ix2++] = cpos;
      ++p;
    }
  }
  data.positives = p;
  data.m = ix2;
}

/**
 * Routine that sees to that only unique peptides are kept (used for analysis
 * on peptide-fdr rather than psm-fdr)
 */
void Scores::weedOutRedundant() {
  // lexicographically order the scores_ (based on peptides names,labels and scores)
  std::sort(scores_.begin(), scores_.end(), lexicOrderProb());
  
  /*
  * much simpler version but it does not fill up the peptide-PSM map:
  * scores_.erase(std::unique(scores_.begin(), scores_.end(), mycmp), scores_.end());
  */
  
  std::string previousPeptide = "";
  int previousLabel = 0;
  size_t lastWrittenIdx = 0u;
  for (size_t idx = 0u; idx < scores_.size(); ++idx){
    std::string currentPeptide = scores_.at(idx).pPSM->getPeptideSequence();
    int currentLabel = scores_.at(idx).label;
    if (currentPeptide != previousPeptide || currentLabel != previousLabel) {
      // insert as a new score
      scores_.at(lastWrittenIdx++) = scores_.at(idx);
      previousPeptide = currentPeptide;
      previousLabel = currentLabel;
    }
    // append the psm
    peptidePsmMap_[scores_.at(lastWrittenIdx - 1).pPSM].push_back(scores_.at(idx).pPSM);
  }
  scores_.resize(lastWrittenIdx);
  postMergeStep();
}

/**
 * Routine that sees to that only unique spectra are kept for TDC
 */
void Scores::weedOutRedundantTDC() {
  // order the scores (based on spectra id and score)
  std::sort(scores_.begin(), scores_.end(), OrderScanMassCharge());  
  scores_.erase(std::unique(scores_.begin(), scores_.end(), UniqueScanMassCharge()), scores_.end());
  
  /* does not actually release memory because of memory fragmentation
  double previousExpMass = 0.0;
  unsigned int previousScan = 0u;
  size_t lastWrittenIdx = 0u;
  for (size_t idx = 0u; idx < scores_.size(); ++idx){
    double currentExpMass = scores_.at(idx).pPSM->expMass;
    int currentScan = scores_.at(idx).pPSM->scan;
    if (currentExpMass != previousExpMass || currentScan != previousScan) {
      // insert as a new score
      scores_.at(lastWrittenIdx++).swap(scores_.at(idx));
      previousScan = currentScan;
      previousExpMass = currentExpMass;
    } else {
      PSMDescription::deletePtr(scores_.at(idx).pPSM);
    }
  }
  scores_.resize(lastWrittenIdx);
  */
  postMergeStep();
}

void Scores::recalculateDescriptionOfCorrect(const double fdr) {
  doc_.clear();
  std::vector<ScoreHolder>::const_iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    if (scoreIt->isTarget()) {
      //      if (fdr>scores_[ix1].pPSM->q) {
      if (0.0 >= scoreIt->q) {
        doc_.registerCorrect(scoreIt->pPSM);
      }
    }
  }
  doc_.trainCorrect();
}

void Scores::setDOCFeatures(Normalizer* pNorm) {
  std::vector<ScoreHolder>::const_iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    doc_.setFeaturesNormalized(scoreIt->pPSM, pNorm);
  }
}

int Scores::getInitDirection(const double fdr, std::vector<double>& direction) {
  int bestPositives = -1;
  int bestFeature = -1;
  bool lowBest = false;
  for (unsigned int featNo = 0; featNo < FeatureNames::getNumFeatures(); featNo++) {
    for (std::vector<ScoreHolder>::iterator scoreIt = scores_.begin(); 
         scoreIt != scores_.end(); ++scoreIt) {
      scoreIt->score = scoreIt->pPSM->features[featNo];
    }
    sort(scores_.begin(), scores_.end());
    // check once in forward direction (high scores are good) and once in backward
    for (int i = 0; i < 2; i++) {
      int positives = 0, decoys = 0;
      double efp = 0.0, q;
      std::vector<ScoreHolder>::const_iterator scoreIt = scores_.begin();
      for ( ; scoreIt != scores_.end(); ++scoreIt) {
        if (scoreIt->isTarget()) {
          positives++;
        } else {
          decoys++;
          if (usePi0_) {
            efp = pi0_ * decoys * targetDecoySizeRatio_;
          } else {
            efp = decoys;
          }
        }
        if (positives) {
          q = efp / (double)positives;
        } else {
          q = pi0_;
        }
        if (fdr <= q) {
          if (positives > bestPositives && scores_.begin()->score != scoreIt->score) {
            bestPositives = positives;
            bestFeature = featNo;
            lowBest = (i == 0);
          }
          if (i == 0) {
            reverse(scores_.begin(), scores_.end());
          }
          break;
        }
      }
    }
  }
  for (int ix = FeatureNames::getNumFeatures(); ix--;) {
    direction[ix] = 0;
  }
  direction[bestFeature] = (lowBest ? -1 : 1);
  if (VERB > 1) {
    cerr << "Selected feature number " << bestFeature + 1
        << " as initial search direction, could separate "
        << bestPositives << " positives in that direction" << endl;
  }
  return bestPositives;
}

void Scores::estimatePi0() {
  std::vector<pair<double, bool> > combined;
  std::vector<double> pvals;
  transform(scores_.begin(), scores_.end(), back_inserter(combined),
            mem_fun_ref(&ScoreHolder::toPair));
  // Estimate pi0_
  PosteriorEstimator::getPValues(combined, pvals);
  pi0_ = PosteriorEstimator::estimatePi0(pvals);
}

void Scores::calcPep() {
  std::vector<pair<double, bool> > combined;
  transform(scores_.begin(),
      scores_.end(),
      back_inserter(combined),
      mem_fun_ref(&ScoreHolder::toPair));
  std::vector<double> peps;
  // Logistic regression on the data
  PosteriorEstimator::estimatePEP(combined, usePi0_, pi0_, peps, true);
  for (size_t ix = 0; ix < scores_.size(); ix++) {
    scores_[ix].pep = peps[ix];
  }
}

unsigned Scores::getQvaluesBelowLevel(double level) {
  unsigned hits = 0;
  std::vector<ScoreHolder>::const_iterator scoreIt = scores_.begin();
  for ( ; scoreIt != scores_.end(); ++scoreIt) {
    if (scoreIt->isTarget() && scoreIt->q < level) {
      hits++;
    }
  }
  return hits;
}
