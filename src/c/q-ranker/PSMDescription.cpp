#include "PSMDescription.h"
#include "DescriptionOfCorrect.h"

namespace qranker {

PSMDescription::PSMDescription()
{
}

PSMDescription::~PSMDescription()
{
}

void PSMDescription::calcRegressionFeature() {
  string pep = peptide.substr(2,peptide.size()-4);  
  pI = DescriptionOfCorrect::isoElectricPoint(pep);
  if (retentionFeatures) {
    DescriptionOfCorrect::fillFeaturesAllIndex(pep, retentionFeatures);
  }
}

} // qranker namspace

